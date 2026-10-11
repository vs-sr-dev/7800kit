"""NMOS 6502 -> C++, against 7800kit/runtime/a78.h.

One region becomes one C++ function over `A78& c` and the address it is
entered at, with the interpreter's semantics instruction for instruction
(`a78_core.cpp`'s `step`): the lockstep self-test holds the two together
frame by frame.

* every instruction under a label `L_xxxx`, in address order, with a
  `goto` where control goes on to an instruction that is not the next one
  emitted (overlapping instructions, code shared from elsewhere); a switch
  at the top sends each entry to its label;
* before every instruction, A78_POLL: the machine's events due by then
  (MARIA's DMA halts the CPU, its display-list interrupt is an NMI) are
  handled at that boundary, as the interpreter handles them at its own;
* each instruction charges its ticks -- the NMOS cycles, the page-crossing
  cycle of an indexed read, a taken branch's cycle(s) -- before it runs;
* JSR pushes the return address and calls the callee -- the region's own
  function when the callee is in a region, else through `a78_call` (the
  bank chosen at run time; the interpreter for what is not native) -- and
  checks that it came back (`c.resume`, and S as it was): otherwise the
  function returns and its callers unwind;
* RTS and RTI leave the address they popped in `c.resume` and return; an
  RTS the tracer saw used as a jump, popping what this invocation pushed,
  goes on to its target instead;
* a JMP (abs) is a `switch` over the targets the tracer saw, `a78_call`
  for the rest (a continuation: the code there returns for us);
* in a region of the window, the code checks after a write that may have
  switched banks, a call or an event that its bank is still selected, and
  leaves the rest to `a78_call` if not.
"""
from .. import m6502
from .discover import FIXED, rname

COND = {'BPL': '!c.fn', 'BMI': 'c.fn', 'BVC': '!c.fv', 'BVS': 'c.fv',
        'BCC': '!c.fc', 'BCS': 'c.fc', 'BNE': '!c.fz', 'BEQ': 'c.fz'}
REG = {'A': 'c.a', 'X': 'c.x', 'Y': 'c.y'}
T = 4                                    # ticks per CPU cycle


def h(v, w=4):
    return f'0x{v:0{w}X}'


def ram_index(a):
    """Where a static address lives in c.ram, or None."""
    if 0x40 <= a < 0x100 or 0x140 <= a < 0x200:
        return 0x800 + a
    if 0x1800 <= a < 0x2800:
        return a - 0x1800
    return None


class Operand:
    """How an instruction reaches its operand: `pre` (statements that fix
    the address and charge a page crossing), `rd` (an expression),
    `wr(x)` (a statement), `bus` (a write that may switch banks)."""

    def __init__(self, ins, penalty):
        m, a = ins.op.mode, ins.arg
        self.pre = ''
        self.bus = False
        if m == 'imm':
            self.rd = h(a, 2)
            return
        if m == 'zp':
            if a >= 0x40:
                self._lv(f'c.ram[{h(0x800 + a)}]')
            else:
                self.rd = f'a78_io_r(c, {h(a, 2)})'
                self.wr = lambda x: f'a78_io_w(c, {h(a, 2)}, {x});'
            return
        if m in ('zpx', 'zpy'):
            r = 'c.x' if m == 'zpx' else 'c.y'
            self.pre = f'const uint8_t z = (uint8_t)({h(a, 2)} + {r}); '
            self.rd = 'a78_zrd(c, z)'
            self.wr = lambda x: f'a78_zwr(c, z, {x});'
            return
        if m == 'abs':
            ri = ram_index(a)
            if ri is not None:
                self._lv(f'c.ram[{h(ri)}]')
            elif a < 0x800:
                self.rd = f'a78_io_r(c, {h(a)})'
                self.wr = lambda x: f'a78_io_w(c, {h(a)}, {x});'
            else:
                self.rd = f'a78_rd(c, {h(a)})'
                self.wr = lambda x: f'a78_wr(c, {h(a)}, {x});'
                self.bus = a >= 0x4000
            return
        if m in ('absx', 'absy'):
            r = 'c.x' if m == 'absx' else 'c.y'
            pen = f'c.cyc += ((({h(a & 0xFF, 2)} + {r}) >> 8) << 2); ' if penalty else ''
            if 0x1800 <= a and a + 0xFF < 0x2800:
                self.pre = pen
                self._lv(f'c.ram[{h(a - 0x1800)} + {r}]')
            else:
                self._ea(pen + f'const uint16_t ea = (uint16_t)({h(a)} + {r}); ')
            return
        if m == 'indy':
            pen = 'c.cyc += (((p & 0xFF) + c.y) >> 8) << 2; ' if penalty else ''
            self._ea(f'const uint16_t p = a78_zpw(c, {h(a, 2)}); {pen}'
                     f'const uint16_t ea = (uint16_t)(p + c.y); ')
            return
        if m == 'indx':
            self._ea(f'const uint16_t ea = a78_zpw(c, (uint8_t)({h(a, 2)} + c.x)); ')
            return
        raise ValueError(m)

    def _lv(self, lv):
        self.rd = lv
        self.wr = lambda x: f'{lv} = {x};'

    def _ea(self, pre):
        self.pre += pre
        self.rd = 'a78_rd(c, ea)'
        self.wr = lambda x: f'a78_wr(c, ea, {x});'
        self.bus = True


