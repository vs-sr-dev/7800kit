// The Atari 7800 under recompiled code: the machine, SALLY's state (an NMOS
// 6502), and the helpers the generated C++ is written in.
//
// Time is counted in ticks of MARIA's clock (7.16 MHz NTSC): a CPU cycle is
// 4 ticks, a scanline 454, and an access to the TIA or the RIOT stretches
// its cycle to 6 (the 1.19 MHz slow bus).
//
// The rule that holds the recompiled code and the interpreter together: both
// charge the same ticks per instruction, and both take interrupts at every
// instruction boundary -- the machine's events (the line start, MARIA's DMA,
// which halts the CPU, and its display-list interrupt, an NMI) are handled
// at the first boundary at or past their time, before the next instruction.
// So the machine at every frame is the same whichever way the code ran: the
// lockstep self-test (a78 --lockstep) compares it.
#pragma once
#include <cstdint>
#include <cstring>

struct A78;
typedef void (*A78Fn)(A78&);

enum {
    TICKS_CYC = 4,            // ticks per CPU cycle
    LINE_TICKS = 454,         // ticks per scanline
    DMA_START = 28,           // MARIA starts its DMA 7 CPU cycles into a line
    SLOW_EXTRA = 2,           // a TIA/RIOT access: 6 ticks for its cycle, not 4
};

// Mappers (cart.py has the same list).
enum { MAP_ROM, MAP_POKEY, MAP_MRAM, MAP_SG, MAP_SG_POKEY, MAP_SG_RAM, MAP_SG9, MAP_ABS, MAP_ACT };

// A write to a sound register, stamped with its tick: TIA (reg 0x15-0x1A)
// or POKEY (0x40 + reg).  Sound is output only.  reg 0xFF marks a frame.
struct A78SndEv { uint64_t t; uint8_t reg, val; };
enum { A78_SND_EVENTS = 16384 };

struct A78Trace;   // what the tracer measures (a78_core.cpp)

struct A78 {
    // ---- SALLY: each flag a byte of 0 or 1 ----
    uint8_t a = 0, x = 0, y = 0, s = 0xFF;
    uint8_t fn = 0, fv = 0, fd = 0, fi = 1, fz = 0, fc = 0;
    uint16_t pc = 0;          // the interpreter's
    uint16_t resume = 0;      // where the last RTS / RTI went
    uint8_t rti = 0;          // ... and whether it was an RTI
    uint8_t jammed = 0;

    // ---- time ----
    uint64_t cyc = 0;         // ticks
    uint64_t next = 0;        // the next tick anything happens (0: an interrupt is pending)
    uint64_t ev_at = 0;       // the next line event
    uint64_t line_start = 0;  // tick of the current line's start
    uint64_t wsync_to = 0;    // the CPU is halted (WSYNC) up to this tick
    int line = 0, ev_phase = 0;   // phase 0: line start next, 1: DMA next
    int lines = 263;          // 313 PAL
    uint64_t frame = 0;
    uint8_t nmi_pending = 0;

    // ---- memory ----
    uint8_t ram[0x1000];      // $1800-$27FF; zero page $40-$FF is $2040, stack $140-$1FF $2140
    uint8_t riot_ram[0x80];   // $0480-$04FF
    uint8_t cram[0x8000];     // cartridge RAM
    const uint8_t* rom = nullptr;
    uint32_t romsize = 0;
    int mapper = MAP_ROM;
    uint32_t bank = 0, mask = 0, nbanks = 0;
    uint16_t win_lo = 0, win_hi = 0;      // the window (win_lo == win_hi: none)
    const uint8_t* rd[32];    // 2 KB pages: memory to read, or null (the slow path)
    uint8_t* wr[32];          // memory to write, or null
    int32_t roff[32];         // ROM offset of the page, -1 if not ROM

    // ---- MARIA ----
    uint8_t pal[32];
    uint16_t dpp = 0, dll = 0, dl = 0;
    uint8_t charbase = 0, offset = 0, holey = 0, dli = 0, dmaon = 0, cwidth = 0;
    uint8_t kangaroo = 0, rm = 0, wmode = 0, colorkill = 0, ctrl = 0, mstat = 0x80;
    uint8_t lram[2][160];
    uint8_t abuf = 0;
    uint8_t fb[313][320];     // the picture: a colour byte per pixel

