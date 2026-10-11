"""An Atari 7800 cartridge: the .a78 header, the ROM, and its mapper.

The CPU sees the cartridge at $4000-$FFFF.  How the ROM fills that space
is the mapper's business (MAME's a7800 cart slot is the reference):

    rom       plain ROM, ending at $FFFF (16 to 48 KB); nothing below it
    pokey     the same, with a POKEY at $4000-$7FFF (Ballblazer)
    mram      the same, with RAM at $4000-$7FFF, A8 not wired (Fractalus)
    sg        SuperGame: 16 KB banks; the bank written to $8000-$BFFF is
              seen at $8000, the last bank at $C000, the one before it at $4000
    sg_pokey  SuperGame with a POKEY at $4000 (Commando)
    sg_ram    SuperGame with 16 KB of RAM at $4000 (Impossible Mission...)
    sg9       SuperGame with 9 banks: bank 0 fixed at $4000, the written
              value + 1 at $8000, bank 8 at $C000 (Alien Brigade, Crossbow)
    abs       Absolute (F-18 Hornet): bank 0 or 1 at $4000 (bit 0 / bit 1
              written to $8000), the upper 32 KB fixed at $8000
    act       Activision (Double Dragon, Rampage): 16 KB bank n, chosen by
              A2-A0 of a write to $E000-$FFFF, at $A000-$DFFF; the rest in 8 KB
              pieces of the last two banks: 8K banks 13, 12 at $4000, $6000,
              15 at $8000, 14 at $E000 (the .a78 layout, as the a7800 emulator
              has it; MAME's EPROM dumps have each bank's halves swapped)

Every mapper has at most one window -- one CPU range whose bytes depend on
a bank register.  A location is printed 'F:addr' outside the window (the
same bytes whatever the bank) and 'B<n>:addr' inside it.

The header's cart type is often wrong in older dumps (Double Dragon and
Rampage claim SuperGame, F-18 Hornet nothing at all): MAME's software list
is right, and OVERRIDES carries its verdict for the ROMs whose header lies.

    python -m 7800kit.cart ROM.a78                 # header, mapper, vectors
    python -m 7800kit.cart "Atari 7800/*/*.a78" --table
"""

import argparse
import glob
import os
import struct
import sys
import zlib

BANK = 0x4000

# ROM CRC32 -> mapper, where the header disagrees with MAME's software list
# (a7800.xml) -- or names no mapper for a cart the list knows by title.
OVERRIDES = {
    0xED0A587D: 'sg',         # Barnyard Blaster (NTSC)         header: sg_pokey
    0xCD1A98C5: 'sg_pokey',   # Commando (NTSC)                 header: sg
    0xA1EAA7C1: 'rom',        # Diagnostic Test Cartridge       header: pokey
    0x63D1C7C1: 'abs',        # F-18 Hornet (NTSC)              header: rom
    0x3B1F2F47: 'sg_ram',     # Impossible Mission (NTSC)       header: sg
    0x65C6DF3F: 'sg_ram',     # Summer Games (NTSC)             header: sg
    0x3FBE74CE: 'pokey',      # Symbolic Debugger (Dev. Card)   header: mram
    0x8981B531: 'sg_ram',     # Winter Games (NTSC)             header: sg
    0x47340DF9: 'sg',         # Sentinel (PAL)                  header: sg_pokey
}
# The same, by title, for dumps of these carts the list does not carry.
TITLE_OVERRIDES = {
    'Double Dragon': 'act',
    'Rampage': 'act',
    'Impossible Mission': 'sg_ram',
}


def mapper_from_header(ct, size):
    """The mapper MAME derives from the header's cart type word."""
    if ct & 0xFF00 == 0x0100:
        return 'act'
    if ct & 0xFF00 == 0x0200:
        return 'abs'
    k = ct & 0x2E
    pokey = ct & 1
    if k == 0x00:
        return 'pokey' if pokey else 'rom'
    if k == 0x02:
        return 'sg_pokey' if pokey else 'sg'
    if k == 0x06:
        # SuperGame + RAM; Fractalus's small cart is RAM without banks
        return 'sg_ram' if size > 0x8000 else 'mram'
    if k == 0x0A:
        return 'sg9'
    return 'rom'


