// The machine under the recompiled code: the bus and the mappers, MARIA
// (display lists, DMA that halts the CPU, the display-list interrupt), the
// RIOT, the TIA's inputs, the interpreter, the door between native and
// interpreted code, and what the tracer measures.  The hardware follows
// MAME's a7800 driver (see THIRD_PARTY.md).
#include "a78.h"
#include "a78_palette.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

// ---- the opcode table: the same text as 7800kit/m6502.py ----
static const char* ROWS =
"00 BRK imm  01 ORA indx 02 JAM* imp 03 SLO* indx 04 NOP* zp  05 ORA zp   06 ASL zp   07 SLO* zp "
"08 PHP imp  09 ORA imm  0A ASL acc  0B ANC* imm  0C NOP* abs 0D ORA abs  0E ASL abs  0F SLO* abs "
"10 BPL rel  11 ORA indy 12 JAM* imp 13 SLO* indy 14 NOP* zpx 15 ORA zpx  16 ASL zpx  17 SLO* zpx "
"18 CLC imp  19 ORA absy 1A NOP* imp 1B SLO* absy 1C NOP* absx 1D ORA absx 1E ASL absx 1F SLO* absx "
"20 JSR abs  21 AND indx 22 JAM* imp 23 RLA* indx 24 BIT zp  25 AND zp   26 ROL zp   27 RLA* zp "
"28 PLP imp  29 AND imm  2A ROL acc  2B ANC* imm  2C BIT abs 2D AND abs  2E ROL abs  2F RLA* abs "
"30 BMI rel  31 AND indy 32 JAM* imp 33 RLA* indy 34 NOP* zpx 35 AND zpx  36 ROL zpx  37 RLA* zpx "
"38 SEC imp  39 AND absy 3A NOP* imp 3B RLA* absy 3C NOP* absx 3D AND absx 3E ROL absx 3F RLA* absx "
"40 RTI imp  41 EOR indx 42 JAM* imp 43 SRE* indx 44 NOP* zp  45 EOR zp   46 LSR zp   47 SRE* zp "
"48 PHA imp  49 EOR imm  4A LSR acc  4B ALR* imm  4C JMP abs 4D EOR abs  4E LSR abs  4F SRE* abs "
"50 BVC rel  51 EOR indy 52 JAM* imp 53 SRE* indy 54 NOP* zpx 55 EOR zpx  56 LSR zpx  57 SRE* zpx "
"58 CLI imp  59 EOR absy 5A NOP* imp 5B SRE* absy 5C NOP* absx 5D EOR absx 5E LSR absx 5F SRE* absx "
"60 RTS imp  61 ADC indx 62 JAM* imp 63 RRA* indx 64 NOP* zp  65 ADC zp   66 ROR zp   67 RRA* zp "
"68 PLA imp  69 ADC imm  6A ROR acc  6B ARR* imm  6C JMP ind 6D ADC abs  6E ROR abs  6F RRA* abs "
"70 BVS rel  71 ADC indy 72 JAM* imp 73 RRA* indy 74 NOP* zpx 75 ADC zpx  76 ROR zpx  77 RRA* zpx "
"78 SEI imp  79 ADC absy 7A NOP* imp 7B RRA* absy 7C NOP* absx 7D ADC absx 7E ROR absx 7F RRA* absx "
"80 NOP* imm 81 STA indx 82 NOP* imm 83 SAX* indx 84 STY zp  85 STA zp   86 STX zp   87 SAX* zp "
"88 DEY imp  89 NOP* imm 8A TXA imp  8B ANE* imm  8C STY abs 8D STA abs  8E STX abs  8F SAX* abs "
"90 BCC rel  91 STA indy 92 JAM* imp 93 SHA* indy 94 STY zpx 95 STA zpx  96 STX zpy  97 SAX* zpy "
"98 TYA imp  99 STA absy 9A TXS imp  9B TAS* absy 9C SHY* absx 9D STA absx 9E SHX* absy 9F SHA* absy "
"A0 LDY imm  A1 LDA indx A2 LDX imm  A3 LAX* indx A4 LDY zp  A5 LDA zp   A6 LDX zp   A7 LAX* zp "
"A8 TAY imp  A9 LDA imm  AA TAX imp  AB LXA* imm  AC LDY abs AD LDA abs  AE LDX abs  AF LAX* abs "
"B0 BCS rel  B1 LDA indy B2 JAM* imp B3 LAX* indy B4 LDY zpx B5 LDA zpx  B6 LDX zpy  B7 LAX* zpy "
"B8 CLV imp  B9 LDA absy BA TSX imp  BB LAS* absy BC LDY absx BD LDA absx BE LDX absy BF LAX* absy "
"C0 CPY imm  C1 CMP indx C2 NOP* imm C3 DCP* indx C4 CPY zp  C5 CMP zp   C6 DEC zp   C7 DCP* zp "
"C8 INY imp  C9 CMP imm  CA DEX imp  CB SBX* imm  CC CPY abs CD CMP abs  CE DEC abs  CF DCP* abs "
"D0 BNE rel  D1 CMP indy D2 JAM* imp D3 DCP* indy D4 NOP* zpx D5 CMP zpx  D6 DEC zpx  D7 DCP* zpx "
"D8 CLD imp  D9 CMP absy DA NOP* imp DB DCP* absy DC NOP* absx DD CMP absx DE DEC absx DF DCP* absx "
"E0 CPX imm  E1 SBC indx E2 NOP* imm E3 ISC* indx E4 CPX zp  E5 SBC zp   E6 INC zp   E7 ISC* zp "
"E8 INX imp  E9 SBC imm  EA NOP imp  EB SBC* imm  EC CPX abs ED SBC abs  EE INC abs  EF ISC* abs "
"F0 BEQ rel  F1 SBC indy F2 JAM* imp F3 ISC* indy F4 NOP* zpx F5 SBC zpx  F6 INC zpx  F7 ISC* zpx "
"F8 SED imp  F9 SBC absy FA NOP* imp FB ISC* absy FC NOP* absx FD SBC absx FE INC absx FF ISC* absx ";

const uint8_t A78_CYC[256] = {
7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,5,2,5,4,4,4,4,2,4,2,4,4,4,4,4,
2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7 };

enum { M_IMP, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABSX, M_ABSY, M_IND, M_INDX, M_INDY, M_REL };
static const char* MODE_NAMES[] = { "imp", "acc", "imm", "zp", "zpx", "zpy", "abs", "absx", "absy",
                                    "ind", "indx", "indy", "rel" };
static const int MODE_LEN[] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1 };

#define INSNS(X) X(ADC) X(AND) X(ASL) X(BCC) X(BCS) X(BEQ) X(BIT) X(BMI) X(BNE) X(BPL) X(BRK) \
    X(BVC) X(BVS) X(CLC) X(CLD) X(CLI) X(CLV) X(CMP) X(CPX) X(CPY) X(DEC) X(DEX) X(DEY) X(EOR) \
    X(INC) X(INX) X(INY) X(JMP) X(JSR) X(LDA) X(LDX) X(LDY) X(LSR) X(NOP) X(ORA) X(PHA) X(PHP) \
    X(PLA) X(PLP) X(ROL) X(ROR) X(RTI) X(RTS) X(SBC) X(SEC) X(SED) X(SEI) X(STA) X(STX) X(STY) \
    X(TAX) X(TAY) X(TSX) X(TXA) X(TXS) X(TYA) X(JAM) X(SLO) X(RLA) X(SRE) X(RRA) X(SAX) X(LAX) \
    X(DCP) X(ISC) X(ANC) X(ALR) X(ARR) X(ANE) X(LXA) X(SBX) X(SHA) X(SHX) X(SHY) X(TAS) X(LAS)
