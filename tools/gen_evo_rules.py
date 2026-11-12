#!/usr/bin/env python3
"""Generate evo_rules.h: the Android fork's evolution rules.

    python3 tools/gen_evo_rules.py           # write evo_rules.h
    python3 tools/gen_evo_rules.py --check   # fail if evo_rules.h is stale

ANDROID FORK ONLY (android/FEATURES_PLAN.md, Phase 2). dex.h holds ONE
evolution per species, so Tyrogue only ever becomes Hitmonlee, Wurmple only
Silcoon, and Espeon is a coin flip. This table holds the branches dex.h cannot,
and the bond evolutions the games tie to friendship. The firmware reads it only
when forkFeatures() is on, so the board's evolutions stay upstream's exactly.

It is a SEPARATE FILE from dex.h on purpose: upstream regenerates dex.h, and a
table living inside it would conflict on every merge.

Where the rules come from, in the order the plan agreed:

1. The games' own condition, wherever it maps onto something the game tracks:
   friendship becomes BOND (with the time of day where the games use one), and
   Tyrogue's Attack-vs-Defence comparison reads the creature's training.
   Derived from PokeAPI's evolution chains, never typed by hand.
2. The NATURE (fork.h), where the games decide by something this game does not
   have: a hidden personality value, a trade, gender or the game version.
   Those mappings are a design choice, so they are written below by hand --
   and checked against PokeAPI, so a target that is not really an evolution of
   its source fails here instead of shipping.

Stone and held-item branches (Gloom, Poliwhirl, Applin, Charcadet, Scyther,
Clamperl) wait for Phase 5, when stones become items; until then those species
evolve as dex.h says. Regional-form branches (Meowth, Wooper, Yamask, Sneasel)
need forms the game does not have, and Shedinja is a second creature appearing,
not a branch -- all left alone.

The level is NOT in this table. A rule applies at the source species' own
evolveLevel from dex.h, plus care mistakes, exactly like any other evolution:
one threshold, asked of one place (CLAUDE.md, trap 1).

Needs tools/pokeapi_cache/evolution_chain_*.json (gitignored). If it is empty,
the chains are fetched with curl, ~540 small requests, once.
"""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.join(HERE, 'pokeapi_cache')
OUT = os.path.join(ROOT, 'evo_rules.h')
sys.path.insert(0, HERE)
from dex_data import DEX  # noqa: E402

DEX_COUNT = len(DEX)
NATURES = ['EASYGOING', 'GLUTTON', 'PLAYFUL', 'TIDY', 'LAZY']   # fork.h order

# The branches the games leave to chance, trade, gender or version. Each source
# lists every target with the natures that lead there; together they must cover
# all five natures exactly once, so every creature has exactly one destination.
# The pairings lean on what each form is like, so the card's hint reads sensibly:
NATURE_BRANCHES = {
    # Wurmple: the games use a hidden personality value, 50/50.
    265: [(266, ['PLAYFUL', 'TIDY']),                 # SILCOON -> the butterfly
          (268, ['EASYGOING', 'GLUTTON', 'LAZY'])],   # CASCOON -> the moth
    # Slowpoke: Slowking needs a trade holding a King's Rock.
    79:  [(80,  ['EASYGOING', 'GLUTTON', 'LAZY']),    # SLOWBRO
          (199, ['PLAYFUL', 'TIDY'])],                # SLOWKING, the clever one
    # Kirlia: Gallade needs a male and a Dawn Stone.
    281: [(282, ['EASYGOING', 'TIDY', 'LAZY']),       # GARDEVOIR
          (475, ['GLUTTON', 'PLAYFUL'])],             # GALLADE, the fighter
    # Snorunt: Froslass needs a female and a Dawn Stone.
    361: [(362, ['EASYGOING', 'GLUTTON', 'LAZY']),    # GLALIE, all mouth
          (478, ['PLAYFUL', 'TIDY'])],                # FROSLASS
    # Burmy: by gender.
    412: [(413, ['EASYGOING', 'TIDY', 'LAZY']),       # WORMADAM keeps its cloak
          (414, ['GLUTTON', 'PLAYFUL'])],             # MOTHIM, after honey
    # Cosmoem: by game version.
    790: [(791, ['EASYGOING', 'GLUTTON', 'PLAYFUL']), # SOLGALEO
          (792, ['TIDY', 'LAZY'])],                   # LUNALA
    # Eevee's stone and location forms. Stones arrive in Phase 5; until then the
    # nature picks one, and an Eevee with enough bond goes to its bond forms
    # first (listed before these, see below).
    133: [(134, ['TIDY']),                            # VAPOREON
          (135, ['PLAYFUL']),                         # JOLTEON
          (136, ['GLUTTON']),                         # FLAREON
          (470, ['EASYGOING']),                       # LEAFEON
          (471, ['LAZY'])],                           # GLACEON
}

