"""Every cartridge through the whole route: trace, recompile, build, lockstep.

    python -m 7800kit.recomp.all ROMDIR [--frames 6000] [--only NAME] [--jobs N]

For each ROM: tracer/a78trace (6000 frames of the monkey pad) unless its
trace is there, `recomp` to build/recomp/KEY, g++, then `a78.exe
--lockstep` -- the interpreter and the native code side by side, the
machine compared at every frame -- and one line of results.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys

from .. import cart as cartmod
from ..tracer.survey import key
from . import __main__ as recomp

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACER = os.path.join(ROOT, 'tracer', 'a78trace.exe')

# Dumps whose layout no mapper here describes (see README).
SKIP = ('CC2 Hack)', 'Klax (1992) (Atari) (Prototype) [a1]', 'Pit Fighter (1992) (Atari) (Prototype) [a1]',
        'Impossible Mission (1987) (Atari) [a1]', 'Symbolic Debugger')


def one(rom, frames, build_dir):
    k = key(rom)
    c = cartmod.Cart.load(rom)
    tdir = os.path.join(build_dir, 'trace', k)
    if not os.path.exists(os.path.join(tdir, 'report.txt')):
        os.makedirs(tdir, exist_ok=True)
        subprocess.run([TRACER, rom, '--trace', tdir, '--out', tdir, '--frames', str(frames),
                        '--mapper', c.mapper, '--pal' if c.pal else '--ntsc', '--quiet'], check=True)
    out = os.path.join(build_dir, 'recomp', k)
    logs = []
    try:
        files = recomp.generate(rom, tdir, out, log=logs.append)
        recomp.build(out, files, log=logs.append)
    except Exception as e:                       # noqa: BLE001 -- reported per game
        return k, f'BUILD FAILED: {str(e)[:300]}', logs
    r = subprocess.run([os.path.join(out, 'a78.exe'), os.path.abspath(rom), '--lockstep',
                        '--frames', str(frames), '--out', out, '--mapper', c.mapper,
                        '--pal' if c.pal else '--ntsc'],
                       capture_output=True, text=True, cwd=out)
    return k, (r.stdout + r.stderr).strip(), logs


def main(argv=None):
    ap = argparse.ArgumentParser(prog='7800kit.recomp.all')
    ap.add_argument('romdir')
    ap.add_argument('--frames', type=int, default=6000)
    ap.add_argument('--only')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--build-dir', default=os.path.join(ROOT, 'build'))
    a = ap.parse_args(argv)
    roms = sorted(glob.glob(os.path.join(a.romdir, '**', '*.a78'), recursive=True))
    roms = [r for r in roms if not any(s in r for s in SKIP)]
    if a.only:
        roms = [r for r in roms if a.only.lower() in r.lower()]
    ok = native = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for k, res, logs in ex.map(lambda r: one(r, a.frames, a.build_dir), roms):
            same = f'lockstep: identical for {a.frames} frames' in res
            ok += same
            m = re.search(r'interpreted insns (\d+).*?fallbacks (\d+)\s+unwinds (\d+)\s+max depth (\d+)', res)
            speed = re.search(r'\((\d+)x\)', res)
            if m and same and int(m.group(1)) == 0:
                native += 1
            info = (f'interp {int(m.group(1)):>9}  fallbacks {m.group(2):>5}  unwinds {m.group(3):>5}  '
                    f'depth {m.group(4):>3}  {speed.group(1) if speed else "?":>5}x') if m else ''
            print(f'{"OK  " if same else "FAIL"} {k:40} {info}', flush=True)
            if not same:
                for line in (res.splitlines() + logs)[:10]:
                    print('       ' + line)
    print(f'{ok}/{len(roms)} identical in lockstep, {native} without the interpreter')
    return 0 if ok == len(roms) else 1


if __name__ == '__main__':
    sys.exit(main())