#define ENUM(n) I_##n,
#define NAME(n) #n,
enum { INSNS(ENUM) I_COUNT };
static const char* INS_NAMES[] = { INSNS(NAME) };

struct OpInfo { uint8_t ins, mode, illegal, penalty; };
static OpInfo OP[256];
static bool g_table_ready = false;

static void build_table() {
    if (g_table_ready) return;
    const char* p = ROWS;
    char code[4], mn[8], md[8];
    int n;
    while (std::sscanf(p, "%3s %7s %7s%n", code, mn, md, &n) == 3) {
        int c = (int)std::strtol(code, nullptr, 16), i;
        OpInfo* o = &OP[c];
        size_t L = std::strlen(mn);
        o->illegal = mn[L - 1] == '*';
        if (o->illegal) mn[--L] = 0;
        for (i = 0; i < I_COUNT; i++) if (!std::strcmp(INS_NAMES[i], mn)) break;
        if (i == I_COUNT) { std::fprintf(stderr, "bad mnemonic %s\n", mn); std::exit(1); }
        o->ins = (uint8_t)i;
        for (i = 0; i < 13; i++) if (!std::strcmp(MODE_NAMES[i], md)) break;
        if (i == 13) { std::fprintf(stderr, "bad mode %s\n", md); std::exit(1); }
        o->mode = (uint8_t)i;
        int in = o->ins;
        o->penalty = (in == I_ORA || in == I_AND || in == I_EOR || in == I_ADC || in == I_SBC ||
                      in == I_CMP || in == I_LDA || in == I_LDX || in == I_LDY || in == I_LAX ||
                      in == I_LAS || in == I_NOP);
        p += n;
    }
    g_table_ready = true;
}

// ---- what the tracer measures ----
enum { R_OP = 1, R_ARG = 2, R_READ = 4, R_JTGT = 8, R_DMA = 16 };
struct A78Trace {
    std::vector<uint8_t> cov;             // a flag set per ROM byte
    uint8_t ramexec[0x10000];             // CPU addresses run outside ROM
    uint64_t ramexec_n = 0, smc_n = 0;
    uint32_t smc_first = 0xFFFFFFFF;
    std::map<std::string, uint64_t> edges; // "k from -> to"
    uint64_t insn_n = 0, illegal_n = 0, comp_rts = 0, dropped = 0;
    uint64_t bank_writes = 0, bank_changes = 0, bank_from_window = 0, bank_hist[16] = {0};
    uint64_t rom_writes = 0, open_rw = 0;
    uint64_t io_r[0x40] = {0}, io_w[0x40] = {0}, riot_r[0x20] = {0}, riot_w[0x20] = {0};
    uint64_t pokey_r[16] = {0}, pokey_w[16] = {0};
    uint64_t ops[256] = {0};
    uint64_t nmi_max_frame = 0, nmi_this_frame = 0, dli_frames = 0;
    uint64_t dma_lines = 0, dma_ticks = 0;
    int int_depth = 0, int_depth_max = 0;
    uint8_t jmark[256]; uint16_t jval[256];   // a shadow of the JSRs' return addresses
    std::map<uint16_t, uint64_t> nmi_at;     // where the main program was at an NMI
    A78Trace() { std::memset(ramexec, 0, sizeof ramexec); std::memset(jmark, 0, sizeof jmark); }
};

static std::string loc(const A78& c, uint16_t a) {
    char b[32];
    if (a >= c.win_lo && a < c.win_hi) std::snprintf(b, sizeof b, "B%u:%04X", c.bank, a);
    else if (c.roff[a >> 11] >= 0) std::snprintf(b, sizeof b, "F:%04X", a);
    else std::snprintf(b, sizeof b, "RAM:%04X", a);
    return b;
}
static inline int32_t rom_at(const A78& c, uint16_t a) {
    int32_t o = c.roff[a >> 11];
    return o < 0 ? -1 : o + (a & 0x7FF);
}
static void edge(A78& c, char k, uint16_t from, uint16_t to) {
    c.tr->edges[std::string(1, k) + " " + loc(c, from) + " -> " + loc(c, to)]++;
}

// ---- the mappers ----
static uint8_t g_zero[0x800], g_ff[0x800], g_sink[0x800];

static void page_rom(A78& c, int p, uint32_t off) {
    if (off + 0x800 > c.romsize) { c.rd[p] = g_ff; c.roff[p] = -1; return; }
    c.rd[p] = c.rom + off; c.roff[p] = (int32_t)off;
}

static void map_update(A78& c) {
    for (int p = 0; p < 32; p++) { c.rd[p] = nullptr; c.wr[p] = nullptr; c.roff[p] = -1; }
    c.rd[1] = c.rd[2] = g_zero; c.wr[1] = c.wr[2] = g_sink;            // $0800-$17FF open
    c.rd[3] = c.wr[3] = c.ram; c.rd[4] = c.wr[4] = c.ram + 0x800;      // $1800-$27FF
    for (int p = 5; p < 8; p++) { c.rd[p] = g_zero; c.wr[p] = g_sink; } // $2800-$3FFF open
    for (int p = 8; p < 32; p++) {
        uint32_t A = p * 0x800;
        switch (c.mapper) {
        case MAP_ROM: case MAP_POKEY: case MAP_MRAM: {
            if (c.mapper != MAP_ROM && A < 0x8000) break;                // the slow path
            uint32_t base = 0x10000 - c.romsize;
            if (A >= base) page_rom(c, p, A - base); else c.rd[p] = g_ff;
            break; }
        case MAP_SG: case MAP_SG_POKEY: case MAP_SG_RAM:
            if (A < 0x8000) {
                if (c.mapper == MAP_SG) page_rom(c, p, (c.mask - 1) * 0x4000 + (A & 0x3FFF));
                else if (c.mapper == MAP_SG_RAM) { c.rd[p] = c.wr[p] = c.cram + (A - 0x4000); }
            } else if (A < 0xC000) page_rom(c, p, (c.bank & c.mask) * 0x4000 + (A & 0x3FFF));
            else page_rom(c, p, c.mask * 0x4000 + (A & 0x3FFF));
            break;
        case MAP_SG9:
            if (A < 0x8000) page_rom(c, p, A & 0x3FFF);
            else if (A < 0xC000) page_rom(c, p, c.bank * 0x4000 + (A & 0x3FFF));
            else page_rom(c, p, (c.mask + 1) * 0x4000 + (A & 0x3FFF));
            break;
        case MAP_ABS:
            if (A < 0x8000) page_rom(c, p, (c.bank & 1) * 0x4000 + (A & 0x3FFF));
            else page_rom(c, p, A);
            break;
        case MAP_ACT:
            if (A >= 0xA000 && A < 0xE000) page_rom(c, p, (c.bank & 7) * 0x4000 + (A - 0xA000));
            else {
                int k = (A & 0xE000) == 0x4000 ? 13 : (A & 0xE000) == 0x6000 ? 12
                      : (A & 0xE000) == 0x8000 ? 15 : 14;
                page_rom(c, p, k * 0x2000 + (A & 0x1FFF));
            }
            break;
        }
    }
}

int a78_mapper_by_name(const char* n) {
    static const char* names[] = { "rom", "pokey", "mram", "sg", "sg_pokey", "sg_ram", "sg9", "abs", "act" };
    for (int i = 0; i < 9; i++) if (!std::strcmp(n, names[i])) return i;
    return -1;
}

