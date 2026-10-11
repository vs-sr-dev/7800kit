"""Trace every cartridge and tabulate what a recompiler faces.

    python -m 7800kit.tracer.survey "7800kit/Atari 7800" [--frames 6000] [--jobs 8]
                                    [--only NAME] [--snap 300,1500,3000,5999]

Each ROM runs headless in tracer/a78trace.exe (the interpreter, with the
monkey pad) to build/trace/KEY: report.txt, cov.bin, PPM frames and a
contact sheet.  The mapper comes from 7800kit/cart.py.  One line per game.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys

from .. import cart as cartmod

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACER = os.path.join(ROOT, 'tracer', 'a78trace.exe')


def key(path):
    d = os.path.basename(os.path.dirname(path))
    n = os.path.splitext(os.path.basename(path))[0]
    n = re.sub(r'\s*\((Atari|Activision|Absolute|Froggo|Atari-Lucasfilm)\)', '', n)
    n = re.sub(r'[^A-Za-z0-9]+', '_', n).strip('_')
    return ('P_' if d.startswith('PAL') else 'O_' if d.startswith('Over') else '') + n


def parse(rep):
    r = {}
    for line in rep.splitlines():
        if line.startswith('edge '):
            k = line.split()[1]
            r['edges_' + k] = r.get('edges_' + k, 0) + 1
            continue
        toks = line.split()
        if not toks:
            continue
        if toks[0] == 'bank':
            r.setdefault('banks', []).append(dict(zip(toks[2::2], map(int, toks[3::2]))))
            continue
        for k, v in zip(toks[::2], toks[1::2]):
            r[k] = v
        if toks[0] in ('illegal_ops', 'io_w', 'io_r', 'riot_w', 'riot_r', 'pokey_w', 'pokey_r', 'nmi_hits'):
            r[toks[0]] = ' '.join(toks[1:])
    return r


def one(rom, frames, snaps, build):
    c = cartmod.Cart.load(rom)
    k = key(rom)
    out = os.path.join(build, 'trace', k)
    os.makedirs(out, exist_ok=True)
    cmd = [TRACER, rom, '--trace', out, '--out', out, '--frames', str(frames),
           '--mapper', c.mapper, '--pal' if c.pal else '--ntsc', '--quiet', '--snap', snaps]
    p = subprocess.run(cmd, capture_output=True, text=True)
    rep = open(os.path.join(out, 'report.txt')).read() if os.path.exists(os.path.join(out, 'report.txt')) else ''
    shots = [os.path.join(out, f'f{int(s):05d}.ppm') for s in snaps.split(',')]
    shots = [s for s in shots if os.path.exists(s)]
    if shots:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'ppm2png.py'),
                        os.path.join(out, 'sheet.png'), *shots, '--cols', '2'])
    return k, c, parse(rep), p.stderr.strip()


def main(argv=None):
    ap = argparse.ArgumentParser(prog='7800kit.tracer.survey')
    ap.add_argument('romdir')
    ap.add_argument('--frames', type=int, default=6000)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--only')
    ap.add_argument('--snap', default='300,1500,3000,5999')
    ap.add_argument('--build-dir', default=os.path.join(ROOT, 'build'))
    a = ap.parse_args(argv)
    roms = sorted(glob.glob(os.path.join(a.romdir, '**', '*.a78'), recursive=True))
    if a.only:
        roms = [r for r in roms if a.only.lower() in r.lower()]
    print(f'{"game":40} {"map":8} {"KB":>3} {"code":>5} {"gfx":>6} {"ind":>3} {"crts":>4} {"drop":>4} '
          f'{"ill":>4} {"bsw":>5} {"bwin":>4} {"ram":>4} {"smc":>4} {"dli/f":>5} {"wsync/f":>7} {"brk":>3} jam')
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for k, c, r, err in ex.map(lambda p: one(p, a.frames, a.snap, a.build_dir), roms):
            if not r:
                print(f'{k[:40]:40} FAILED {err[:100]}')
                continue
            banks = r.get('banks', [])
            code = sum(b['code_bytes'] for b in banks)
            gfx = sum(b['gfx_dma'] for b in banks)
            fr = max(1, int(r.get('frames', 1)))
            print(f'{k[:40]:40} {c.mapper:8} {c.size // 1024:3d} {code:5d} {gfx:6d} '
                  f'{r.get("edges_i", 0):3d} {r.get("computed_rts", "?"):>4} {r.get("dropped_returns", "?"):>4} '
                  f'{r.get("illegal", "?"):>4} {r.get("bank_changes", "?"):>5} {r.get("bank_change_from_window", "?"):>4} '
                  f'{r.get("ramexec_bytes", "?"):>4} {r.get("smc_writes", "?"):>4} '
                  f'{int(r.get("nmi", 0)) / fr:5.1f} {int(r.get("wsync", 0)) / fr:7.1f} {r.get("brk", "?"):>3} '
                  f'{r.get("jammed", "?")}' + (f'  {err[:80]}' if err else ''), flush=True)


if __name__ == '__main__':
    sys.exit(main())