    // ---- TIA, RIOT, console, POKEY ----
    uint8_t inptctrl = 0, lock = 0;
    uint8_t tia[0x20];
    uint8_t joy = 0xFF;       // SWCHA pins: P0 R L D U (bits 7-4), P1 (3-0), active low
    uint8_t buttons = 0;      // MAME's BUTTONS: 1 P1 b2, 2 P0 b2, 4 P1 b1, 8 P0 b1 (active high)
    uint8_t console = 0xFF;   // SWCHB pins: 1 reset, 2 select, 8 pause (active low), 40/80 difficulty
    uint8_t ddra = 0, ddrb = 0, ora = 0, orb = 0;
    uint8_t one_button0 = 1, one_button1 = 1;
    uint64_t timer_t0 = 0;    // RIOT timer: written at this tick...
    uint32_t timer_shift = 10; uint8_t timer_v = 0, timer_flag = 0;
    uint8_t pokey[16];

    // ---- sound: the registers' writes since the synthesizer last took them ----
    bool snd_log = false;
    uint32_t snd_n = 0;
    A78SndEv snd_ev[A78_SND_EVENTS];

    // ---- how the code runs ----
    bool natives = true;      // false: the interpreter alone (the reference)
    bool flat_stop = false;   // the flat interpreter stops at the frame event
    bool line_stop = false;   // ... and at every line's start
    void (*frame_hook)(A78&) = nullptr;
    void (*line_hook)(A78&) = nullptr;     // at every line's start (debugging)

    // the last instructions run, native and interpreted (with A78_HISTORY)
    uint16_t hist[4096]; uint8_t hbank[4096]; uint64_t hcyc[4096]; uint32_t hn = 0;
    int32_t watch = -1; uint64_t watch_from = 0;   // --watch ADDR: report each time it runs
    void* user = nullptr;
    A78Trace* tr = nullptr;

    // ---- what ran ----
    uint64_t n_interp = 0, n_native_calls = 0, n_fallback = 0, n_unwind = 0;
    uint64_t n_nmi = 0, n_brk = 0, n_wsync = 0, n_dma_full = 0;
    int depth = 0, max_depth = 0;

    A78() {
        std::memset(ram, 0, sizeof ram); std::memset(riot_ram, 0, sizeof riot_ram);
        std::memset(cram, 0, sizeof cram); std::memset(pal, 0, sizeof pal);
        std::memset(lram, 0, sizeof lram); std::memset(fb, 0, sizeof fb);
        std::memset(tia, 0, sizeof tia); std::memset(pokey, 0, sizeof pokey);
        std::memset(rd, 0, sizeof rd); std::memset(wr, 0, sizeof wr);
        for (auto& o : roff) o = -1;
    }
};

extern const uint8_t A78_CYC[256];

// ---- the bus ----
uint8_t a78_rd_slow(A78& c, uint16_t a);
void a78_wr_slow(A78& c, uint16_t a, uint8_t v);
uint8_t a78_io_r(A78& c, uint16_t a);          // $0000-$07FF: TIA, MARIA, RIOT, RAM mirrors
void a78_io_w(A78& c, uint16_t a, uint8_t v);