// A write to the cartridge: bank switching, POKEY, cartridge RAM.
static void cart_w(A78& c, uint16_t a, uint8_t v) {
    uint32_t nb = c.bank;
    bool sw = false;
    switch (c.mapper) {
    case MAP_POKEY: case MAP_SG_POKEY:
        if (a < 0x8000) {
            c.pokey[a & 0x0F] = v;
            if (c.tr) c.tr->pokey_w[a & 0x0F]++;
            if (c.snd_log && c.snd_n < A78_SND_EVENTS) c.snd_ev[c.snd_n++] = { c.cyc, (uint8_t)(0x40 | (a & 0x0F)), v };
            return;
        }
        if (c.mapper == MAP_SG_POKEY && a < 0xC000) { nb = v & c.mask; sw = true; }
        break;
    case MAP_MRAM:
        if (a < 0x8000) { c.cram[(a - 0x4000) & 0x3EFF] = v; return; }
        break;
    case MAP_SG: case MAP_SG_RAM:
        if (a >= 0x8000 && a < 0xC000) { nb = v & c.mask; sw = true; }
        break;
    case MAP_SG9:
        if (a >= 0x8000 && a < 0xC000) { nb = (v & c.mask) + 1; sw = true; }
        break;
    case MAP_ABS:
        if (a == 0x8000) { sw = true; if (v & 1) nb = 0; else if (v & 2) nb = 1; }
        break;
    case MAP_ACT:
        if (a >= 0xE000) { nb = a & 7; sw = true; }
        break;
    }
    if (!sw) { if (c.tr) c.tr->rom_writes++; return; }
    if (c.tr) {
        c.tr->bank_writes++;
        if (nb != c.bank) {
            c.tr->bank_changes++;
            c.tr->bank_hist[nb & 15]++;
            if (c.pc >= c.win_lo && c.pc < c.win_hi) c.tr->bank_from_window++;
        }
    }
    if (nb != c.bank) { c.bank = nb; map_update(c); }
}

static uint8_t pokey_r(A78& c, uint16_t a) {
    int r = a & 0x0F;
    if (c.tr) c.tr->pokey_r[r]++;
    switch (r) {
    case 0x0A: {                          // RANDOM: a function of time, the same both ways
        uint64_t x = c.cyc / TICKS_CYC * 0x9E3779B97F4A7C15ull;
        return (uint8_t)(x >> 56);
    }
    case 0x0E: return 0xFF;               // IRQST: nothing pending
    case 0x0F: return 0xFF;               // SKSTAT
    default: return 0xFF;
    }
}

uint8_t a78_rd_slow(A78& c, uint16_t a) {
    if (a < 0x800) return a78_io_r(c, a);
    switch (c.mapper) {
    case MAP_POKEY: case MAP_SG_POKEY:
        if (a >= 0x4000 && a < 0x8000) return pokey_r(c, a);
        break;
    case MAP_MRAM:
        if (a >= 0x4000 && a < 0x8000) return c.cram[(a - 0x4000) & 0x3EFF];
        break;
    }
    if (c.tr) c.tr->open_rw++;
    return 0;
}

void a78_wr_slow(A78& c, uint16_t a, uint8_t v) {
    if (a < 0x800) { a78_io_w(c, a, v); return; }
    if (a >= 0x4000) { cart_w(c, a, v); return; }
    if (c.tr) c.tr->open_rw++;
}

// What MARIA's DMA reads: the bus without side effects.
static inline uint8_t dma_rd(A78& c, uint16_t a) {
    const uint8_t* p = c.rd[a >> 11];
    if (p) {
        if (c.tr) { int32_t o = c.roff[a >> 11]; if (o >= 0) c.tr->cov[o + (a & 0x7FF)] |= R_DMA; }
        return p[a & 0x7FF];
    }
    if (a < 0x200 && (a & 0xFF) >= 0x40) return c.ram[0x800 + a];
    if (c.mapper == MAP_MRAM && a >= 0x4000 && a < 0x8000) return c.cram[(a - 0x4000) & 0x3EFF];
    return 0;
}

// ---- the RIOT ----
static uint8_t riot_timer(A78& c, bool clear) {
    uint64_t e = (c.cyc - c.timer_t0) / TICKS_CYC;
    uint64_t n = e >> c.timer_shift;
    if (n <= c.timer_v) return (uint8_t)(c.timer_v - n);
    uint64_t after = e - ((uint64_t)(c.timer_v + 1) << c.timer_shift);
    if (clear) c.timer_flag = 2;          // the flag read away
    return (uint8_t)(0xFF - after);
}

// ---- the TIA's and MARIA's registers, the RIOT, the RAM mirrors: $0000-$07FF ----
uint8_t a78_io_r(A78& c, uint16_t a) {
    uint16_t lo = a & 0xFF, page = a >> 8;
    if (page < 4 && lo < 0x40) {
        if (lo < 0x20) {                      // TIA: the inputs
            c.cyc += SLOW_EXTRA;
            if (c.tr) c.tr->io_r[lo]++;
            uint8_t b = c.buttons;
            switch (lo & 0x0F) {
            case 0x08: return (uint8_t)((b & 0x02) << 6);
            case 0x09: return (uint8_t)((b & 0x08) << 4);
            case 0x0A: return (uint8_t)((b & 0x01) << 7);
            case 0x0B: return (uint8_t)((b & 0x04) << 5);
            case 0x0C: return ((b & 0x0A) && c.one_button0) ? 0x00 : 0x80;
            case 0x0D: return ((b & 0x05) && c.one_button1) ? 0x00 : 0x80;
            case 0x0E: case 0x0F: return 0xFF;
            default: return 0x00;
            }
        }
        if (c.tr) c.tr->io_r[lo]++;
        return (lo & 0x1F) == 0x08 ? c.mstat : 0x00;   // MARIA: MSTAT only
    }
    if (page < 2) return c.ram[0x800 + a];              // $40-$FF, $140-$1FF
    if (page < 4) {
        if (!(lo & 0x80)) { if (c.tr) c.tr->open_rw++; return 0; }
        c.cyc += SLOW_EXTRA;
        int r = lo & 0x1F;
        if (c.tr) c.tr->riot_r[r]++;
        if (r & 4) {
            if (r & 1) {                                // interrupt flags
                uint64_t uf = c.timer_t0 + (((uint64_t)c.timer_v + 1) << c.timer_shift) * TICKS_CYC;
                return (c.cyc >= uf && c.timer_flag != 2) ? 0x80 : 0x00;
            }
            return riot_timer(c, true);
        }
        switch (r & 3) {
        case 0: return (uint8_t)((c.ora & c.ddra) | (c.joy & ~c.ddra));
        case 1: return c.ddra;
        case 2: return (uint8_t)((c.orb & c.ddrb) | (c.console & ~c.ddrb));
        default: return c.ddrb;
        }
    }
    if (page < 6 && (lo & 0x80)) { c.cyc += SLOW_EXTRA; return c.riot_ram[lo & 0x7F]; }
    if (c.tr) c.tr->open_rw++;
    return 0;
}

static void maria_w(A78& c, uint8_t r, uint8_t v);