def stmt(ins):
    """C++ for an instruction that does not transfer control: (text, bus write)."""
    m, mode = ins.op.mnem, ins.op.mode
    pen = m in m6502.PAGE_PENALTY
    if m in ('LDA', 'LDX', 'LDY'):
        o = Operand(ins, pen)
        return f'{{ {o.pre}a78_nz(c, {REG[m[2]]} = {o.rd}); }}', False
    if m in ('STA', 'STX', 'STY'):
        o = Operand(ins, False)
        return f'{{ {o.pre}{o.wr(REG[m[2]])} }}', o.bus
    if m in ('ADC', 'SBC'):
        o = Operand(ins, pen)
        return f'{{ {o.pre}a78_{m.lower()}(c, {o.rd}); }}', False
    if m in ('AND', 'ORA', 'EOR'):
        o = Operand(ins, pen)
        cop = {'AND': '&=', 'ORA': '|=', 'EOR': '^='}[m]
        return f'{{ {o.pre}a78_nz(c, c.a {cop} {o.rd}); }}', False
    if m in ('CMP', 'CPX', 'CPY'):
        o = Operand(ins, pen)
        r = {'CMP': 'c.a', 'CPX': 'c.x', 'CPY': 'c.y'}[m]
        return f'{{ {o.pre}a78_cmp(c, {r}, {o.rd}); }}', False
    if m == 'BIT':
        o = Operand(ins, False)
        return f'{{ {o.pre}a78_bit(c, {o.rd}); }}', False
    if m in ('ASL', 'LSR', 'ROL', 'ROR'):
        fn = 'a78_' + m.lower()
        if mode == 'acc':
            return f'c.a = {fn}(c, c.a);', False
        o = Operand(ins, False)
        return f'{{ {o.pre}uint8_t v = {o.rd}; {o.wr(f"{fn}(c, v)")} }}', o.bus
    if m in ('INC', 'DEC'):
        d = '+' if m == 'INC' else '-'
        o = Operand(ins, False)
        return f'{{ {o.pre}uint8_t v = {o.rd}; {o.wr(f"a78_nz(c, (uint8_t)(v {d} 1))")} }}', o.bus
    simple = {
        'PHA': 'a78_push(c, c.a);', 'PHP': 'a78_push(c, a78_p(c) | 0x10);',
        'PLA': 'a78_nz(c, c.a = a78_pull(c));', 'PLP': 'a78_setp(c, a78_pull(c)); a78_recalc(c);',
        'TAX': 'a78_nz(c, c.x = c.a);', 'TAY': 'a78_nz(c, c.y = c.a);',
        'TXA': 'a78_nz(c, c.a = c.x);', 'TYA': 'a78_nz(c, c.a = c.y);',
        'TSX': 'a78_nz(c, c.x = c.s);', 'TXS': 'c.s = c.x;',
        'INX': 'a78_nz(c, ++c.x);', 'INY': 'a78_nz(c, ++c.y);',
        'DEX': 'a78_nz(c, --c.x);', 'DEY': 'a78_nz(c, --c.y);',
        'CLC': 'c.fc = 0;', 'SEC': 'c.fc = 1;', 'CLI': 'c.fi = 0;',
        'SEI': 'c.fi = 1;', 'CLD': 'c.fd = 0;', 'SED': 'c.fd = 1;', 'CLV': 'c.fv = 0;',
        'NOP': ';',
    }
    if m in simple:
        return simple[m], False
    raise ValueError(f'{ins.pc:04X}: {ins}')