static inline uint8_t a78_rd(A78& c, uint16_t a) {
    const uint8_t* p = c.rd[a >> 11];
    return p ? p[a & 0x7FF] : a78_rd_slow(c, a);
}
static inline void a78_wr(A78& c, uint16_t a, uint8_t v) {
    uint8_t* p = c.wr[a >> 11];
    if (p) p[a & 0x7FF] = v; else a78_wr_slow(c, a, v);
}
// zero page: $40-$FF is RAM, $00-$3F the TIA and MARIA
static inline uint8_t a78_zrd(A78& c, uint8_t z) {
    return z >= 0x40 ? c.ram[0x800 + z] : a78_io_r(c, z);
}
static inline void a78_zwr(A78& c, uint8_t z, uint8_t v) {
    if (z >= 0x40) c.ram[0x800 + z] = v; else a78_io_w(c, z, v);
}
static inline uint16_t a78_zpw(A78& c, uint8_t z) {
    return a78_zrd(c, z) | a78_zrd(c, (uint8_t)(z + 1)) << 8;
}
static inline uint16_t a78_rd16(A78& c, uint16_t a) {
    return a78_rd(c, a) | a78_rd(c, (uint16_t)(a + 1)) << 8;
}
// JMP (abs): the NMOS part fetches the high byte from the same page
static inline uint16_t a78_ind16(A78& c, uint16_t a) {
    return a78_rd(c, a) | a78_rd(c, (uint16_t)((a & 0xFF00) | ((a + 1) & 0xFF))) << 8;
}

// ---- the stack: page 1, $140-$1FF is RAM ($2140), $100-$13F the TIA and MARIA ----
static inline void a78_push(A78& c, uint8_t v) {
    if (c.s >= 0x40) c.ram[0x900 + c.s] = v; else a78_io_w(c, 0x100 + c.s, v);
    c.s--;
}
static inline uint8_t a78_pull(A78& c) {
    c.s++;
    return c.s >= 0x40 ? c.ram[0x900 + c.s] : a78_io_r(c, 0x100 + c.s);
}

// ---- flags and ALU (NMOS: decimal mode sets N V Z from the binary sum) ----
static inline uint8_t a78_p(const A78& c) {   // P as pushed by an interrupt (B clear)
    return c.fn << 7 | c.fv << 6 | 0x20 | c.fd << 3 | c.fi << 2 | c.fz << 1 | c.fc;
}
static inline void a78_setp(A78& c, uint8_t p) {
    c.fn = p >> 7; c.fv = p >> 6 & 1; c.fd = p >> 3 & 1; c.fi = p >> 2 & 1;
    c.fz = p >> 1 & 1; c.fc = p & 1;
}
static inline uint8_t a78_nz(A78& c, uint8_t v) { c.fn = v >> 7; c.fz = v == 0; return v; }

static inline void a78_adc(A78& c, uint8_t v) {
    unsigned cin = c.fc;
    if (c.fd) {
        unsigned al = (c.a & 0x0F) + (v & 0x0F) + cin;
        if (al > 9) al += 6;
        unsigned ah = (c.a >> 4) + (v >> 4) + (al > 0x0F);
        c.fz = ((c.a + v + cin) & 0xFF) == 0;
        c.fn = (ah >> 3) & 1;
        c.fv = ((~(c.a ^ v) & (c.a ^ (ah << 4))) >> 7) & 1;
        if (ah > 9) ah += 6;
        c.fc = ah > 0x0F;
        c.a = (uint8_t)(ah << 4 | (al & 0x0F));
    } else {
        unsigned r = c.a + v + cin;
        c.fv = ((~(c.a ^ v) & (c.a ^ r)) >> 7) & 1;
        c.fc = r > 0xFF;
        a78_nz(c, c.a = (uint8_t)r);
    }
}
static inline void a78_sbc(A78& c, uint8_t v) {
    unsigned cin = c.fc;
    unsigned r = c.a - v - (1 - cin);
    c.fv = (((c.a ^ v) & (c.a ^ r)) >> 7) & 1;
    c.fc = r < 0x100;
    if (c.fd) {
        int al = (c.a & 0x0F) - (v & 0x0F) - (int)(1 - cin);
        int ah = (c.a >> 4) - (v >> 4);
        if (al & 0x10) { al -= 6; ah--; }
        if (ah & 0x10) ah -= 6;
        a78_nz(c, (uint8_t)r);
        c.a = (uint8_t)((ah << 4) | (al & 0x0F));
    } else a78_nz(c, c.a = (uint8_t)r);
}
static inline void a78_cmp(A78& c, uint8_t r, uint8_t v) {
    c.fc = r >= v; a78_nz(c, (uint8_t)(r - v));
}
static inline void a78_bit(A78& c, uint8_t v) {
    c.fz = (c.a & v) == 0; c.fn = v >> 7; c.fv = v >> 6 & 1;
}
static inline uint8_t a78_asl(A78& c, uint8_t v) { c.fc = v >> 7; return a78_nz(c, (uint8_t)(v << 1)); }
static inline uint8_t a78_lsr(A78& c, uint8_t v) { c.fc = v & 1; return a78_nz(c, v >> 1); }
static inline uint8_t a78_rol(A78& c, uint8_t v) {
    uint8_t ci = c.fc; c.fc = v >> 7; return a78_nz(c, (uint8_t)(v << 1 | ci));
}
static inline uint8_t a78_ror(A78& c, uint8_t v) {
    uint8_t ci = c.fc; c.fc = v & 1; return a78_nz(c, (uint8_t)(v >> 1 | ci << 7));
}
// the undocumented ones
static inline void a78_arr(A78& c, uint8_t v) {
    c.a &= v;
    c.a = (uint8_t)(c.a >> 1 | c.fc << 7);
    a78_nz(c, c.a);
    c.fc = c.a >> 6 & 1;
    c.fv = (c.a >> 6 ^ c.a >> 5) & 1;
}
static inline void a78_sbx(A78& c, uint8_t v) {
    unsigned t = (c.a & c.x);
    c.fc = t >= v;
    a78_nz(c, c.x = (uint8_t)(t - v));
}