void a78_io_w(A78& c, uint16_t a, uint8_t v) {
    uint16_t lo = a & 0xFF, page = a >> 8;
    if (page < 4 && lo < 0x40) {
        if (lo < 0x20) {                      // TIA: INPTCTRL until locked, the sound
            c.cyc += SLOW_EXTRA;
            if (c.tr) c.tr->io_w[lo]++;
            if (!c.lock) { c.lock = v & 1; c.inptctrl = v; }
            c.tia[lo] = v;
            if (lo >= 0x15 && lo <= 0x1A && c.snd_log && c.snd_n < A78_SND_EVENTS)
                c.snd_ev[c.snd_n++] = { c.cyc, (uint8_t)lo, v };
            return;
        }
        if (c.tr) c.tr->io_w[lo]++;
        maria_w(c, lo & 0x1F, v);
        return;
    }
    if (page < 2) { c.ram[0x800 + a] = v; return; }
    if (page < 4) {
        if (!(lo & 0x80)) { if (c.tr) c.tr->open_rw++; return; }
        c.cyc += SLOW_EXTRA;
        int r = lo & 0x1F;
        if (c.tr) c.tr->riot_w[r]++;
        if (r & 4) {
            if (r & 0x10) {                             // TIM1T, TIM8T, TIM64T, T1024T
                static const uint8_t sh[4] = { 0, 3, 6, 10 };
                c.timer_v = v; c.timer_shift = sh[r & 3]; c.timer_t0 = c.cyc; c.timer_flag = 0;
            }
            return;
        }
        switch (r & 3) {
        case 0: c.ora = v; break;
        case 1: c.ddra = v; break;
        case 2: c.orb = v; break;
        default: c.ddrb = v; break;
        }
        uint8_t pb = (uint8_t)((c.orb & c.ddrb) | ~c.ddrb);
        c.one_button0 = pb >> 2 & 1;
        c.one_button1 = pb >> 4 & 1;
        return;
    }
    if (page < 6 && (lo & 0x80)) { c.cyc += SLOW_EXTRA; c.riot_ram[lo & 0x7F] = v; return; }
    if (c.tr) c.tr->open_rw++;
}

// ---- MARIA ----
static void maria_w(A78& c, uint8_t r, uint8_t v) {
    if (r & 3) c.pal[r] = v;
    switch (r) {
    case 0x00: for (int i = 0; i < 8; i++) c.pal[4 * i] = v; break;
    case 0x04: {                                        // WSYNC: halt to the next line
        c.n_wsync++;
        uint64_t t = c.line_start + LINE_TICKS;
        while (t + TICKS_CYC <= c.cyc) t += LINE_TICKS;
        c.wsync_to = t;
        if (c.cyc < t) c.cyc = t;
        break; }
    case 0x0C: c.dpp = (uint16_t)((c.dpp & 0x00FF) | v << 8); break;
    case 0x10: c.dpp = (uint16_t)((c.dpp & 0xFF00) | v); break;
    case 0x14: c.charbase = v; break;
    case 0x1C:
        c.ctrl = v;
        c.colorkill = v >> 7;
        if (((v >> 5) & 3) == 2) c.dmaon = 1;
        else if (((v >> 5) & 3) == 3) c.dmaon = 0;
        c.cwidth = v >> 4 & 1;
        c.kangaroo = v >> 2 & 1;
        c.rm = v & 3;
        break;
    }
}

static inline bool is_holey(const A78& c, uint16_t a) {
    return ((c.holey & 2) && (a & 0x9000) == 0x9000) || ((c.holey & 1) && (a & 0x8800) == 0x8800);
}

static inline int lram_w(A78& c, uint16_t addr, uint8_t off, uint8_t pal) {
    uint8_t d = dma_rd(c, addr);
    uint8_t* L = c.lram[c.abuf];
    pal <<= 2;
    if (c.wmode) {
        uint8_t k = (pal & 0x10) | (d & 0x0C) | (d >> 6);
        if (((k & 3) || c.kangaroo) && off < 160) L[off] = k;
        off++;
        k = (pal & 0x10) | ((d & 0x03) << 2) | ((d & 0x30) >> 4);
        if (((k & 3) || c.kangaroo) && off < 160) L[off] = k;
        return 2;
    }
    for (int i = 0; i < 4; i++, off++) {
        uint8_t k = pal | ((d >> (6 - 2 * i)) & 3);
        if (((k & 3) || c.kangaroo) && off < 160) L[off] = k;
    }
    return 4;
}

// One line's DMA and the display of the line before; returns MARIA's cycles.
static int draw_scanline(A78& c, int fl) {
    int mc = 0;
    if (c.dmaon) {
        mc = 16;
        uint16_t dl = c.dl;
        while ((dma_rd(c, dl + 1) & 0x5F) != 0 && mc < 426) {
            uint16_t gfx; int width; uint8_t hpos, pal; bool ind = false;
            if (!(dma_rd(c, dl + 1) & 0x1F)) {          // the extended header
                gfx = (uint16_t)(dma_rd(c, dl + 2) << 8 | dma_rd(c, dl));
                width = ((dma_rd(c, dl + 3) ^ 0xFF) & 0x1F) + 1;
                hpos = dma_rd(c, dl + 4);
                pal = dma_rd(c, dl + 3) >> 5;
                c.wmode = dma_rd(c, dl + 1) >> 7;
                ind = dma_rd(c, dl + 1) >> 5 & 1;
                dl += 5; mc += 10;
            } else {
                gfx = (uint16_t)(dma_rd(c, dl + 2) << 8 | dma_rd(c, dl));
                width = ((dma_rd(c, dl + 1) ^ 0xFF) & 0x1F) + 1;
                hpos = dma_rd(c, dl + 3);
                pal = dma_rd(c, dl + 1) >> 5;
                dl += 4; mc += 8;
            }
            for (int x = 0; x < width; x++) {
                if (mc >= 426) break;
                if (ind) {
                    uint8_t ch = dma_rd(c, (uint16_t)(gfx + x));
                    uint16_t da = (uint16_t)(((c.charbase << 8) | ch) + (c.offset << 8));
                    if (is_holey(c, da)) continue;
                    mc += 3;
                    if (c.cwidth) {
                        hpos += lram_w(c, da, hpos, pal);
                        hpos += lram_w(c, (uint16_t)(da + 1), hpos, pal);
                        mc += 6;
                    } else {
                        hpos += lram_w(c, da, hpos, pal);
                        mc += 3;
                    }
                } else {
                    uint16_t da = (uint16_t)(gfx + x + (c.offset << 8));
                    if (is_holey(c, da)) continue;
                    hpos += lram_w(c, da, hpos, pal);
                    mc += 3;
                }
            }
        }
        if (c.offset == 0) {
            mc += 6;
            if (dma_rd(c, c.dll + 3) & 0x80) mc += 17;
        }
    }
    // the line DMA'd before is shown
    c.abuf ^= 1;
    uint8_t* L = c.lram[c.abuf];
    uint8_t* out = c.fb[fl];
    for (int i = 0; i < 160; i++) {
        uint8_t k = L[i], d0, d1;
        switch (c.rm) {
        case 2:
            d0 = (k & 0x10) | (k & 0x02) | ((k >> 3) & 1);
            d1 = (k & 0x10) | ((k << 1) & 0x02) | ((k >> 2) & 1);
            break;
        case 3:
            d0 = (k & 0x1C) | (k & 0x02);
            d1 = (k & 0x1C) | ((k << 1) & 0x02);
            break;
        default: d0 = d1 = k; break;
        }
        uint8_t p0 = c.pal[d0], p1 = c.pal[d1];
        if (c.colorkill) { p0 &= 0x0F; p1 &= 0x0F; }
        out[2 * i] = p0; out[2 * i + 1] = p1;
        L[i] = 0;
    }
    return mc;
}

static inline void load_dll(A78& c) {
    c.dl = (uint16_t)(dma_rd(c, c.dll + 1) << 8 | dma_rd(c, c.dll + 2));
    uint8_t h = dma_rd(c, c.dll);
    c.offset = h & 0x0F;
    c.holey = (h & 0x60) >> 5;
    c.dli = h >> 7;
}