# Conditions the games use that this game cannot track, replaced by one it can.
# Sylveon needs a known Fairy move, and Eevee can learn none of the moves in
# this game's move table, so it would be unreachable. The games also ask for
# affection, which is more than friendship: HIGH bond stands in for both.
OVERRIDE = {
    (133, 700): 'EVC_BOND_HIGH',
}

# Friendship evolutions that also need an item or a form are not plain bond.
BOND_KEYS = {'trigger', 'min_happiness', 'time_of_day', 'is_default',
             'version_group', 'required_pokemon_form'}


def num(url):
    return int(url.rstrip('/').split('/')[-1])


def fetch_chains():
    if glob.glob(os.path.join(CACHE, 'evolution_chain_*.json')):
        return
    os.makedirs(CACHE, exist_ok=True)
    r = subprocess.run(['curl', '-fsSL',
                        'https://pokeapi.co/api/v2/evolution-chain/?limit=2000'],
                       capture_output=True, check=True)
    for res in json.loads(r.stdout)['results']:
        n = num(res['url'])
        path = os.path.join(CACHE, 'evolution_chain_%d.json' % n)
        subprocess.run(['curl', '-fsSL', res['url'], '-o', path], check=True)


def load_edges():
    """{from: [(to, [details])]} for every evolution in PokeAPI."""
    edges = {}

    def walk(node):
        s = num(node['species']['url'])
        for nxt in node['evolves_to']:
            d = num(nxt['species']['url'])
            edges.setdefault(s, []).append((d, nxt['evolution_details']))
            walk(nxt)

    files = glob.glob(os.path.join(CACHE, 'evolution_chain_*.json'))
    if len(files) < 500:
        sys.exit('only %d evolution chains cached; expected ~540' % len(files))
    for f in files:
        walk(json.load(open(f))['chain'])
    return edges


def base_detail(details, src_slug):
    """The detail for the species' ordinary form: no regional form required."""
    for d in details:
        form = (d.get('required_pokemon_form') or {}).get('name')
        if form in (None, src_slug):
            return d
    return None


def clean(d):
    return {k: v for k, v in d.items() if v not in (None, False, '', 0, [])}


