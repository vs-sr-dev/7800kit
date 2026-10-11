"""Code discovery for 7800 cartridges.

    python -m 7800kit.recomp.discover ROM.a78 [--trace DIR] [--report]

The code is gathered by region: the ROM outside the mapper's window (slot
-1, the same bytes whatever the bank) and, for each bank, the window with
that bank selected (slot k).  A region is one C++ function in the end,
entered by a switch on the address it is called at: every instruction is
emitted once, and every branch or jump inside a region is a goto -- a state
machine that jumps around never grows the C stack, and code reached from
many entries is not duplicated.

A region holds everything control reaches from its entries: both ways at a
branch, JMP abs, an indirect JMP to the targets the tracer saw, an RTS the
tracer saw used as a jump (PHA PHA RTS) to the targets it took.  A walk
stops at RTS and RTI, and at what no game runs -- BRK, JAM and the
undocumented opcodes, the signs of a walk into data -- and at a transfer
out of the region: into another region (the target becomes one of its
entries) or where the bytes cannot be known statically -- the window from
outside it (which bank?), RAM, an indirect target nobody saw.  At run time
those go through `a78_call`, which finds the native code for the bank
selected then, or interprets.

Entries: the vectors; every JSR target the descent meets (from outside the
window into it, in each bank where the tracer ran code at that address);
and, from a trace (tracer/a78trace output), every address that was the
target of a JSR, a JMP or a computed RTS.  Native code translates the bytes
at an address whatever discovery thought of them, so an entry in the wrong
place costs code, never correctness.
"""
import argparse
import os
import re
import sys

from .. import cart as cartmod
from .. import m6502

FIXED = -1
R_OP, R_ARG, R_READ, R_JTGT, R_DMA = 1, 2, 4, 8, 16


def rname(slot):
    return 'r_F' if slot == FIXED else f'r_B{slot}'


def ename(slot, addr):
    return f'e_F_{addr:04X}' if slot == FIXED else f'e_B{slot}_{addr:04X}'


class Region:
    __slots__ = ('slot', 'entries', 'code', 'calls', 'indirect', 'rts_jumps', 'tramps', 'problems', 'work')

    def __init__(self, slot):
        self.slot = slot
        self.entries = set()
        self.code = {}            # addr -> Insn
        self.calls = set()        # (slot, target) of JSRs, slot None if dynamic
        self.indirect = {}        # addr of JMP (abs) -> [targets]
        self.rts_jumps = {}       # addr of an RTS used as a jump -> [targets]
        self.tramps = {}          # addr of a JMP / JSR to a trampoline in RAM -> [targets]
        self.problems = []
        self.work = []

    @property
    def name(self):
        return rname(self.slot)


class Trace:
    """What a78trace saw: per ROM byte flags, the indirect jumps, the computed RTS."""

    def __init__(self, path, cart):
        self.cart = cart
        self.cov = open(os.path.join(path, 'cov.bin'), 'rb').read()
        self.ind = {}             # (slot, site) -> {target}
        self.rts = {}
        self.tramp = {}           # RAM address of a JMP the game keeps there -> {(slot, target)}
        rep = open(os.path.join(path, 'report.txt')).read()
        for k, s, d in re.findall(r'^edge ([ir]) (\S+) -> (\S+) x\d+', rep, re.M):
            ss, sa = _parse_loc(s)
            ds, da = _parse_loc(d)
            if ds is None:
                continue
            if ss is None:
                if k == 'i':
                    self.tramp.setdefault(sa, set()).add((ds, da))
                continue
            (self.ind if k == 'i' else self.rts).setdefault((ss, sa), set()).add(da)

    def flag(self, slot, addr):
        o = self.cart.offset(addr, 0 if slot == FIXED else slot)
        return self.cov[o] if o is not None and o < len(self.cov) else 0


def _parse_loc(s):
    m = re.match(r'(F|B(\d+)|RAM):([0-9A-F]{4})', s)
    if not m:
        return None, None
    a = int(m.group(3), 16)
    if m.group(1) == 'F':
        return FIXED, a
    if m.group(1) == 'RAM':
        return None, a
    return int(m.group(2)), a