static void dma_event(A78& c) {
    int fl = c.line;
    uint64_t at = c.ev_at;
    if (fl == 16 && c.dmaon) { c.dll = c.dpp; load_dll(c); }
    if (fl > 15 && fl < c.lines - 5) {
        bool was_on = c.dmaon;
        int mc = draw_scanline(c, fl);
        if (was_on) {
            if (c.tr) { c.tr->dma_lines++; c.tr->dma_ticks += mc; }
            if (mc >= 426) {                            // no time left: halted to the next line
                c.n_dma_full++;
                uint64_t t = c.line_start + LINE_TICKS;
                if (c.cyc < t) c.cyc = t;
                if (c.wsync_to < t) c.wsync_to = t;
            } else if (c.wsync_to <= at) {
                c.cyc += (uint64_t)((mc + 3) / 4) * TICKS_CYC;
            }
        }
    }
    if (fl > 16 && fl < c.lines - 5 && c.dmaon) {
        if (c.offset == 0) { c.dll += 3; load_dll(c); }
        else c.offset--;
    }
    if (c.dli) {
        c.dli = 0;
        c.nmi_pending = 1;
        if (c.tr) c.tr->nmi_this_frame++;
    }
}

// ---- events: the line start, MARIA's DMA, the frame ----
void a78_recalc(A78& c) { c.next = c.nmi_pending ? 0 : c.ev_at; }

// Returns false when a flat interpreter is to stop here (the frame event).
static bool events(A78& c) {
    while (c.cyc >= c.ev_at) {
        if (c.ev_phase == 1) {
            dma_event(c);
            c.ev_phase = 0;
            c.ev_at = c.line_start + LINE_TICKS;
            continue;
        }
        c.line_start = c.ev_at;
        c.ev_phase = 1;
        c.ev_at = c.line_start + DMA_START;
        if (++c.line >= c.lines) c.line = 0;
        if (c.line == 16) c.mstat = 0x00;
        if (c.line == c.lines - 5) c.mstat = 0x80;
        if (c.line && c.line_hook) { a78_recalc(c); c.line_hook(c); }
        if (c.line && !c.natives && c.line_stop) { a78_recalc(c); return false; }
        if (c.line == 0) {
            c.frame++;
            if (c.snd_log && c.snd_n < A78_SND_EVENTS) c.snd_ev[c.snd_n++] = { c.line_start, 0xFF, 0 };
            if (c.tr) {
                if (c.tr->nmi_this_frame > c.tr->nmi_max_frame) c.tr->nmi_max_frame = c.tr->nmi_this_frame;
                if (c.tr->nmi_this_frame) c.tr->dli_frames++;
                c.tr->nmi_this_frame = 0;
            }
            a78_recalc(c);
            if (c.frame_hook) c.frame_hook(c);           // may throw A78Stop
            if (!c.natives && (c.flat_stop || c.line_stop)) { a78_recalc(c); return false; }
        }
    }
    a78_recalc(c);
    return true;
}

static uint16_t take_interrupt(A78& c, uint16_t at) {
    if (!c.nmi_pending) return 0;
    c.nmi_pending = 0;
    c.n_nmi++;
    if (c.tr) {
        if (c.tr->int_depth == 0) c.tr->nmi_at[at]++;
        if (++c.tr->int_depth > c.tr->int_depth_max) c.tr->int_depth_max = c.tr->int_depth;
    }
    a78_push(c, at >> 8); a78_push(c, at & 0xFF); a78_push(c, a78_p(c));
    c.fi = 1;                                           // NMOS: D is left as it was
    c.cyc += 7 * TICKS_CYC;
    a78_recalc(c);
    return a78_rd16(c, 0xFFFA);
}

// ---- native functions, by (slot, address) ----
static std::vector<A78Fn> g_fixed(0xC000, nullptr);          // $4000-$FFFF outside the window
static std::vector<std::vector<A78Fn>> g_win(16, std::vector<A78Fn>(0x4000, nullptr));

void a78_register(int slot, uint16_t addr, A78Fn fn) {
    if (slot < 0) g_fixed[addr - 0x4000] = fn;
    else g_win[slot & 15][addr & 0x3FFF] = fn;
}

A78Fn a78_lookup(A78& c, uint16_t pc) {
    if (!c.natives || pc < 0x4000) return nullptr;
    if (pc >= c.win_lo && pc < c.win_hi) return g_win[c.bank & 15][pc & 0x3FFF];
    if (c.roff[pc >> 11] < 0) return nullptr;
    return g_fixed[pc - 0x4000];
}

// Plain RAM, where reading has no side effect: zero page, the stack, $1800-$27FF, cartridge RAM.
static inline bool is_ram(const A78& c, uint16_t a) {
    if (a < 0x200) return (a & 0xFF) >= 0x40;
    if (a < 0x800) return false;
    return c.wr[a >> 11] != nullptr && c.wr[a >> 11] != g_sink;
}

// A transfer to a trampoline in RAM: the boundary before its instruction,
// and if that is a JMP (abs) or JMP (ind), its target in t -- the caller
// goes on there.  Otherwise t is the RAM address itself.  False: unwinding.
bool a78_tramp(A78& c, uint16_t a, uint16_t& t) {
    if (c.cyc >= c.next && !a78_poll(c, a)) return false;
    uint8_t op = a78_rd(c, a);
    t = a;
    if (op == 0x4C) { A78_HIST(c, a); c.cyc += 3 * TICKS_CYC; t = a78_rd16(c, (uint16_t)(a + 1)); }
    else if (op == 0x6C) { A78_HIST(c, a); c.cyc += 5 * TICKS_CYC; t = a78_ind16(c, a78_rd16(c, (uint16_t)(a + 1))); }
    return true;
}

bool a78_poll(A78& c, uint16_t at) {
    for (;;) {
        events(c);
        uint8_t s0 = c.s;
        uint16_t h = take_interrupt(c, at);
        if (!h) return true;
        a78_call(c, h);
        if (c.resume != at || c.s != s0) { c.n_unwind++; return false; }
    }
}

void a78_jam(A78& c, uint16_t at) {
    c.jammed = 1;
    std::fprintf(stderr, "a78: the CPU jammed at %04X (frame %llu)\n", at, (unsigned long long)c.frame);
    throw A78Stop();
}

// ---- the interpreter ----
enum Step { ST_NEXT, ST_RTS, ST_RTI };

static inline void mark_exec(A78& c, uint16_t pc, int len) {
    A78Trace* t = c.tr;
    t->insn_n++;
    int32_t o = rom_at(c, pc);
    if (o < 0) { t->ramexec[pc] = 1; t->ramexec_n++; return; }
    for (int i = 0; i < len; i++) {
        int32_t q = rom_at(c, (uint16_t)(pc + i));
        if (q >= 0) t->cov[q] |= i ? R_ARG : R_OP;
    }
}
static inline void mark_jtgt(A78& c, uint16_t a) {
    int32_t o = rom_at(c, a);
    if (o >= 0) c.tr->cov[o] |= R_JTGT;
}
static void drop_check(A78& c, int upto) {      // a pull or TXS discarding JSR return addresses
    for (int s = c.s + 1; s <= upto && s < 256; s++)
        if (c.tr->jmark[s]) { c.tr->jmark[s] = 0; c.tr->dropped++; }
}