class Cart:
    def __init__(self, data, path=''):
        self.path = path
        if data[1:10] != b'ATARI7800':
            raise ValueError(f'{path}: no a78 header')
        self.header = bytes(data[:128])
        self.title = data[17:49].rstrip(b'\0').decode('latin1')
        size = struct.unpack('>I', data[49:53])[0]
        self.data = bytes(data[128:128 + size])   # overdumps carry a few bytes more
        self.cart_type = data[53] << 8 | data[54]
        self.controllers = (data[55], data[56])
        self.pal = data[57] & 1
        # the set's folders know better than some headers (Impossible Mission says PAL)
        parts = [p.lower() for p in os.path.normpath(path).split(os.sep)]
        if '(pal)' in parts[-1].lower() or 'palvideo' in parts:
            self.pal = 1
        elif 'ntscvideo' in parts:
            self.pal = 0
        self.crc = zlib.crc32(self.data)
        self.mapper = OVERRIDES.get(self.crc)
        if not self.mapper:
            for t, m in TITLE_OVERRIDES.items():
                if os.path.basename(path).startswith(t):
                    self.mapper = m
        if not self.mapper:
            self.mapper = mapper_from_header(self.cart_type, len(self.data))
        self.size = len(self.data)
        self.nbanks = max(1, self.size // BANK)
        n = self.nbanks
        self.mask = (n - 2) if n & 1 else (n - 1)   # MAME: 9 banks -> mask 7
        if self.mapper in ('sg', 'sg_pokey', 'sg_ram'):
            self.window = (0x8000, 0xC000)
            self.banks = list(range(self.mask + 1))
        elif self.mapper == 'sg9':
            self.window = (0x8000, 0xC000)
            self.banks = list(range(1, self.mask + 2))
        elif self.mapper == 'abs':
            self.window = (0x4000, 0x8000)
            self.banks = [0, 1]
        elif self.mapper == 'act':
            self.window = (0xA000, 0xE000)
            self.banks = list(range(8))
        else:
            self.window = None
            self.banks = []

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read(), path)

    @property
    def name(self):
        return os.path.splitext(os.path.basename(self.path))[0]

    def in_window(self, addr):
        return self.window is not None and self.window[0] <= addr < self.window[1]

    def offset(self, addr, bank=0):
        """ROM offset of a CPU address, or None (RAM, I/O, open bus)."""
        a, m, n = addr & 0xFFFF, self.mapper, self.size
        if a < 0x4000:
            return None
        if m in ('rom', 'pokey', 'mram'):
            if m != 'rom' and a < 0x8000:
                return None
            base = 0x10000 - n
            return a - base if a >= base else None
        if m in ('sg', 'sg_pokey', 'sg_ram'):
            if a < 0x8000:
                return None if m != 'sg' else (self.mask - 1) * BANK + (a & 0x3FFF)
            if a < 0xC000:
                return (bank & self.mask) * BANK + (a & 0x3FFF)
            return self.mask * BANK + (a & 0x3FFF)
        if m == 'sg9':
            if a < 0x8000:
                return a & 0x3FFF
            if a < 0xC000:
                return bank * BANK + (a & 0x3FFF)
            return (self.mask + 1) * BANK + (a & 0x3FFF)
        if m == 'abs':
            if a < 0x8000:
                return (bank & 1) * BANK + (a & 0x3FFF)
            return a
        if m == 'act':
            if 0xA000 <= a < 0xE000:
                return (bank & 7) * BANK + (a - 0xA000)
            k = {0x4000: 13, 0x6000: 12, 0x8000: 15, 0xE000: 14}[a & 0xE000]
            return k * 0x2000 + (a & 0x1FFF)
        return None

    def reader(self, bank=None):
        """read(addr) over the cartridge, with `bank` in the window."""
        d = self.data
        if bank is None:
            bank = self.banks[0] if self.banks else 0

        def read(addr):
            o = self.offset(addr & 0xFFFF, bank)
            return d[o] if o is not None and o < len(d) else 0
        return read

    def word(self, addr, bank=None):
        r = self.reader(bank)
        return r(addr) | r(addr + 1) << 8

    def vectors(self):
        return {'NMI': self.word(0xFFFA), 'RESET': self.word(0xFFFC),
                'IRQ': self.word(0xFFFE)}

    def where(self, addr, bank=None):
        if self.in_window(addr):
            return f'B{bank if bank is not None else "?"}:{addr:04X}'
        if self.offset(addr) is not None:
            return f'F:{addr:04X}'
        return f'--:{addr:04X}'

    def fixed_ranges(self):
        """CPU ranges of ROM outside the window."""
        out, start = [], None
        for a in range(0x4000, 0x10000, 0x100):
            fixed = self.offset(a) is not None and not self.in_window(a)
            if fixed and start is None:
                start = a
            if not fixed and start is not None:
                out.append((start, a))
                start = None
        if start is not None:
            out.append((start, 0x10000))
        return out


def main(argv=None):
    ap = argparse.ArgumentParser(prog='7800kit.cart')
    ap.add_argument('roms', nargs='+')
    ap.add_argument('--table', action='store_true')
    a = ap.parse_args(argv)
    paths = [p for g in a.roms for p in (sorted(glob.glob(g)) or [g])]
    for p in paths:
        c = Cart.load(p)
        v = c.vectors()
        hm = mapper_from_header(c.cart_type, c.size)
        fix = '' if hm == c.mapper else f' (header: {hm})'
        if a.table:
            print(f'{c.name[:46]:46} {c.size // 1024:3d}K {c.mapper:8} '
                  f'{"PAL " if c.pal else "NTSC"} R={v["RESET"]:04X} N={v["NMI"]:04X} '
                  f'I={v["IRQ"]:04X} ctl={c.controllers[0]},{c.controllers[1]}{fix}')
            continue
        print(c.name)
        print(f'  title  {c.title!r}  type {c.cart_type:04X}  crc {c.crc:08X}')
        print(f'  size   {c.size} ({c.nbanks} x 16K)  mapper {c.mapper}{fix}'
              f'  {"PAL" if c.pal else "NTSC"}  controllers {c.controllers}')
        if c.window:
            print(f'  window ${c.window[0]:04X}-${c.window[1] - 1:04X}  banks {c.banks}')
        print('  fixed  ' + ' '.join(f'${lo:04X}-${hi - 1:04X}' for lo, hi in c.fixed_ranges()))
        for k, x in v.items():
            print(f'  {k:6} ${x:04X}')


if __name__ == '__main__':
    sys.exit(main())
