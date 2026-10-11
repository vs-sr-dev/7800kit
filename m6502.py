"""The 7800's CPU: SALLY, an NMOS 6502 with a halt line for MARIA's DMA.

The full NMOS set, the undocumented opcodes included (LAX, SAX, DCP, ISC,
SLO, RLA, SRE, RRA, ANC, ALR, ARR, SBX, the unstable ANE/LXA/SHA/SHX/SHY/
TAS/LAS, the multi-byte NOPs and the JAMs that hang the CPU).  They are
marked `illegal`: Atari's guidelines forbade them, so a game reaching one
has either wandered into data or is telling us something.

    python -m 7800kit.m6502 ROM.a78 --vec                # NMI / RESET / IRQ
    python -m 7800kit.m6502 ROM.a78 --at E000 -n 40      # a listing
    python -m 7800kit.m6502 ROM.a78 --bank 3 --at 8000   # a page of the window
"""

import argparse
import sys

# Addressing modes: name -> operand byte count.
MODES = {
    'imp': 0, 'acc': 0, 'imm': 1, 'zp': 1, 'zpx': 1, 'zpy': 1,
    'abs': 2, 'absx': 2, 'absy': 2, 'ind': 2, 'indx': 1, 'indy': 1, 'rel': 1,
}

# The same text is in runtime/a78_cpu.cpp.
ROWS = """
00 BRK imm  01 ORA indx 02 JAM* imp 03 SLO* indx 04 NOP* zp  05 ORA zp   06 ASL zp   07 SLO* zp
08 PHP imp  09 ORA imm  0A ASL acc  0B ANC* imm  0C NOP* abs 0D ORA abs  0E ASL abs  0F SLO* abs
10 BPL rel  11 ORA indy 12 JAM* imp 13 SLO* indy 14 NOP* zpx 15 ORA zpx  16 ASL zpx  17 SLO* zpx
18 CLC imp  19 ORA absy 1A NOP* imp 1B SLO* absy 1C NOP* absx 1D ORA absx 1E ASL absx 1F SLO* absx
20 JSR abs  21 AND indx 22 JAM* imp 23 RLA* indx 24 BIT zp  25 AND zp   26 ROL zp   27 RLA* zp
28 PLP imp  29 AND imm  2A ROL acc  2B ANC* imm  2C BIT abs 2D AND abs  2E ROL abs  2F RLA* abs
30 BMI rel  31 AND indy 32 JAM* imp 33 RLA* indy 34 NOP* zpx 35 AND zpx  36 ROL zpx  37 RLA* zpx
38 SEC imp  39 AND absy 3A NOP* imp 3B RLA* absy 3C NOP* absx 3D AND absx 3E ROL absx 3F RLA* absx
40 RTI imp  41 EOR indx 42 JAM* imp 43 SRE* indx 44 NOP* zp  45 EOR zp   46 LSR zp   47 SRE* zp
48 PHA imp  49 EOR imm  4A LSR acc  4B ALR* imm  4C JMP abs 4D EOR abs  4E LSR abs  4F SRE* abs
50 BVC rel  51 EOR indy 52 JAM* imp 53 SRE* indy 54 NOP* zpx 55 EOR zpx  56 LSR zpx  57 SRE* zpx
58 CLI imp  59 EOR absy 5A NOP* imp 5B SRE* absy 5C NOP* absx 5D EOR absx 5E LSR absx 5F SRE* absx
60 RTS imp  61 ADC indx 62 JAM* imp 63 RRA* indx 64 NOP* zp  65 ADC zp   66 ROR zp   67 RRA* zp
68 PLA imp  69 ADC imm  6A ROR acc  6B ARR* imm  6C JMP ind 6D ADC abs  6E ROR abs  6F RRA* abs
70 BVS rel  71 ADC indy 72 JAM* imp 73 RRA* indy 74 NOP* zpx 75 ADC zpx  76 ROR zpx  77 RRA* zpx
78 SEI imp  79 ADC absy 7A NOP* imp 7B RRA* absy 7C NOP* absx 7D ADC absx 7E ROR absx 7F RRA* absx
80 NOP* imm 81 STA indx 82 NOP* imm 83 SAX* indx 84 STY zp  85 STA zp   86 STX zp   87 SAX* zp
88 DEY imp  89 NOP* imm 8A TXA imp  8B ANE* imm  8C STY abs 8D STA abs  8E STX abs  8F SAX* abs
90 BCC rel  91 STA indy 92 JAM* imp 93 SHA* indy 94 STY zpx 95 STA zpx  96 STX zpy  97 SAX* zpy
98 TYA imp  99 STA absy 9A TXS imp  9B TAS* absy 9C SHY* absx 9D STA absx 9E SHX* absy 9F SHA* absy
A0 LDY imm  A1 LDA indx A2 LDX imm  A3 LAX* indx A4 LDY zp  A5 LDA zp   A6 LDX zp   A7 LAX* zp
A8 TAY imp  A9 LDA imm  AA TAX imp  AB LXA* imm  AC LDY abs AD LDA abs  AE LDX abs  AF LAX* abs
B0 BCS rel  B1 LDA indy B2 JAM* imp B3 LAX* indy B4 LDY zpx B5 LDA zpx  B6 LDX zpy  B7 LAX* zpy
B8 CLV imp  B9 LDA absy BA TSX imp  BB LAS* absy BC LDY absx BD LDA absx BE LDX absy BF LAX* absy
C0 CPY imm  C1 CMP indx C2 NOP* imm C3 DCP* indx C4 CPY zp  C5 CMP zp   C6 DEC zp   C7 DCP* zp
C8 INY imp  C9 CMP imm  CA DEX imp  CB SBX* imm  CC CPY abs CD CMP abs  CE DEC abs  CF DCP* abs
D0 BNE rel  D1 CMP indy D2 JAM* imp D3 DCP* indy D4 NOP* zpx D5 CMP zpx  D6 DEC zpx  D7 DCP* zpx
D8 CLD imp  D9 CMP absy DA NOP* imp DB DCP* absy DC NOP* absx DD CMP absx DE DEC absx DF DCP* absx
E0 CPX imm  E1 SBC indx E2 NOP* imm E3 ISC* indx E4 CPX zp  E5 SBC zp   E6 INC zp   E7 ISC* zp
E8 INX imp  E9 SBC imm  EA NOP imp  EB SBC* imm  EC CPX abs ED SBC abs  EE INC abs  EF ISC* abs
F0 BEQ rel  F1 SBC indy F2 JAM* imp F3 ISC* indy F4 NOP* zpx F5 SBC zpx  F6 INC zpx  F7 ISC* zpx
F8 SED imp  F9 SBC absy FA NOP* imp FB ISC* absy FC NOP* absx FD SBC absx FE INC absx FF ISC* absx
"""