static Step step(A78& c) {
    const uint16_t pc0 = c.pc;
    A78_HIST(c, pc0);
    const uint8_t opc = a78_rd(c, pc0);
    const OpInfo& o = OP[opc];
    uint16_t pc = pc0 + 1, ea = 0;
    unsigned extra = 0;                 // cycles: page crossings, taken branches
    switch (o.mode) {
    case M_IMM: ea = pc++; break;
    case M_ZP: ea = a78_rd(c, pc++); break;
    case M_ZPX: ea = (uint8_t)(a78_rd(c, pc++) + c.x); break;
    case M_ZPY: ea = (uint8_t)(a78_rd(c, pc++) + c.y); break;
    case M_ABS: ea = a78_rd16(c, pc); pc += 2; break;
    case M_ABSX: { uint16_t b = a78_rd16(c, pc); pc += 2; ea = b + c.x;
        if (o.penalty && ((b & 0xFF) + c.x) > 0xFF) extra = 1;
        break; }
    case M_ABSY: { uint16_t b = a78_rd16(c, pc); pc += 2; ea = b + c.y;
        if (o.penalty && ((b & 0xFF) + c.y) > 0xFF) extra = 1;
        break; }
    case M_IND: { uint16_t p = a78_rd16(c, pc); pc += 2; ea = a78_ind16(c, p); break; }
    case M_INDX: ea = a78_zpw(c, (uint8_t)(a78_rd(c, pc++) + c.x)); break;
    case M_INDY: { uint16_t b = a78_zpw(c, a78_rd(c, pc++)); ea = b + c.y;
        if (o.penalty && ((b & 0xFF) + c.y) > 0xFF) extra = 1;
        break; }
    case M_REL: { int8_t d = (int8_t)a78_rd(c, pc++); ea = pc + d; break; }
    }
    if (c.tr) {
        mark_exec(c, pc0, 1 + MODE_LEN[o.mode]);
        c.tr->ops[opc]++;
        if (o.illegal) c.tr->illegal_n++;
        bool reads = o.mode != M_IMM && o.mode != M_REL && o.mode != M_IMP && o.mode != M_ACC &&
                     o.ins != I_JMP && o.ins != I_JSR && o.ins != I_STA && o.ins != I_STX &&
                     o.ins != I_STY && o.ins != I_SAX;
        if (reads) { int32_t q = rom_at(c, ea); if (q >= 0) c.tr->cov[q] |= R_READ; }
        if (!reads && o.mode != M_IMM && o.mode != M_REL && o.mode != M_IMP && o.mode != M_ACC &&
            o.ins != I_JMP && o.ins != I_JSR && c.tr->ramexec[ea]) {
            c.tr->smc_n++;
            if (c.tr->smc_first == 0xFFFFFFFF) c.tr->smc_first = pc0;
        }
    }
    c.cyc += (A78_CYC[opc] + extra) * TICKS_CYC;
    c.n_interp++;
    Step st = ST_NEXT;
    uint8_t v;
    auto rd = [&](uint16_t a) -> uint8_t { return o.mode == M_ZP || o.mode == M_ZPX || o.mode == M_ZPY
                                              ? a78_zrd(c, (uint8_t)a) : a78_rd(c, a); };
    auto wr = [&](uint16_t a, uint8_t x) { if (o.mode == M_ZP || o.mode == M_ZPX || o.mode == M_ZPY)
                                               a78_zwr(c, (uint8_t)a, x); else a78_wr(c, a, x); };
    auto br = [&](bool cond) {
        if (cond) { c.cyc += TICKS_CYC; if ((ea & 0xFF00) != (pc & 0xFF00)) c.cyc += TICKS_CYC; pc = ea; }
    };
#define RMW(expr) do { if (o.mode == M_ACC) { v = c.a; c.a = expr; } \
                       else { v = rd(ea); wr(ea, expr); } } while (0)
    switch (o.ins) {
    case I_LDA: a78_nz(c, c.a = rd(ea)); break;
    case I_LDX: a78_nz(c, c.x = rd(ea)); break;
    case I_LDY: a78_nz(c, c.y = rd(ea)); break;
    case I_LAX: a78_nz(c, c.a = c.x = rd(ea)); break;
    case I_STA: wr(ea, c.a); break;
    case I_STX: wr(ea, c.x); break;
    case I_STY: wr(ea, c.y); break;
    case I_SAX: wr(ea, c.a & c.x); break;
    case I_ADC: a78_adc(c, rd(ea)); break;
    case I_SBC: a78_sbc(c, rd(ea)); break;
    case I_AND: a78_nz(c, c.a &= rd(ea)); break;
    case I_ORA: a78_nz(c, c.a |= rd(ea)); break;
    case I_EOR: a78_nz(c, c.a ^= rd(ea)); break;
    case I_CMP: a78_cmp(c, c.a, rd(ea)); break;
    case I_CPX: a78_cmp(c, c.x, rd(ea)); break;
    case I_CPY: a78_cmp(c, c.y, rd(ea)); break;
    case I_BIT: a78_bit(c, rd(ea)); break;
    case I_ASL: RMW(a78_asl(c, v)); break;
    case I_LSR: RMW(a78_lsr(c, v)); break;
    case I_ROL: RMW(a78_rol(c, v)); break;
    case I_ROR: RMW(a78_ror(c, v)); break;
    case I_INC: RMW(a78_nz(c, (uint8_t)(v + 1))); break;
    case I_DEC: RMW(a78_nz(c, (uint8_t)(v - 1))); break;
    case I_SLO: v = rd(ea); v = a78_asl(c, v); wr(ea, v); a78_nz(c, c.a |= v); break;
    case I_RLA: v = rd(ea); v = a78_rol(c, v); wr(ea, v); a78_nz(c, c.a &= v); break;
    case I_SRE: v = rd(ea); v = a78_lsr(c, v); wr(ea, v); a78_nz(c, c.a ^= v); break;
    case I_RRA: v = rd(ea); v = a78_ror(c, v); wr(ea, v); a78_adc(c, v); break;
    case I_DCP: v = (uint8_t)(rd(ea) - 1); wr(ea, v); a78_cmp(c, c.a, v); break;
    case I_ISC: v = (uint8_t)(rd(ea) + 1); wr(ea, v); a78_sbc(c, v); break;
    case I_ANC: a78_nz(c, c.a &= rd(ea)); c.fc = c.fn; break;
    case I_ALR: c.a &= rd(ea); c.a = a78_lsr(c, c.a); break;
    case I_ARR: a78_arr(c, rd(ea)); break;
    case I_SBX: a78_sbx(c, rd(ea)); break;
    case I_ANE: a78_nz(c, c.a = (uint8_t)((c.a | 0xEE) & c.x & rd(ea))); break;
    case I_LXA: a78_nz(c, c.a = c.x = (uint8_t)((c.a | 0xEE) & rd(ea))); break;
    case I_SHA: wr(ea, (uint8_t)(c.a & c.x & ((ea >> 8) + 1))); break;
    case I_SHX: wr(ea, (uint8_t)(c.x & ((ea >> 8) + 1))); break;
    case I_SHY: wr(ea, (uint8_t)(c.y & ((ea >> 8) + 1))); break;
    case I_TAS: c.s = c.a & c.x; wr(ea, (uint8_t)(c.s & ((ea >> 8) + 1))); break;
    case I_LAS: a78_nz(c, c.a = c.x = c.s = rd(ea) & c.s); break;
    case I_BPL: br(!c.fn); break;
    case I_BMI: br(c.fn); break;
    case I_BVC: br(!c.fv); break;
    case I_BVS: br(c.fv); break;
    case I_BCC: br(!c.fc); break;
    case I_BCS: br(c.fc); break;
    case I_BNE: br(!c.fz); break;
    case I_BEQ: br(c.fz); break;
    case I_JMP:
        // an indirect JMP, or any JMP run from RAM (a trampoline the game writes there)
        if (c.tr) { if (o.mode == M_IND || rom_at(c, pc0) < 0) edge(c, 'i', pc0, ea); mark_jtgt(c, ea); }
        pc = ea; break;
    case I_JSR: {
        uint16_t ret = pc - 1;
        a78_push(c, ret >> 8); a78_push(c, ret & 0xFF);
        if (c.tr) { uint8_t hs = c.s + 1; c.tr->jmark[hs] = 1; c.tr->jval[hs] = ret; mark_jtgt(c, ea); }
        pc = ea; break; }
    case I_RTS: {
        uint8_t hs = c.s + 1;
        uint8_t lo = a78_pull(c), hi = a78_pull(c);
        uint16_t ret = (uint16_t)(lo | hi << 8);
        if (c.tr) {
            if (!c.tr->jmark[hs] || c.tr->jval[hs] != ret) {
                c.tr->comp_rts++; edge(c, 'r', pc0, (uint16_t)(ret + 1)); mark_jtgt(c, (uint16_t)(ret + 1));
            }
            c.tr->jmark[hs] = 0;
        }
        c.rti = 0;
        pc = (uint16_t)(ret + 1); st = ST_RTS; break; }
    case I_RTI: {
        a78_setp(c, a78_pull(c));
        uint8_t lo = a78_pull(c), hi = a78_pull(c);
        pc = (uint16_t)(lo | hi << 8);
        if (c.tr && c.tr->int_depth) c.tr->int_depth--;
        c.rti = 1;
        a78_recalc(c);
        st = ST_RTI; break; }
    case I_BRK: {
        c.n_brk++;
        uint16_t ret = pc0 + 2;
        a78_push(c, ret >> 8); a78_push(c, ret & 0xFF); a78_push(c, a78_p(c) | 0x10);
        c.fi = 1;
        pc = a78_rd16(c, 0xFFFE);
        if (c.tr) edge(c, 'b', pc0, pc);
        break; }
    case I_PHA: a78_push(c, c.a); break;
    case I_PHP: a78_push(c, a78_p(c) | 0x10); break;
    case I_PLA: if (c.tr) drop_check(c, c.s + 1); a78_nz(c, c.a = a78_pull(c)); break;
    case I_PLP: if (c.tr) drop_check(c, c.s + 1); a78_setp(c, a78_pull(c)); a78_recalc(c); break;
    case I_TAX: a78_nz(c, c.x = c.a); break;
    case I_TAY: a78_nz(c, c.y = c.a); break;
    case I_TXA: a78_nz(c, c.a = c.x); break;
    case I_TYA: a78_nz(c, c.a = c.y); break;
    case I_TSX: a78_nz(c, c.x = c.s); break;
    case I_TXS:
        if (c.tr) { if (c.x > c.s) drop_check(c, c.x); else for (int s = c.x + 1; s <= c.s; s++) c.tr->jmark[s] = 0; }
        c.s = c.x; break;
    case I_INX: a78_nz(c, ++c.x); break;
    case I_INY: a78_nz(c, ++c.y); break;
    case I_DEX: a78_nz(c, --c.x); break;
    case I_DEY: a78_nz(c, --c.y); break;
    case I_CLC: c.fc = 0; break;
    case I_SEC: c.fc = 1; break;
    case I_CLI: c.fi = 0; break;
    case I_SEI: c.fi = 1; break;
    case I_CLD: c.fd = 0; break;
    case I_SED: c.fd = 1; break;
    case I_CLV: c.fv = 0; break;
    case I_NOP: if (o.mode != M_IMP && o.mode != M_IMM) (void)rd(ea); break;
    case I_JAM: a78_jam(c, pc0);
    }