class Body:
    """One region as C++ (a list of lines)."""

    def __init__(self, prog, r, comments=True):
        self.p, self.r, self.comments = prog, r, comments
        self.win = r.slot != FIXED
        self.bank = r.slot
        self.sites = {}

    def _count(self, k):
        self.sites[k] = self.sites.get(k, 0) + 1

    def _bankcheck(self, at):
        """After something that may have switched the bank: leave if it did."""
        if not self.win:
            return ''
        return f' if (c.bank != {self.bank}) {{ a78_call(c, {h(at)}); return; }}'

    def _bankcheck_t(self):
        if not self.win:
            return ''
        return f' if (c.bank != {self.bank}) {{ a78_call(c, t); return; }}'

    def _switch(self, targets):
        """Go on at t: a goto for the traced targets here, a78_call for the rest."""
        cases = ' '.join(f'case {h(x)}: goto L_{x:04X};' for s, x in targets
                         if self._local(x) and (s == self.r.slot or s == FIXED))
        return f'switch (t) {{ {cases} default: a78_call(c, t); return; }}'

    def _local(self, t):
        return t in self.r.code and self.p.slot_of(t, self.r.slot) == self.r.slot

    def _goto(self, t):
        if self._local(t):
            return f'goto L_{t:04X};'
        self._count('dynamic jump')
        return f'a78_call(c, {h(t)}); return;'

    def _callee(self, t):
        ts = self.p.slot_of(t, self.r.slot)
        if ts is not None and ts in self.p.regions and t in self.p.regions[ts].entries:
            return f'{rname(ts)}(c, {h(t)});'
        self._count('dynamic call')
        return f'a78_call(c, {h(t)});'

    def insn(self, a):
        """(C++ text, whether control goes on to a + size)."""
        ins = self.r.code[a]
        op = ins.op
        m = op.mnem
        nxt = (a + op.size) & 0xFFFF
        tick = f'c.cyc += {op.cyc * T}; '
        if m in COND:
            t = ins.target()
            extra = T + (T if (t & 0xFF00) != (nxt & 0xFF00) else 0)
            return f'{tick}if ({COND[m]}) {{ c.cyc += {extra}; {self._goto(t)} }}', True
        if m == 'JMP':
            if op.mode == 'abs' and a in self.r.tramps:
                self._count('trampoline')
                return f'{tick}{{ uint16_t t; if (!a78_tramp(c, {h(ins.arg)}, t)) return;{self._bankcheck_t()} '                        f'{self._switch(self.r.tramps[a])} }}', False
            if op.mode == 'abs':
                return tick + self._goto(ins.arg), False
            self._count('indirect jump')
            cases = ' '.join(f'case {h(t)}: goto L_{t:04X};'
                             for t in self.r.indirect.get(a, []) if self._local(t))
            return (f'{tick}{{ const uint16_t t = a78_ind16(c, {h(ins.arg)}); '
                    f'switch (t) {{ {cases} default: a78_call(c, t); return; }} }}'), False
        if m == 'JSR':
            self._count('call')
            ret = (a + 3) & 0xFFFF
            push = (f'a78_push(c, {h((a + 2) >> 8 & 0xFF, 2)}); '
                    f'a78_push(c, {h((a + 2) & 0xFF, 2)});')
            callee = self._callee(ins.arg)
            if a in self.r.tramps:
                self._count('trampoline')
                callee = f'uint16_t t; if (!a78_tramp(c, {h(ins.arg)}, t)) return; a78_call(c, t);'
            return (f'{tick}{push} '
                    f'{{ const uint8_t s0 = c.s + 2; {callee} '
                    f'if (c.resume != {h(ret)} || c.s != s0) return; }}'
                    + self._bankcheck(ret)), True
        if m == 'RTS':
            pop = ('{ const uint8_t lo = a78_pull(c), hi = a78_pull(c); '
                   'c.resume = (uint16_t)((lo | hi << 8) + 1); c.rti = 0; ')
            jumps = self.r.rts_jumps.get(a)
            if not jumps:
                return f'{tick}{pop}return; }}', False
            # popped what this invocation pushed: a jump, not a return
            self._count('rts jump')
            cases = ' '.join(f'case {h(t)}: goto L_{t:04X};' for t in jumps if self._local(t))
            return (f'{tick}{pop}if (c.s <= s_in) {{ switch (c.resume) {{ {cases} '
                    f'default: a78_call(c, c.resume); return; }} }} return; }}'), False
        if m == 'RTI':
            return (f'{tick}a78_setp(c, a78_pull(c)); '
                    f'{{ const uint8_t lo = a78_pull(c), hi = a78_pull(c); '
                    f'c.resume = (uint16_t)(lo | hi << 8); }} c.rti = 1; a78_recalc(c); return;'), False
        text, bus = stmt(ins)
        text = tick + text
        if bus:
            text += self._bankcheck(nxt)
        return text, True

    def emit(self):
        r = self.r
        order = sorted(r.code)
        lines = [f'void {r.name}(A78& c, uint16_t e) {{']
        if r.rts_jumps:
            lines.append('    const uint8_t s_in = c.s;')
        lines.append('    switch (e) {')
        for e in sorted(r.entries):
            lines.append(f'    case {h(e)}: goto L_{e:04X};')
        lines.append('    default: c.resume = e; c.rti = 0; return;')
        lines.append('    }')
        poll = (lambda at: f'A78_POLLB(c, {h(at)}, {self.bank});') if self.win \
            else (lambda at: f'A78_POLL(c, {h(at)});')
        for n, a in enumerate(order):
            text, on = self.insn(a)
            if self.comments:
                text += f'  // {a:04X} {r.code[a]}'
            lines.append(f'L_{a:04X}: {poll(a)}')
            lines.append('    ' + text)
            ins = r.code[a]
            nxt = (a + ins.op.size) & 0xFFFF
            following = order[n + 1] if n + 1 < len(order) else None
            if on and nxt != following:
                lines.append('    ' + self._goto(nxt))
        lines.append('}')
        return lines