# NMOS cycles, without the page-crossing extra (+1 on indexed reads that
# cross a page, +1 on a taken branch, +1 more if it lands on another page).
CYC = [
    7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7]

# Instructions whose indexed read costs a cycle more across a page.
PAGE_PENALTY = {'ORA', 'AND', 'EOR', 'ADC', 'SBC', 'CMP', 'LDA', 'LDX', 'LDY',
                'LAX', 'LAS', 'NOP'}


class Op:
    __slots__ = ('code', 'mnem', 'mode', 'size', 'illegal', 'cyc')

    def __init__(self, code, mnem, mode):
        self.code = code
        self.illegal = mnem.endswith('*')
        self.mnem = mnem.rstrip('*')
        self.mode = mode
        self.size = 1 + MODES[mode]
        self.cyc = CYC[code]


def _table():
    t = [None] * 256
    toks = ROWS.split()
    for i in range(0, len(toks), 3):
        code = int(toks[i], 16)
        t[code] = Op(code, toks[i + 1], toks[i + 2])
    assert all(t), 'opcode table incomplete'
    return t


OPS = _table()

BRANCHES = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'}
ENDS = {'RTS', 'RTI', 'JMP', 'JAM', 'BRK'}   # no fall-through
STORES = {'STA', 'STX', 'STY', 'SAX', 'SHA', 'SHX', 'SHY', 'TAS'}
RMW = {'ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC', 'SLO', 'RLA', 'SRE', 'RRA', 'DCP', 'ISC'}