// ---- time and interrupts ----
// A78_POLL is the boundary before an instruction at `at`: the events due
// are handled, and an interrupt taken returns to `at`.  a78_poll returns
// false when it did not come back there with the stack as it was: the
// native code then returns, and its callers unwind.
bool a78_poll(A78& c, uint16_t at);
void a78_recalc(A78& c);         // c.next from the events and the NMI line
#ifdef A78_HISTORY
void a78_watch(const A78& c, uint16_t at);
#define A78_HIST(c, at) ((c).hist[(c).hn & 4095] = (at), (c).hcyc[(c).hn & 4095] = (c).cyc, \
    (c).hbank[(c).hn++ & 4095] = (uint8_t)(c).bank, (at) == (c).watch ? a78_watch(c, at) : (void)0)
#else
#define A78_HIST(c, at) ((void)0)
#endif
#define A78_POLL(c, at) do { if ((c).cyc >= (c).next && !a78_poll((c), (at))) return; A78_HIST(c, at); } while (0)
// the same in a function of the window: leave if the bank changed under it
#define A78_POLLB(c, at, b) do { if ((c).cyc >= (c).next) { \
    if (!a78_poll((c), (at))) return; \
    if ((c).bank != (b)) { a78_call((c), (at)); return; } } A78_HIST(c, at); } while (0)

// ---- calls between native and interpreted code ----
void a78_call(A78& c, uint16_t target);       // run code at target as a JSR'd routine
A78Fn a78_lookup(A78& c, uint16_t pc);        // the native function at pc (bank as now), or null
bool a78_tramp(A78& c, uint16_t a, uint16_t& t);   // a JMP (abs/ind) the game keeps in RAM at a
void a78_exec(A78& c, uint16_t pc, int base);  // interpret until an RTS/RTI lifts S above base
void a78_register(int slot, uint16_t addr, A78Fn fn);   // slot: window bank, or -1 outside it
void a78_register_all();                      // generated: every native function
[[noreturn]] void a78_jam(A78& c, uint16_t at);

// ---- the machine ----
bool a78_load(A78& c, const uint8_t* rom, uint32_t size, int mapper, bool pal);
void a78_reset(A78& c);
void a78_run(A78& c);              // from the reset vector, forever (frame_hook decides)
bool a78_flat_frame(A78& c);       // the interpreter alone, to the next frame event
uint64_t a78_hash(const A78& c);
void a78_rgb(const A78& c, uint32_t* out, int& w, int& h);   // the visible picture, 0xRRGGBB
struct A78Stop {};                 // thrown by a frame hook to end the run

// ---- the tracer ----
void a78_trace_begin(A78& c);
void a78_trace_report(A78& c, const char* dir, const char* rom_path);
int a78_mapper_by_name(const char* name);
