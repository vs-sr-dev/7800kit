"""Build tracer/a78trace.exe: the runtime with no native code -- the
interpreter, the tracer (--trace DIR) and a plain emulator (--window).

    python -m 7800kit.tracer.build
"""
import os
import sys

from ..recomp import __main__ as recomp

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    out = os.path.join(os.path.dirname(HERE), 'build', 'tracer')
    os.makedirs(out, exist_ok=True)
    exe = recomp.build(out, [os.path.join(recomp.RUNTIME, 'none.cpp')], opt='-O2')
    for name in ('a78.exe', 'SDL2.dll'):
        src = os.path.join(out, name)
        if os.path.exists(src):
            dst = os.path.join(HERE, 'a78trace.exe' if name == 'a78.exe' else name)
            with open(src, 'rb') as f, open(dst, 'wb') as g:
                g.write(f.read())
    print(f'  {os.path.join(HERE, "a78trace.exe")}')
    return 0 if exe else 1


if __name__ == '__main__':
    sys.exit(main())