#undef RMW
    c.pc = pc;
    return st;
}

// The nested interpreter: run from pc until an RTS or RTI takes S above
// `base` (the stack as the caller left it, its return address pushed).
// Native code takes over at every native function entry.
void a78_exec(A78& c, uint16_t pc, int base) {
    c.pc = pc;
    c.n_fallback++;
    for (;;) {
        if (c.cyc >= c.next) {
            const uint16_t at = c.pc;        // an interrupt's handler may run the interpreter too
            if (!a78_poll(c, at)) {
                if ((int)c.s > base) return;
                c.pc = c.resume;
                continue;
            }
            c.pc = at;
        }
        if (A78Fn fn = a78_lookup(c, c.pc)) {
            c.n_native_calls++;
            fn(c);
            if ((int)c.s > base) return;
            c.pc = c.resume;
            continue;
        }
        Step st = step(c);
        if (st != ST_NEXT) {
            c.resume = c.pc;
            if ((int)c.s > base) return;
        }
    }
}

// Deeper than this, a call does not nest: it leaves `target` in c.resume
// and returns, and its callers unwind to the dispatcher at the top, which
// goes on there -- the 6502's stack, which holds every return address, is
// untouched.  (Code that keeps jumping between regions without returning
// would otherwise grow the C stack without bound.)
enum { CALL_DEPTH = 256 };

void a78_call(A78& c, uint16_t target) {
    if (c.depth >= CALL_DEPTH) { c.resume = target; c.rti = 0; c.n_unwind++; return; }
    if (++c.depth > c.max_depth) c.max_depth = c.depth;
    A78Fn fn = a78_lookup(c, target);
    if (!fn && c.natives && is_ram(c, target)) {
        // a JMP the game keeps in RAM (an NMI vector pointing there, a dispatch): taken here
        uint8_t op = a78_rd(c, target);
        if (op == 0x4C || op == 0x6C) {
            uint16_t t;
            if (!a78_tramp(c, target, t)) { c.depth--; return; }
            target = t;
            fn = a78_lookup(c, target);
        }
    }
    if (fn) { c.n_native_calls++; fn(c); }
    else a78_exec(c, target, c.s);
    c.depth--;
}

// ---- the flat interpreter: the reference, no native code, no nesting ----
bool a78_flat_frame(A78& c) {
    for (;;) {
        if (c.jammed) return false;
        if (c.cyc >= c.next) {
            if (!events(c)) return true;            // the frame: stop before delivering
            uint16_t h = take_interrupt(c, c.pc);
            if (h) { c.pc = h; continue; }          // the handler's first boundary polls too
        }
        step(c);
    }
}

// ---- the machine ----
bool a78_load(A78& c, const uint8_t* rom, uint32_t size, int mapper, bool pal) {
    build_table();
    std::memset(g_ff, 0xFF, sizeof g_ff);
    c.rom = rom; c.romsize = size; c.mapper = mapper;
    c.lines = pal ? 313 : 263;
    c.nbanks = size / 0x4000 ? size / 0x4000 : 1;
    c.mask = (c.nbanks & 1) ? c.nbanks - 2 : c.nbanks - 1;
    c.bank = 0;
    switch (mapper) {
    case MAP_SG: case MAP_SG_POKEY: case MAP_SG_RAM: case MAP_SG9: c.win_lo = 0x8000; c.win_hi = 0xC000; break;
    case MAP_ABS: c.win_lo = 0x4000; c.win_hi = 0x8000; break;
    case MAP_ACT: c.win_lo = 0xA000; c.win_hi = 0xE000; break;
    default: c.win_lo = c.win_hi = 0; break;
    }
    map_update(c);
    return true;
}

void a78_reset(A78& c) {
    c.pc = a78_rd16(c, 0xFFFC);
    c.s = 0xFF; c.fi = 1; c.fd = 0;
    c.line = 0; c.line_start = 0; c.ev_phase = 1; c.ev_at = DMA_START;
    c.mstat = 0x80;
    a78_recalc(c);
}

void a78_run(A78& c) {
    uint16_t entry = c.pc;
    a78_call(c, entry);
    for (;;) a78_exec(c, c.resume, 0x1FF);  // unwound to the top: interpret on
}

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

