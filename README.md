# 7800kit

Atari 7800 ProSystem (1986) reverse engineering and static recompilation:
the cartridge and its mappers, SALLY (the NMOS 6502 inside), a tracer that
measures what a recompiler faces in every game, a 6502-to-C++ recompiler,
and a runtime that replaces the console -- MARIA and all -- under the
recompiled code.

The sibling of [watarakit](https://github.com/vs-sr-dev/watarakit): the
recompiler grew out of its W65C02 one, rebuilt for the NMOS part, for
MARIA's interrupts in the middle of the screen, and for six mappers.

Status: the static recompiler runs the whole library. All 135 good dumps of
the set (NTSC, PAL and overdumps; in `Atari 7800/`, not distributed) are
recompiled to C++, built, and run frame for frame identical to the
interpreter (lockstep, 6000 frames each); 127 of them never touch the
interpreter at all. With sound: the TIA's two channels and the POKEY's four.

## Tools

```sh
python -m 7800kit.play "ms. pac"                               # find, recompile if needed, play in a window
python -m 7800kit.cart "7800kit/Atari 7800/*/*.a78" --table   # size, mapper, vectors
python -m 7800kit.m6502 ROM.a78 --vec                          # NMI / RESET / IRQ
python -m 7800kit.m6502 ROM.a78 --at F000 -n 40                # disassembly
python -m 7800kit.m6502 ROM.a78 --bank 3 --at 8000             # a page of the window

python -m 7800kit.tracer.build                                 # tracer/a78trace.exe (msys64 ucrt64 g++)
tracer/a78trace.exe ROM.a78 --trace OUTDIR --frames 6000 --snap 300,1500
tracer/a78trace.exe ROM.a78 --window                           # just play it, interpreted
python -m 7800kit.tracer.survey "7800kit/Atari 7800" --jobs 8  # every cart, one line each
```

`a78trace` is the runtime built with no native code: a 7800 (MAME's a7800
driver as the hardware reference) with a scripted pad. With `--trace` it
writes `report.txt` (coverage by bank -- code, data read, graphics fetched
by MARIA -- indirect jumps and their targets, RTS that do not return to
their JSR, dropped return addresses, code run from RAM, self-modifying
writes, bank switches, the NMIs per frame and where they land, WSYNCs, the
undocumented opcodes run, the TIA/MARIA/RIOT/POKEY registers used),
`cov.bin` (a flag per ROM byte: opcode, operand, read as data, jump
target, MARIA DMA) and PPM frames. `tools/ppm2png.py` makes contact sheets.

## The recompiler

```sh
python -m 7800kit.recomp.discover ROM.a78 --trace build/trace/GAME --report   # regions
python -m 7800kit.recomp ROM.a78 --trace build/trace/GAME --out build/recomp/GAME --build
build/recomp/GAME/a78.exe ROM.a78 --lockstep --frames 6000   # native vs interpreter, every frame
build/recomp/GAME/a78.exe ROM.a78 --window --scale 3         # play: arrows, Z/X, Enter = Reset,
                                                             # Shift = Select, P = Pause, F1/F2 difficulty
build/recomp/GAME/a78.exe ROM.a78 --frames 1800 --wav out.wav
python -m 7800kit.recomp.all "7800kit/Atari 7800" --jobs 6   # every cart: trace, recompile, build, lockstep
```

- `recomp/discover.py` -- code by *region*: the ROM outside the mapper's
  window (the same bytes whatever the bank) and the window with each bank.
  A region becomes one C++ function entered through a switch on the
  address: every instruction is emitted once and every branch or jump
  inside it is a `goto`. (watarakit's one-function-per-entry, with the
  code reached by JMPs copied into every function, gives Super Huey 442,000
  instructions for its 5,000; by region it is 7,220.) Entries: the vectors,
  the JSR targets, and the tracer's jump, indirect-jump and computed-RTS
  targets.
- `recomp/emit.py` -- 6502 to C++ with the interpreter's ticks (NMOS
  cycles, the page-crossing cycle, the taken branch's one or two); JSR as a
  C call with the return checked (`c.resume` and S), so `PLA PLA` exits
  unwind correctly; JMP (abs) as a switch over the traced targets; an RTS
  the tracer saw used as a jump (PHA PHA RTS) goes on to its target when it
  pops what its own invocation pushed; a JMP or JSR to a trampoline the
  game keeps in RAM (see below) reads the opcode there at run time and
  jumps natively.
- `runtime/` -- the machine (`a78.h`, `a78_core.cpp`): the bus, the six
  mappers, MARIA (display lists, the DMA that halts the CPU line by line,
  the display-list interrupt), the RIOT, the TIA's inputs, an interpreter in
  two forms -- flat (the reference) and nested (the fallback for whatever is
  not native, handing back to native code at every native entry) -- and
  the executable (`a78_main.cpp`: headless, `--interp`, `--lockstep`,
  `--trace`, `--window` with SDL2).
- `runtime/a78_audio.cpp` -- the sound, synthesized in CPU cycles from the
  sound registers' writes, which the machine logs with the tick of each:
  sound is output only, so it never takes part in what the CPU sees.
- Time is MARIA's clock: a CPU cycle is 4 ticks, a line 454, a TIA or RIOT
  access 2 more (the 1.19 MHz slow cycle). The machine's events -- the line
  start, MARIA's DMA 7 cycles in, the DLI -- are handled at the first
  instruction boundary past their time, by the interpreter and by the
  native code alike (a compare before every instruction). Watarakit could
  take interrupts at safe points only; a 7800 game changes colours from a
  DLI handler at a precise line, so here every boundary is one. That is
  what keeps the lockstep exact.
- `--lockstep-from F` compares the two at every line from frame F, and a
  build with `--history` keeps the last 4096 instructions each ran and
  `--watch ADDR` reports S and the stack each time an address runs: the
  tools that found the one real divergence (an interrupt taken inside the
  nested interpreter, whose handler moved the interpreter's own pc).

## The machine, briefly

| | Atari 7800 | Watara Supervision |
|---|---|---|
| CPU | 6502 (SALLY, NMOS) @ 1.79 MHz, halted by MARIA's DMA, 1.19 MHz on TIA/RIOT | W65C02 @ 4 MHz (SoC) |
| RAM | 4 KB (two 6116s; zero page and stack are mirrors of $2040/$2140), 128 B in the RIOT | 8 KB |
| Video | MARIA: display lists of objects, drawn into a line buffer, 160 or 320 wide, 25 colours of 256; no framebuffer, no sprite limit but DMA time | 8 KB framebuffer, 2 bpp, the CPU blits |
| Sound | TIA (2 channels); a POKEY (4) on some carts | 2 pulse, noise, sample DMA |
| Cart | 16-48 KB plain, or banked: SuperGame (16 KB at $8000), Absolute, Activision; RAM or POKEY at $4000 | 16 KB bank at $8000 |
| Interrupts | NMI from MARIA at the end of a zone (DLI) | frame NMI, timer IRQ, DMA IRQ |

## Survey of the library (6000 frames each, monkey pad)

- 88 NTSC, 51 PAL and 7 overdumps in the set; 12 are dumps no mapper here
  describes (the four Cuttle Cart 2 hacks keep the fixed bank in the middle
  of the file, Klax [a1] and Pit Fighter [a1] are scrambled, Impossible
  Mission [a1] jams at frame 129, the Symbolic Debugger is a development
  tool for a card with RAM), the rest boot and reach play or a menu.
- Mappers (NTSC): 36 plain, 31 SuperGame, 8 SuperGame with cart RAM, 4
  SuperGame with 9 banks, 4 plain with a POKEY, Commando's SuperGame with one, Absolute (F-18 Hornet), Activision (Double
  Dragon, Rampage), and Rescue on Fractalus's RAM without banks.
- Code actually run: 0.6-15 KB per game (median 7 KB); the rest of the
  carts is graphics -- up to 47 KB fetched by MARIA in Klax.
- **No game runs an undocumented opcode.** Bank switches -- up to 80,000 in
  100 seconds in Motor Psycho -- are never made from code inside the
  window: the switching code always lives in the fixed bank.
- Indirect jumps in three games out of four; RTS used as a jump (PHA PHA
  RTS) in F-18 Hornet, Planet Smashers, Rampage and Tubes.
- Code in RAM: a JMP or two in eleven games (below), real routines in three:
  Commando copies 301 bytes to $1D00, Fatal Run 56 bytes to $2523 and
  rewrites them 18,000 times in 100 seconds, the diagnostic cart tests
  memory from the RIOT's RAM. Those stay interpreted -- 170 instructions a
  frame.
- DLIs: one a frame (Asteroids, Galaga, Joust...) to 32 (Ms. Pac-Man);
  none at all in Tank Command, Water Ski and Tubes. WSYNC: 97 a frame in
  Ballblazer and 95 in Pole Position II, whose 3D grids and roads are drawn
  line by line.
- MARIA's DMA takes 50 of the 454 ticks of a line in Asteroids, 248 in Ms.
  Pac-Man: more than half the CPU's time, gone.
- The recompiled code is only 1.3-1.6 times as fast as the interpreter
  (180-290x real time against 120-180x): a 1.79 MHz CPU is a small part of
  the work; MARIA's lines are most of it.

## Notes from the library

- **Headers that lie.** The NTSC Impossible Mission says PAL and SuperGame
  without RAM; Commando forgets its POKEY, Summer and Winter Games their
  RAM; F-18 Hornet's cart type is $0300, which means nothing; Double Dragon
  and Rampage claim SuperGame. `cart.py` takes MAME's software list's
  word (by CRC), the set's folders for NTSC/PAL, and a few titles. The
  Activision .a78 dumps are in the order the CPU sees them, 8 KB at a
  time; MAME's are the EPROMs', each bank's halves swapped.
- **A JMP in zero page.** Choplifter, Karateka, Gato and Hat Trick write
  `JMP (ptr)` to $EA or $FA and jump there to dispatch; Mario Bros. and
  Donkey Kong Jr. keep theirs at $2430, Donkey Kong at $2100, Dig Dug at
  $1FBD, Pit Fighter two at $4B and $4F. Gato and Hat Trick sign the same
  block at the same offset ($045B): *"THE ATARI 7800 VERSION, BY JACK
  SANDBERG, SR. SOFTWARE ENGINEER, ibid inc., WEST HARTFORD, CT ... SECURITY
  STRING DO NOT REMOVE"* -- and Karateka keeps its intro text there.
- **NMI vectors into RAM.** Desert Falcon's NMI vector is $0040, Ikari
  Warriors' $00F0: a `JMP` the game rewrites for every zone, a DLI chain in
  software.
- **What the developers left.** 3D Asteroids: *"IF YOU CAN READ THIS YOU'RE
  TOO CLOSE. GCC (C)1984"* (Asteroids keeps the end of it). The diagnostic
  cartridge: *"(C) 1984 GCC VIA THE ALIEN - (C) 1984 ATARI VIA RON AND
  JERRY - LONG LIVE MARIA!"* Midnight Mutants, *"PROGRAM AND DESIGN BY PETER
  ADAMS"*, has a Professor Von Adams in its lore and a hint that reads *"IT'S
  A WONDER PETER EVER GOT THIS PROGRAM BEDUGGED!"* Rampage carries its
  porter's address and phone number (Spectral Dimensions, Atlanta, GA).
- **Impossible Mission falls.** Driven by the monkey pad, the agent falls
  and the game ends in a BRK loop at $0000 a little later, the same way
  native and interpreted (so not the recompiler); whether a real 7800 does
  the same is not checked yet.

## Not covered (yet)

- The BIOS: the cart starts at its reset vector, RAM cleared (the BIOS's
  signature check is skipped, as most emulators allow).
- Light guns (Alien Brigade, Barnyard Blaster, Crossbow, Meltdown,
  Sentinel): they run, but the monkey pad has no gun.
- The POKEY's timers and serial port, the High Score Cartridge, XM.
- The sound is a first pass, to be checked by ear.