class Insn:
    __slots__ = ('pc', 'op', 'arg', 'raw')

    def __init__(self, pc, op, arg, raw):
        self.pc, self.op, self.arg, self.raw = pc, op, arg, raw

    @property
    def size(self):
        return self.op.size

    def target(self):
        """The static target of a branch, JSR or JMP abs, else None."""
        m = self.op.mode
        if m == 'rel':
            return (self.pc + 2 + _s8(self.arg)) & 0xFFFF
        if self.op.mnem in ('JSR', 'JMP') and m == 'abs':
            return self.arg
        return None

    def __str__(self):
        return f'{self.op.mnem:<4} {fmt_operand(self)}'.rstrip()


def _s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def decode(read, pc):
    """Decode one instruction; `read(addr)` returns a byte."""
    op = OPS[read(pc)]
    n = MODES[op.mode]
    raw = bytes(read((pc + i) & 0xFFFF) for i in range(op.size))
    arg = 0
    if n == 1:
        arg = raw[1]
    elif n == 2:
        arg = raw[1] | raw[2] << 8
    return Insn(pc, op, arg, raw)


# The 7800's registers, for listings.
NAMES = {
    0x01: 'INPTCTRL', 0x08: 'INPT0', 0x09: 'INPT1', 0x0A: 'INPT2', 0x0B: 'INPT3',
    0x0C: 'INPT4', 0x0D: 'INPT5', 0x15: 'AUDC0', 0x16: 'AUDC1', 0x17: 'AUDF0',
    0x18: 'AUDF1', 0x19: 'AUDV0', 0x1A: 'AUDV1',
    0x20: 'BACKGRND', 0x24: 'WSYNC', 0x28: 'MSTAT', 0x2C: 'DPPH', 0x30: 'DPPL',
    0x34: 'CHARBASE', 0x38: 'OFFSET', 0x3C: 'CTRL',
    0x280: 'SWCHA', 0x281: 'CTLSWA', 0x282: 'SWCHB', 0x283: 'CTLSWB',
    0x284: 'INTIM', 0x285: 'TIMINT', 0x294: 'TIM1T', 0x295: 'TIM8T', 0x296: 'TIM64T',
    0x297: 'T1024T',
}
for _p in range(8):
    for _c in range(1, 4):
        NAMES[0x20 + _p * 4 + _c] = f'P{_p}C{_c}'


def fmt_operand(ins, names=NAMES):
    m, a = ins.op.mode, ins.arg

    def nm(v, w=4):
        if names and v in names:
            return names[v]
        return f'${v:0{w}X}'
    if m == 'imp':
        return ''
    if m == 'acc':
        return 'A'
    if m == 'imm':
        return f'#${a:02X}'
    if m == 'zp':
        return nm(a, 2)
    if m == 'zpx':
        return nm(a, 2) + ',X'
    if m == 'zpy':
        return nm(a, 2) + ',Y'
    if m == 'abs':
        return nm(a)
    if m == 'absx':
        return nm(a) + ',X'
    if m == 'absy':
        return nm(a) + ',Y'
    if m == 'ind':
        return f'({nm(a)})'
    if m == 'indx':
        return f'({nm(a, 2)},X)'
    if m == 'indy':
        return f'({nm(a, 2)}),Y'
    if m == 'rel':
        return f'${ins.target():04X}'
    raise ValueError(m)


def main(argv=None):
    from . import cart
    ap = argparse.ArgumentParser(prog='7800kit.m6502')
    ap.add_argument('rom')
    ap.add_argument('--at', default='FFFC', help='CPU address (hex)')
    ap.add_argument('--bank', type=int, default=None, help='the bank seen in the window')
    ap.add_argument('-n', type=int, default=32)
    ap.add_argument('--vec', action='store_true', help='print the vectors')
    a = ap.parse_args(argv)
    c = cart.Cart.load(a.rom)
    read = c.reader(a.bank)
    if a.vec:
        for name, addr in c.vectors().items():
            print(f'{name:5} ${addr:04X}')
        return
    pc = int(a.at, 16)
    bank = a.bank if a.bank is not None else (c.banks[0] if c.banks else 0)
    for _ in range(a.n):
        ins = decode(read, pc)
        flag = '  ; illegal' if ins.op.illegal else ''
        print(f'{c.where(pc, bank)}  {ins.raw.hex(" "):<9} {ins}{flag}')
        pc = (pc + ins.size) & 0xFFFF


if __name__ == '__main__':
    sys.exit(main())
