"""Play a game, recompiled: find it by name, build it if needed, open a window.

    python -m 7800kit.play "ms. pac"            # NTSC first, then PAL
    python -m 7800kit.play "xevious" --pal --scale 4
    python -m 7800kit.play "joust" --interp     # the interpreter instead

Keys: arrows, Z / X = the two buttons, Enter = Reset, Shift = Select,
P = Pause, F1 / F2 = the difficulty switches, Esc quits.
"""
import argparse
import glob
import os
import subprocess
import sys

from . import cart as cartmod
from .recomp import __main__ as recomp
from .tracer.survey import key

ROOT = os.path.dirname(os.path.abspath(__file__))
TRACER = os.path.join(ROOT, 'tracer', 'a78trace.exe')


def main(argv=None):
    ap = argparse.ArgumentParser(prog='7800kit.play')
    ap.add_argument('name')
    ap.add_argument('--pal', action='store_true', help='prefer the PAL dump')
    ap.add_argument('--interp', action='store_true')
    ap.add_argument('--scale', default='3')
    ap.add_argument('--romdir', default=os.path.join(ROOT, 'Atari 7800'))
    a = ap.parse_args(argv)
    roms = sorted(glob.glob(os.path.join(a.romdir, '**', '*.a78'), recursive=True))
    hits = [r for r in roms if a.name.lower() in os.path.basename(r).lower()]
    hits.sort(key=lambda r: ('PALVideo' in r) != a.pal or 'Overdumps' in r)
    if not hits:
        print(f'no ROM matches {a.name!r}')
        return 1
    rom = hits[0]
    c = cartmod.Cart.load(rom)
    k = key(rom)
    flags = ['--mapper', c.mapper, '--pal' if c.pal else '--ntsc', '--window', '--scale', a.scale]
    print(f'{os.path.basename(rom)}  ({c.mapper}, {"PAL" if c.pal else "NTSC"})')
    if a.interp:
        return subprocess.run([TRACER, rom, *flags]).returncode
    out = os.path.join(ROOT, 'build', 'recomp', k)
    exe = os.path.join(out, 'a78.exe')
    if not os.path.exists(exe):
        tdir = os.path.join(ROOT, 'build', 'trace', k)
        if not os.path.exists(os.path.join(tdir, 'report.txt')):
            os.makedirs(tdir, exist_ok=True)
            print('  tracing...')
            subprocess.run([TRACER, rom, '--trace', tdir, '--out', tdir, '--frames', '6000',
                            '--mapper', c.mapper, '--pal' if c.pal else '--ntsc', '--quiet'], check=True)
        print('  recompiling...')
        recomp.build(out, recomp.generate(rom, tdir, out))
    return subprocess.run([exe, rom, *flags], cwd=out).returncode


if __name__ == '__main__':
    sys.exit(main())