class Program:
    def __init__(self, cart, trace=None):
        self.cart, self.trace = cart, trace
        self.regions = {}         # slot -> Region
        self.readers = {FIXED: cart.reader()}
        for b in cart.banks:
            self.readers[b] = cart.reader(b)
        # every place a ROM byte shows in the CPU's space: offset -> [(slot, addr)]
        self.where = {}
        for a in range(0x4000, 0x10000):
            if cart.in_window(a):
                for b in cart.banks:
                    o = cart.offset(a, b)
                    if o is not None:
                        self.where.setdefault(o, []).append((b, a))
            else:
                o = cart.offset(a)
                if o is not None:
                    self.where.setdefault(o, []).append((FIXED, a))

    def slot_of(self, addr, rslot):
        """The slot an address seen from region `rslot` is read from, or None
        when it cannot be known (the window from outside it, RAM, I/O)."""
        c = self.cart
        if c.in_window(addr):
            return rslot if rslot != FIXED else None
        return FIXED if c.offset(addr) is not None else None

    def window_banks(self, addr):
        """Banks where the tracer ran code at a window address."""
        if not self.trace:
            return []
        return [b for b in self.cart.banks if self.trace.flag(b, addr) & R_OP]

    def region(self, slot):
        if slot not in self.regions:
            self.regions[slot] = Region(slot)
        return self.regions[slot]

    def entry(self, slot, addr):
        r = self.region(slot)
        if addr not in r.entries:
            r.entries.add(addr)
            r.work.append(addr)

    def _goto(self, r, t):
        """A transfer to t from region r: walked on here, or an entry elsewhere."""
        s = self.slot_of(t, r.slot)
        if s == r.slot:
            r.work.append(t)
        elif s is not None:
            self.entry(s, t)

    def _tramp(self, r, a, t):
        """A JMP or JSR to RAM where the tracer saw a JMP run: a trampoline."""
        if not self.trace or t not in self.trace.tramp or self.slot_of(t, r.slot) is not None:
            return False
        r.tramps[a] = sorted(self.trace.tramp[t])
        return True

    def _step(self, r):
        a = r.work.pop()
        if a in r.code:
            return
        s = self.slot_of(a, r.slot)
        if s != r.slot:
            return
        ins = m6502.decode(self.readers[s], a)
        op = ins.op
        if op.illegal or op.mnem in ('BRK', 'JAM'):
            r.problems.append(a)
            return
        r.code[a] = ins
        m = op.mnem
        nxt = (a + op.size) & 0xFFFF
        t = ins.target()
        if m in m6502.BRANCHES:
            self._goto(r, t)
            self._goto(r, nxt)
        elif m == 'JMP':
            if op.mode == 'abs' and self._tramp(r, a, t):
                for ts, x in self.trace.tramp[t]:
                    if ts == r.slot or ts == FIXED:
                        self._goto(r, x)
                    else:
                        self.entry(ts, x)
            elif op.mode == 'abs':
                self._goto(r, t)
            else:
                seen = self.trace.ind.get((s, a), set()) if self.trace else set()
                r.indirect[a] = sorted(seen)
                for x in seen:
                    self._goto(r, x)
        elif m == 'JSR':
            ts = self.slot_of(t, r.slot)
            r.calls.add((ts, t))
            if self._tramp(r, a, t):
                for xs, x in self.trace.tramp[t]:
                    self.entry(xs, x)
            elif ts is not None:
                self.entry(ts, t)
            elif self.cart.in_window(t):
                for b in self.window_banks(t):
                    self.entry(b, t)
            self._goto(r, nxt)
        elif m == 'RTS':
            seen = self.trace.rts.get((s, a), set()) if self.trace else set()
            if seen:
                r.rts_jumps[a] = sorted(seen)
                for x in seen:
                    self._goto(r, x)
        elif m == 'RTI':
            pass
        else:
            self._goto(r, nxt)

    def discover(self):
        c = self.cart
        for v in c.vectors().values():
            if c.in_window(v):
                for b in (self.window_banks(v) or c.banks[:1]):
                    self.entry(b, v)
            elif c.offset(v) is not None:
                self.entry(FIXED, v)
        if self.trace:
            for o, fl in enumerate(self.trace.cov):
                if fl & R_JTGT:
                    for s, a in self.where.get(o, []):
                        self.entry(s, a)
        busy = True
        while busy:
            busy = False
            for r in list(self.regions.values()):
                while r.work:
                    busy = True
                    self._step(r)
        for r in self.regions.values():              # entries on data: left to the interpreter
            r.entries = {a for a in r.entries if a in r.code}
        self.regions = {s: r for s, r in self.regions.items() if r.entries}
        return self

    def coverage(self):
        """Executed instruction starts (per the trace) that some region holds."""
        if not self.trace:
            return None
        held = set()
        for slot, r in self.regions.items():
            for a in r.code:
                o = self.cart.offset(a, 0 if slot == FIXED else slot)
                if o is not None:
                    held.add(o)
        ran = [o for o, x in enumerate(self.trace.cov) if x & R_OP]
        return sum(1 for o in ran if o in held), len(ran)


def main(argv=None):
    ap = argparse.ArgumentParser(prog='7800kit.recomp.discover')
    ap.add_argument('rom')
    ap.add_argument('--trace')
    ap.add_argument('--report', action='store_true')
    a = ap.parse_args(argv)
    c = cartmod.Cart.load(a.rom)
    tr = Trace(a.trace, c) if a.trace else None
    p = Program(c, tr).discover()
    insns = sum(len(r.code) for r in p.regions.values())
    ents = sum(len(r.entries) for r in p.regions.values())
    print(f'{c.name}: {len(p.regions)} regions, {ents} entries, {insns} instructions')
    cov = p.coverage()
    if cov:
        print(f'  traced instructions held by a region: {cov[0]}/{cov[1]}')
    probs = sum(len(r.problems) for r in p.regions.values())
    ind = sum(len(r.indirect) for r in p.regions.values())
    rj = sum(len(r.rts_jumps) for r in p.regions.values())
    print(f'  indirect jump sites {ind}, RTS jump sites {rj}, walks into data {probs}')
    if a.report:
        for slot in sorted(p.regions):
            r = p.regions[slot]
            print(f'  {r.name:6} {len(r.code):6} insns  {len(r.entries):4} entries  {len(r.calls):4} calls'
                  + (f'  data at {",".join("%04X" % x for x in r.problems[:6])}' if r.problems else ''))


if __name__ == '__main__':
    sys.exit(main())
