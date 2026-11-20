#!/usr/bin/env python3
"""Generate fork_forms.h: the Megas, Primals, Gigantamax and fusion forms that
adventures (android/FEATURES_PLAN.md, Phase 12) can use.

    python3 tools/gen_forms.py            # regenerate fork_forms.h
    python3 tools/gen_forms.py --check    # report only, write nothing

Three sources, each used for exactly one thing:

  SpriteCollab's tracker.json   WHICH forms exist and their subfolder numbers
  a HEAD per form               whether the art is REALLY there (tracker.json
                                under-reports: 11 forms it calls missing exist)
  PokeAPI pokemon/<variety>     stats and typing

A FORM WITH NO ART NEVER SPAWNS, the same rule as noart.h. It still gets a row
(with art=0) so that it keeps its id when upstream draws it later.

FORM IDS ARE PERSISTED (the fork's record names a kept creature's form by its
index in FORM_TBL), so the table is APPEND-ONLY, exactly like MOVES: existing
rows are read back out of the committed fork_forms.h and kept in their order,
and anything new goes at the end. Reordering would silently turn every kept
Kyurem-Black into whatever now sits at its index. A row that disappears
upstream is kept too, with its art cleared.

Downloads go to tools/pmd_cache/ and tools/pokeapi_cache/forms/, both
gitignored. curl, not urllib, for the reason pack_pmd.py gives.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'fork_forms.h')
PMD_CACHE = os.path.join(HERE, 'pmd_cache')
API_CACHE = os.path.join(HERE, 'pokeapi_cache', 'forms')
RAW = 'https://raw.' + 'githubusercontent.com/PMDCollab/SpriteCollab/master'
POKEAPI = 'https://pokeapi.co/api/v2'

# Kinds. A battle STATE (Mega, Primal, G-Max, Ultra Burst) is carried by a kept
# creature as permission to transform; a FUSION is kept as itself. Order is
# persisted with the rows, so append-only too.
KINDS = ['FK_NONE', 'FK_MEGA', 'FK_PRIMAL', 'FK_GMAX', 'FK_ULTRA', 'FK_FUSION']

# SpriteCollab subgroup name -> (kind, PokeAPI variety suffix, label).
# Alternate colours, Sky Temple variants and the like are not forms and are
# deliberately absent: anything not named here is ignored.
SUFFIX = {
    'Mega':         ('FK_MEGA', 'mega', 'MEGA'),
    'Mega_X':       ('FK_MEGA', 'mega-x', 'MEGA X'),
    'Mega_Y':       ('FK_MEGA', 'mega-y', 'MEGA Y'),
    'Mega_Z':       ('FK_MEGA', 'mega-z', 'MEGA Z'),
    'Primal':       ('FK_PRIMAL', 'primal', 'PRIMAL'),
    'Gigantamax':   ('FK_GMAX', 'gmax', 'G-MAX'),
    'Eternamax':    ('FK_GMAX', 'eternamax', 'ETERNAMAX'),
    'Ultra':        ('FK_ULTRA', 'ultra', 'ULTRA'),
    'Black':        ('FK_FUSION', 'black', 'BLACK'),
    'White':        ('FK_FUSION', 'white', 'WHITE'),
    'Dusk_Mane':    ('FK_FUSION', 'dusk', 'DUSK MANE'),
    'Dawn_Wings':   ('FK_FUSION', 'dawn', 'DAWN WINGS'),
    'Ice_Rider':    ('FK_FUSION', 'ice', 'ICE RIDER'),
    'Shadow_Rider': ('FK_FUSION', 'shadow', 'SHADOW RIDER'),
}
# "White" is also Basculin's stripe and Flabebe's flower, which are colours,
# not fusions. Fusions are these three species and no others.
FUSION_DEX = {646, 800, 898}   # Kyurem, Necrozma, Calyrex


def curl(args):
    return subprocess.run(['curl', '-sS', '--retry', '2', '-A', 'Mozilla/5.0'] + args,
                          capture_output=True, text=True)


def get_json(url, path):
    if os.path.exists(path):
        return json.load(open(path))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    r = curl(['-f', '-o', path, url])
    if r.returncode != 0:
        if os.path.exists(path):
            os.remove(path)
        return None
    return json.load(open(path))


def head_ok(url):
    """True on 200, False on 404, and an exception on anything else -- a
    network failure must never read as "no art" (check_sprites.py's lesson)."""
    r = curl(['-I', '-o', '/dev/null', '-w', '%{http_code}', url])
    code = r.stdout.strip()
    if code == '200':
        return True
    if code == '404':
        return False
    raise RuntimeError(f'HEAD {url} -> {code or r.stderr.strip()}')


def type_ids():
    """T_* names in dex.h's enum order, so a type is its index there."""
    txt = open(os.path.join(ROOT, 'dex.h')).read()
    body = re.search(r'enum PkType : uint8_t \{([^}]*)\}', txt).group(1)
    names = [n.strip() for n in body.split(',') if n.strip().startswith('T_')]
    return {n[2:].lower(): n for n in names}


def committed_rows():
    """(dex, sub) of every row already in fork_forms.h, in order."""
    if not os.path.exists(OUT):
        return []
    rows = []
    for m in re.finditer(r'^\s*\{ (\d+), (\d+), FK_', open(OUT).read(), re.M):
        if int(m.group(1)):   # row 0 is "no form", not data
            rows.append((int(m.group(1)), int(m.group(2))))
    return rows


def candidates(tracker):
    out = []
    for dexs, e in tracker.items():
        dex = int(dexs)
        if dex == 0:
            continue
        for subs, g in e.get('subgroups', {}).items():
            name = g.get('name', '')
            if subs == '0000' or name not in SUFFIX:
                continue
            kind = SUFFIX[name][0]
            if kind == 'FK_FUSION' and dex not in FUSION_DEX:
                continue
            shiny_sub = next((k for k, sg in g.get('subgroups', {}).items()
                              if sg.get('name', '').lower() == 'shiny'), None)
            out.append({'dex': dex, 'sub': int(subs), 'name': name,
                        'species': e['name'], 'shiny_sub': shiny_sub})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()

    dexh = open(os.path.join(ROOT, 'dex.h')).read()
    dex_count = int(re.search(r'#define DEX_COUNT (\d+)', dexh).group(1))
    types = type_ids()

    tracker = get_json(f'{RAW}/tracker.json', os.path.join(PMD_CACHE, 'tracker.json'))
    if tracker is None:
        sys.exit('could not fetch tracker.json')
    cands = [c for c in candidates(tracker) if c['dex'] <= dex_count]

    def probe(c):
        base = f"{RAW}/sprite/{c['dex']:04d}/{c['sub']:04d}"
        c['art'] = head_ok(f'{base}/AnimData.xml')
        c['shiny'] = bool(c['art'] and c['shiny_sub']
                          and head_ok(f"{base}/{c['shiny_sub']}/AnimData.xml"))
        return c

    with ThreadPoolExecutor(16) as ex:
        cands = list(ex.map(probe, cands))

    # Stats and typing from PokeAPI. The species lists its varieties, which is
    # the only reliable way to the variety's slug (MR. MIME, FLABEBE...).
    rows, no_api = {}, []
    for c in cands:
        sp = get_json(f"{POKEAPI}/pokemon-species/{c['dex']}",
                      os.path.join(API_CACHE, f"species_{c['dex']:04d}.json"))
        if sp is None:
            raise RuntimeError(f"pokemon-species/{c['dex']} failed")
        want = '-' + SUFFIX[c['name']][1]
        var = next((v['pokemon']['name'] for v in sp['varieties']
                    if v['pokemon']['name'] == sp['name'] + want), None)
        if var is None:
            no_api.append(f"{c['species']}-{c['name']}")
            continue
        p = get_json(f'{POKEAPI}/pokemon/{var}', os.path.join(API_CACHE, f'{var}.json'))
        if p is None:
            raise RuntimeError(f'pokemon/{var} failed')
        st = {s['stat']['name']: s['base_stat'] for s in p['stats']}
        ty = [types[t['type']['name']] for t in sorted(p['types'], key=lambda t: t['slot'])]
        rows[(c['dex'], c['sub'])] = {
            'dex': c['dex'], 'sub': c['sub'], 'kind': SUFFIX[c['name']][0],
            'label': SUFFIX[c['name']][2], 'species': c['species'], 'var': var,
            'stats': [st['hp'], st['attack'], st['defense'], st['speed'],
                      st['special-attack'], st['special-defense']],
            't1': ty[0], 't2': ty[1] if len(ty) > 1 else 'T_NONE',
            'art': c['art'], 'shiny': c['shiny'],
        }

    # Append-only ordering: committed rows first, in their order.
    order = committed_rows()
    lost = [k for k in order if k not in rows]
    if lost:
        sys.exit(f'rows in fork_forms.h no longer derivable: {lost}. Keeping a '
                 'persisted id needs its data; fix the source, do not delete the row.')
    order += sorted(k for k in rows if k not in order)

    art = [rows[k] for k in order if rows[k]['art']]
    print(f'{len(cands)} candidate forms, {len(rows)} with PokeAPI data, {len(art)} with art')
    for kind in KINDS[1:]:
        n = [r for r in rows.values() if r['kind'] == kind]
        print(f"  {kind:10} {len(n):3} rows, {sum(r['art'] for r in n):3} art, "
              f"{sum(r['shiny'] for r in n):3} shiny art")
    if no_api:
        print('no PokeAPI variety (not emitted):', ', '.join(sorted(no_api)))
    if a.check:
        return

    with open(OUT, 'w') as f:
        f.write('#pragma once\n#include <stdint.h>\n#include "dex.h"\n\n')
        f.write('// GENERATED by tools/gen_forms.py -- do not edit.\n')
        f.write('// APPEND-ONLY: a row\'s index is persisted in the fork record.\n\n')
        f.write('enum : uint8_t { ' + ', '.join(KINDS) + ' };\n\n')
        f.write('struct FormEntry {\n'
                '  int16_t dex;        // the base species\n'
                '  uint8_t sub;        // SpriteCollab subfolder, sprite/NNNN/00SS\n'
                '  uint8_t kind;       // FK_*\n'
                '  const char *label;  // "MEGA X"; ASCII only\n'
                '  uint8_t bHp, bAtk, bDef, bSpe, bSpA, bSpD;\n'
                '  uint8_t type1, type2;\n'
                '  uint8_t art;        // 1: the sprite exists; 0: never spawn it\n'
                '  uint8_t shinyArt;   // 0: a shiny draws the plain form\n'
                '};\n\n')
        f.write(f'#define FORM_COUNT {len(order)}\n')
        f.write('// Row 0 is "no form", so 0 can mean that wherever a form id is stored.\n')
        f.write('static const FormEntry FORM_TBL[FORM_COUNT + 1] = {\n')
        f.write('  { 0, 0, FK_NONE, "", 0, 0, 0, 0, 0, 0, T_NORMAL, T_NONE, 0, 0 },\n')
        for i, k in enumerate(order, 1):
            r = rows[k]
            s = ', '.join(str(x) for x in r['stats'])
            f.write(f"  {{ {r['dex']}, {r['sub']}, {r['kind']}, \"{r['label']}\", {s}, "
                    f"{r['t1']}, {r['t2']}, {int(r['art'])}, {int(r['shiny'])} }},"
                    f"  // {i}: {r['var']}\n")
        f.write('};\n')
    print('wrote', os.path.relpath(OUT, ROOT))


if __name__ == '__main__':
    main()
