#!/usr/bin/env python3
"""Bundle the adventure form sprites into android/packs/sprites-forms.pak.

    python3 tools/gen_forms.py          # which forms exist and have art
    python3 tools/pack_pmd.py forms     # tools/sdcard/mons/f[s]NNNN_SS.bin
    python3 tools/pack_forms.py         # this: the bundle the APK carries
    python3 tools/pack_forms.py --unpack  # the reverse, for a fresh checkout

--unpack is unpack_bundle.py's job for this pack: the loose f*.bin are
gitignored like every other sprite, so a fresh checkout has only the bundle,
and formsprite_test needs the files.

The Android fork only (android/FEATURES_PLAN.md, Phase 12). It lives under
android/, not web/, because the board's installer has no use for it and
web/paks.json describes regions, which forms are not. Same TPAK format and the
same writer as the region packs, so the app indexes it the same way.

Every drawable row in fork_forms.h must have its file, and the pack carries
nothing else: a missing sprite is a hard failure here, not a creature that
draws as its base species on the phone and goes unnoticed.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MONS = os.path.join(HERE, 'sdcard', 'mons')
OUT = os.path.join(ROOT, 'android', 'packs', 'sprites-forms.pak')
GITHUB_LIMIT = 100 * 1024 * 1024

sys.path.insert(0, HERE)
from pack_bundle import parse_pak, write_pak  # noqa: E402


def wanted():
    txt = open(os.path.join(ROOT, 'fork_forms.h')).read()
    rows = re.findall(r'^\s*\{ (\d+), (\d+), FK_\w+, "[^"]*", (?:\d+, ){6}'
                      r'T_\w+, T_\w+, (\d), (\d) \}', txt, re.M)
    names = []
    for dex, sub, art, shiny in rows:
        dex, sub = int(dex), int(sub)
        if not dex or art != '1':
            continue
        names.append(f'f{dex:04d}_{sub:02d}.bin')
        if shiny == '1':
            names.append(f'fs{dex:04d}_{sub:02d}.bin')
    return names


def unpack():
    files = parse_pak(open(OUT, 'rb').read())
    os.makedirs(MONS, exist_ok=True)
    for name, blob in files.items():
        with open(os.path.join(MONS, os.path.basename(name)), 'wb') as f:
            f.write(blob)
    print(f'{os.path.relpath(MONS, ROOT)}: {len(files)} form sprites restored')


def main():
    if sys.argv[1:] == ['--unpack']:
        return unpack()
    names = wanted()
    missing = [n for n in names if not os.path.exists(os.path.join(MONS, n))]
    if missing:
        sys.exit(f'{len(missing)} form sprites not packed yet (run pack_pmd.py forms): '
                 + ', '.join(missing[:8]))
    extra = sorted(set(os.path.basename(f) for f in glob.glob(os.path.join(MONS, 'f*.bin')))
                   - set(names))
    if extra:
        print('not in fork_forms.h, left out:', ', '.join(extra))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    total = write_pak(OUT, [os.path.join(MONS, n) for n in sorted(names)])
    size = os.path.getsize(OUT)
    if size > GITHUB_LIMIT:
        sys.exit(f'{OUT} is {size / 1048576:.1f} MB, over GitHub\'s 100 MB file limit')
    print(f'{os.path.relpath(OUT, ROOT)}: {len(names)} sprites, '
          f'{total / 1048576:.1f} MB ({size / 1048576:.1f} MB total)')


if __name__ == '__main__':
    main()