uint64_t a78_hash(const A78& c) {
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, c.ram, sizeof c.ram);
    h = fnv(h, c.riot_ram, sizeof c.riot_ram);
    h = fnv(h, c.cram, sizeof c.cram);
    h = fnv(h, c.pal, sizeof c.pal);
    h = fnv(h, c.fb, sizeof c.fb[0] * c.lines);
    uint8_t cpu[24] = { c.a, c.x, c.y, c.s, a78_p(c), (uint8_t)c.bank, c.nmi_pending, c.dmaon,
                        c.ctrl, c.charbase, c.offset, (uint8_t)c.dpp, (uint8_t)(c.dpp >> 8),
                        (uint8_t)c.dll, (uint8_t)(c.dll >> 8), c.ora, c.orb, c.ddra, c.ddrb,
                        c.timer_v, c.timer_flag, c.lock, c.mstat, 0 };
    h = fnv(h, cpu, sizeof cpu);
    h = fnv(h, &c.cyc, sizeof c.cyc);
    h = fnv(h, &c.timer_t0, sizeof c.timer_t0);
    return h;
}

void a78_rgb(const A78& c, uint32_t* out, int& w, int& h) {
    const uint32_t* P = c.lines == 313 ? A78_PAL : A78_NTSC;
    int y0 = c.lines == 313 ? 35 : 27;
    w = 320; h = c.lines == 313 ? 260 : 224;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < 320; x++) out[y * 320 + x] = P[c.fb[y0 + y][x]];
}

// ---- the tracer's report ----
void a78_trace_begin(A78& c) {
    c.tr = new A78Trace();
    c.tr->cov.assign(c.romsize, 0);
    c.natives = false;
}

void a78_trace_report(A78& c, const char* dir, const char* rom_path) {
    A78Trace* t = c.tr;
    std::string d = dir;
    FILE* f = std::fopen((d + "/cov.bin").c_str(), "wb");
    if (f) { std::fwrite(t->cov.data(), 1, t->cov.size(), f); std::fclose(f); }
    f = std::fopen((d + "/ramexec.bin").c_str(), "wb");
    if (f) { std::fwrite(t->ramexec, 1, sizeof t->ramexec, f); std::fclose(f); }
    f = std::fopen((d + "/report.txt").c_str(), "w");
    if (!f) return;
    static const char* mnames[] = { "rom", "pokey", "mram", "sg", "sg_pokey", "sg_ram", "sg9", "abs", "act" };
    std::fprintf(f, "rom %s\nsize %u mapper %s lines %d frames %llu jammed %d\n", rom_path, c.romsize,
                 mnames[c.mapper], c.lines, (unsigned long long)c.frame, c.jammed);
    std::fprintf(f, "insns %llu nmi %llu brk %llu illegal %llu wsync %llu dma_full_lines %llu "
                 "int_depth_max %d nmi_max_per_frame %llu frames_with_dli %llu\n",
                 (unsigned long long)t->insn_n, (unsigned long long)c.n_nmi, (unsigned long long)c.n_brk,
                 (unsigned long long)t->illegal_n, (unsigned long long)c.n_wsync,
                 (unsigned long long)c.n_dma_full, t->int_depth_max,
                 (unsigned long long)t->nmi_max_frame, (unsigned long long)t->dli_frames);
    std::fprintf(f, "dma_avg_ticks_per_line %.1f\n", t->dma_lines ? (double)t->dma_ticks / t->dma_lines : 0.0);
    uint32_t nb = (c.romsize + 0x3FFF) / 0x4000;
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t op = 0, arg = 0, rdn = 0, both = 0, dma = 0;
        for (uint32_t i = b * 0x4000; i < (b + 1) * 0x4000 && i < c.romsize; i++) {
            uint8_t x = t->cov[i];
            if (x & R_OP) op++;
            if (x & (R_OP | R_ARG)) arg++;
            if (x & R_READ) rdn++;
            if ((x & R_OP) && (x & R_ARG)) both++;
            if (x & R_DMA) dma++;
        }
        std::fprintf(f, "bank %u code_bytes %u insn_starts %u data_read %u gfx_dma %u overlap %u\n",
                     b, arg, op, rdn, dma, both);
    }
    int ramcode = 0; for (int i = 0; i < 0x10000; i++) ramcode += t->ramexec[i];
    std::fprintf(f, "ramexec_bytes %d ramexec_insns %llu smc_writes %llu", ramcode,
                 (unsigned long long)t->ramexec_n, (unsigned long long)t->smc_n);
    if (t->smc_first != 0xFFFFFFFF) std::fprintf(f, " smc_first_pc %04X", t->smc_first);
    std::fprintf(f, "\nbank_writes %llu bank_changes %llu bank_change_from_window %llu\nbank_values",
                 (unsigned long long)t->bank_writes, (unsigned long long)t->bank_changes,
                 (unsigned long long)t->bank_from_window);
    for (int i = 0; i < 16; i++) if (t->bank_hist[i]) std::fprintf(f, " %d:%llu", i, (unsigned long long)t->bank_hist[i]);
    std::fprintf(f, "\ncomputed_rts %llu dropped_returns %llu rom_writes %llu open_bus %llu\n",
                 (unsigned long long)t->comp_rts, (unsigned long long)t->dropped,
                 (unsigned long long)t->rom_writes, (unsigned long long)t->open_rw);
    std::fprintf(f, "io_w");
    for (int i = 0; i < 0x40; i++) if (t->io_w[i]) std::fprintf(f, " %02X:%llu", i, (unsigned long long)t->io_w[i]);
    std::fprintf(f, "\nio_r");
    for (int i = 0; i < 0x40; i++) if (t->io_r[i]) std::fprintf(f, " %02X:%llu", i, (unsigned long long)t->io_r[i]);
    std::fprintf(f, "\nriot_w");
    for (int i = 0; i < 0x20; i++) if (t->riot_w[i]) std::fprintf(f, " %02X:%llu", i, (unsigned long long)t->riot_w[i]);
    std::fprintf(f, "\nriot_r");
    for (int i = 0; i < 0x20; i++) if (t->riot_r[i]) std::fprintf(f, " %02X:%llu", i, (unsigned long long)t->riot_r[i]);
    std::fprintf(f, "\npokey_w");
    for (int i = 0; i < 16; i++) if (t->pokey_w[i]) std::fprintf(f, " %X:%llu", i, (unsigned long long)t->pokey_w[i]);
    std::fprintf(f, "\npokey_r");
    for (int i = 0; i < 16; i++) if (t->pokey_r[i]) std::fprintf(f, " %X:%llu", i, (unsigned long long)t->pokey_r[i]);
    std::fprintf(f, "\nillegal_ops");
    for (int i = 0; i < 256; i++)
        if (t->ops[i] && OP[i].illegal) std::fprintf(f, " %02X(%s):%llu", i, INS_NAMES[OP[i].ins], (unsigned long long)t->ops[i]);
    int nops = 0; for (int i = 0; i < 256; i++) nops += t->ops[i] != 0;
    std::fprintf(f, "\nops_used %d\nnmi_hits", nops);
    std::vector<std::pair<uint64_t, uint16_t>> hits;
    for (auto& kv : t->nmi_at) hits.push_back({ kv.second, kv.first });
    std::sort(hits.rbegin(), hits.rend());
    for (size_t i = 0; i < hits.size() && i < 8; i++)
        std::fprintf(f, " %04X:%llu", hits[i].second, (unsigned long long)hits[i].first);
    std::fprintf(f, " (%zu places)\n", hits.size());
    for (auto& kv : t->edges) std::fprintf(f, "edge %s x%llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    std::fclose(f);
}