def build():
    edges = load_edges()
    slug = {i + 1: row[1] for i, row in enumerate(DEX)}
    name = {i + 1: row[2] for i, row in enumerate(DEX)}
    table_to = {i + 1: row[4] for i, row in enumerate(DEX)}
    table_lv = {i + 1: row[5] for i, row in enumerate(DEX)}
    rules = []   # (from, to, cond, arg, comment)

    def need_table_level(src):
        if not table_to[src] or not table_lv[src]:
            sys.exit('%s (%d) has a rule but evolves to nothing in dex_data.py; '
                     'the rule needs its evolveLevel' % (name[src], src))

    # 1. bond, and Tyrogue's stats, straight from the games
    for src in sorted(edges):
        if src > DEX_COUNT:
            continue
        outs = [(d, det) for d, det in edges[src] if d <= DEX_COUNT]
        stats = any(base_detail(det, slug[src]) and
                    base_detail(det, slug[src]).get('relative_physical_stats') is not None
                    for _, det in outs)
        for dst, det in outs:
            b = base_detail(det, slug[src])
            if b is None:
                continue
            c = clean(b)
            if (src, dst) in OVERRIDE:
                need_table_level(src)
                rules.append((src, dst, OVERRIDE[(src, dst)], 0, 'override'))
                continue
            if stats:
                rel = b.get('relative_physical_stats')
                cond = {1: 'EVC_ATK_GT_DEF', -1: 'EVC_DEF_GT_ATK'}.get(rel, 'EVC_ATK_EQ_DEF')
                need_table_level(src)
                rules.append((src, dst, cond, 0, 'games: Attack vs Defence'))
                continue
            if c.get('min_happiness'):
                extra = set(c) - BOND_KEYS
                if extra:
                    sys.exit('%s -> %s: friendship plus %s, which this game cannot '
                             'track. Add it to OVERRIDE or leave it out on purpose.'
                             % (name[src], name[dst], sorted(extra)))
                need_table_level(src)
                tod = c.get('time_of_day')
                cond = {'day': 'EVC_BOND_DAY', 'night': 'EVC_BOND_NIGHT'}.get(tod, 'EVC_BOND')
                rules.append((src, dst, cond, 0, 'games: friendship'))

    # 2. the nature, where the games use something this game has not got
    for src, branches in NATURE_BRANCHES.items():
        need_table_level(src)
        real = {d for d, _ in edges.get(src, [])}
        seen = []
        for dst, nats in branches:
            if dst not in real:
                sys.exit('%s -> %s is not an evolution in PokeAPI' % (name[src], name[dst]))
            seen += nats
            mask = 0
            for n in nats:
                mask |= 1 << NATURES.index(n)
            rules.append((src, dst, 'EVC_NATURE', mask, '/'.join(nats)))
        if sorted(seen) != sorted(NATURES):
            sys.exit('%s: the natures must cover all five exactly once, got %s'
                     % (name[src], seen))

    # Within a species: high bond, then bond by time, then plain bond, then the
    # stats, then the nature. The first rule whose condition holds is the one
    # taken, so the nature -- which always holds for exactly one branch -- is the
    # floor every creature with a branch can count on.
    order = ['EVC_BOND_HIGH', 'EVC_BOND_DAY', 'EVC_BOND_NIGHT', 'EVC_BOND',
             'EVC_ATK_GT_DEF', 'EVC_DEF_GT_ATK', 'EVC_ATK_EQ_DEF', 'EVC_NATURE']
    rules.sort(key=lambda r: (r[0], order.index(r[2]), r[1]))

    # A rule set where the table's own target is not offered would hide what
    # upstream already does. Every ruled source keeps its dex.h target.
    for src in sorted({r[0] for r in rules}):
        if table_to[src] not in {r[1] for r in rules if r[0] == src}:
            sys.exit('%s: the rules leave out its dex.h target %s'
                     % (name[src], name[table_to[src]]))
    return rules, name


def render(rules, name):
    o = ['#pragma once',
         '#include <stdint.h>',
         '',
         '// GENERATED by tools/gen_evo_rules.py from PokeAPI\'s evolution chains',
         '// -- do not edit. The ANDROID FORK\'s evolution rules: read only when',
         '// forkFeatures() is on (fork.h, section 5). The level is the source',
         '// species\' own evolveLevel in dex.h; these add the condition that picks',
         '// the branch. Within a source the rules are in priority order: the first',
         '// whose condition holds is the one taken.',
         '',
         'enum : uint8_t {',
         '  EVC_BOND = 0,     // bond >= FORK_EVO_BOND',
         '  EVC_BOND_DAY,     // ...and the clock reads day',
         '  EVC_BOND_NIGHT,   // ...and the clock reads night',
         '  EVC_BOND_HIGH,    // bond >= FORK_EVO_BOND_HIGH',
         '  EVC_ATK_GT_DEF,   // Attack training above Defence',
         '  EVC_DEF_GT_ATK,   // Defence training above Attack',
         '  EVC_ATK_EQ_DEF,   // the two equal',
         '  EVC_NATURE,       // arg: bitmask of natures (1 << NATURE_*)',
         '};',
         '',
         'struct EvoRule { uint16_t from, to; uint8_t cond, arg; };',
         '',
         '#define EVO_RULE_COUNT %d' % len(rules),
         'static const EvoRule EVO_RULES[EVO_RULE_COUNT] = {']
    for src, dst, cond, arg, why in rules:
        o.append('  { %4d, %4d, %-15s %2d },  // %s -> %s (%s)'
                 % (src, dst, cond + ',', arg, name[src], name[dst], why))
    o += ['};', '']
    return '\n'.join(o)


def main():
    fetch_chains()
    rules, name = build()
    text = render(rules, name)
    if '--check' in sys.argv:
        cur = open(OUT).read() if os.path.exists(OUT) else ''
        if cur != text:
            sys.exit('evo_rules.h is stale: run python3 tools/gen_evo_rules.py')
        print('evo_rules.h is current (%d rules)' % len(rules))
        return
    open(OUT, 'w').write(text)
    print('wrote evo_rules.h: %d rules over %d species'
          % (len(rules), len({r[0] for r in rules})))


if __name__ == '__main__':
    main()
