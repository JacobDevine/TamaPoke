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

It also writes formthumbs.bin into the pack (step 12f): the party, box, sheet
and picker draw thumbnails, not PMD sprites, so a kept fusion needs one. Same
TPTH format and the same shrink as make_thumbs.py's thumbs.bin, but indexed by
FORM id (offset[0] = form 1), with an empty blob for a form with no art. It is
deliberately not rows added to thumbs.bin, whose index is the dex number and
which upstream regenerates.
"""
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MONS = os.path.join(HERE, 'sdcard', 'mons')
OUT = os.path.join(ROOT, 'android', 'packs', 'sprites-forms.pak')
THUMBS = 'formthumbs.bin'
GITHUB_LIMIT = 100 * 1024 * 1024

sys.path.insert(0, HERE)
from pack_bundle import parse_pak, write_pak  # noqa: E402
from make_thumbs import read_pmd_idle_frame0, shrink  # noqa: E402

FORM_ROW = (r'^\s*\{ (\d+), (\d+), FK_\w+, "[^"]*", (?:\d+, ){6}'
            r'T_\w+, T_\w+, (\d), (\d) \}')


def rows():
    """Every FORM_TBL row after row 0, in id order: (dex, sub, art, shinyArt)."""
    txt = open(os.path.join(ROOT, 'fork_forms.h')).read()
    count = int(re.search(r'#define FORM_COUNT (\d+)', txt).group(1))
    found = [tuple(int(v) for v in r) for r in re.findall(FORM_ROW, txt, re.M)]
    # The thumbnail index is positional, so a row the pattern missed would
    # shift every form after it onto its neighbour's picture.
    if len(found) != count + 1 or found[0][0] != 0:
        sys.exit(f'fork_forms.h: read {len(found)} rows, expected FORM_COUNT + 1 = {count + 1}')
    return found[1:]


def wanted():
    names = []
    for dex, sub, art, shiny in rows():
        if not art:
            continue
        names.append(f'f{dex:04d}_{sub:02d}.bin')
        if shiny:
            names.append(f'fs{dex:04d}_{sub:02d}.bin')
    return names


def write_thumbs():
    """formthumbs.bin beside the form sprites; returns its path."""
    blobs = []
    for dex, sub, art, _ in rows():
        if not art:
            blobs.append(struct.pack('<3B', 0, 0, 0))
            continue
        w, h, pal, data = read_pmd_idle_frame0(os.path.join(MONS, f'f{dex:04d}_{sub:02d}.bin'))
        nw, nh, npal, ndata = shrink(w, h, pal, data)
        if len(npal) > 255:
            sys.exit(f'form f{dex:04d}_{sub:02d}: palette of {len(npal)}')
        blobs.append(struct.pack('<3B', nw, nh, len(npal)) +
                     struct.pack(f'<{len(npal)}H', *npal) + ndata)
    pos = 4 + 2 + 4 * len(blobs)
    offsets = []
    for b in blobs:
        offsets.append(pos)
        pos += len(b)
    out = os.path.join(MONS, THUMBS)
    with open(out, 'wb') as f:
        f.write(b'TPTH')
        f.write(struct.pack('<H', len(blobs)))
        f.write(struct.pack(f'<{len(blobs)}I', *offsets))
        for b in blobs:
            f.write(b)
    return out


def unpack():
    files = parse_pak(open(OUT, 'rb').read())
    os.makedirs(MONS, exist_ok=True)
    for name, blob in files.items():
        with open(os.path.join(MONS, os.path.basename(name)), 'wb') as f:
            f.write(blob)
    print(f'{os.path.relpath(MONS, ROOT)}: {len(files)} form files restored')


def main():
    if sys.argv[1:] == ['--unpack']:
        return unpack()
    names = wanted()
    missing = [n for n in names if not os.path.exists(os.path.join(MONS, n))]
    if missing:
        sys.exit(f'{len(missing)} form sprites not packed yet (run pack_pmd.py forms): '
                 + ', '.join(missing[:8]))
    extra = sorted(set(os.path.basename(f) for f in glob.glob(os.path.join(MONS, 'f*.bin')))
                   - set(names) - {THUMBS})
    if extra:
        print('not in fork_forms.h, left out:', ', '.join(extra))
    thumbs = write_thumbs()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    total = write_pak(OUT, [os.path.join(MONS, n) for n in sorted(names)] + [thumbs])
    size = os.path.getsize(OUT)
    if size > GITHUB_LIMIT:
        sys.exit(f'{OUT} is {size / 1048576:.1f} MB, over GitHub\'s 100 MB file limit')
    print(f'{os.path.relpath(OUT, ROOT)}: {len(names)} sprites and {THUMBS}, '
          f'{total / 1048576:.1f} MB ({size / 1048576:.1f} MB total)')


if __name__ == '__main__':
    main()
