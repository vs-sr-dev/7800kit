# Sources

7800kit is MIT-licensed (`LICENSE`). It contains one piece of third-party
data: the RGB values of the 7800's 256 colours in `runtime/a78_palette.h`,
taken from MAME's a7800 driver (`src/mame/atari/a7800.cpp`, Robert
Tuccitto's palettes, BSD-3-Clause).

The hardware the runtime models -- the memory map, MARIA's display lists,
DMA timing and display-list interrupt, the RIOT, the TIA's inputs and the
cartridge mappers -- follows the behaviour of MAME's a7800 driver
(`src/mame/atari/a7800.cpp`, `maria.cpp`, `src/devices/bus/a7800/`, by Dan
Boris, Fabio Priuli, Mike Saarna, Robert Tuccitto; BSD-3-Clause), read as
documentation: the interpreter, the runtime and the synthesizer are written
independently. Where a cart's .a78 header is wrong, `cart.py` uses the
mapper given by MAME's software list (`hash/a7800.xml`). The Activision
layout of the .a78 dumps follows the a7800 emulator's description.

The recompiler and the runtime's structure come from watarakit
(https://github.com/vs-sr-dev/watarakit, same author, MIT).
