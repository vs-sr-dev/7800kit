// The sound, computed in CPU cycles from the logged register writes.
// The TIA after the classic description of its audio (a 31.4 kHz clock,
// AUDF+1 division, then per AUDC a pure tone, a division or one of the
// 4/5/9-bit polynomials); the POKEY after its datasheet (64 or 15 kHz base
// clock or 1.79 MHz, 8- or 16-bit dividers, the 4/5/9/17-bit polynomials,
// the high-pass flip-flops).  Each output sample averages the cycles it spans.
#include "a78_audio.h"
#include <cstdio>

static inline uint8_t lfsr4(uint8_t& x) { x = (uint8_t)(((x << 1) | (((x >> 3) ^ (x >> 2)) & 1)) & 0xF); return x & 1; }
static inline uint8_t lfsr5(uint8_t& x) { x = (uint8_t)(((x << 1) | (((x >> 4) ^ (x >> 2)) & 1)) & 0x1F); return x & 1; }
static inline uint8_t lfsr9(uint16_t& x) { x = (uint16_t)(((x << 1) | (((x >> 8) ^ (x >> 4)) & 1)) & 0x1FF); return x & 1; }

static void tia_clock(A78Audio::Tia& ch) {
    if (ch.div < ch.f) { ch.div++; return; }
    ch.div = 0;
    switch (ch.c & 0x0F) {
    case 0x0: case 0xB: ch.out = 1; break;
    case 0x1: ch.out = lfsr4(ch.p4); break;
    case 0x2: if (++ch.cnt >= 15) { ch.cnt = 0; ch.out = lfsr4(ch.p4); } break;
    case 0x3: if (lfsr5(ch.p5)) ch.out = lfsr4(ch.p4); break;
    case 0x4: case 0x5: ch.out ^= 1; break;
    case 0x6: case 0xA: if (++ch.cnt >= 31) ch.cnt = 0; ch.out = ch.cnt < 18; break;
    case 0x7: case 0x9: ch.out = lfsr5(ch.p5); break;
    case 0x8: ch.out = lfsr9(ch.p9); break;
    case 0xC: case 0xD: if (++ch.cnt >= 3) { ch.cnt = 0; ch.out ^= 1; } break;
    case 0xE: if (++ch.cnt >= 93) ch.cnt = 0; ch.out = ch.cnt < 46; break;
    case 0xF: if (lfsr5(ch.p5) && ++ch.cnt >= 3) { ch.cnt = 0; ch.out ^= 1; } break;
    }
}

static void apply(A78Audio& a, uint8_t reg, uint8_t v) {
    if (reg >= 0x15 && reg <= 0x1A) {
        int n = (reg - 0x15) & 1;
        switch ((reg - 0x15) >> 1) {
        case 0: a.tia[n].c = v & 0x0F; break;
        case 1: a.tia[n].f = v & 0x1F; break;
        case 2: a.tia[n].v = v & 0x0F; break;
        }
    } else if (reg >= 0x40 && reg < 0x50) {
        a.has_pokey = true;
        a.pk[reg & 0x0F] = v;
        if ((reg & 0x0F) == 9) for (int i = 0; i < 4; i++) a.pdiv[i] = 0;   // STIMER
    }
}

// One POKEY channel's divider ran out: its output's next value.
static void pokey_fire(A78Audio& a, int ch, uint8_t b4, uint8_t b5, uint8_t bp) {
    uint8_t c = a.pk[ch * 2 + 1];
    if (!(c & 0x80) && !b5) return;
    if (c & 0x20) a.pout[ch] ^= 1;
    else a.pout[ch] = (c & 0x40) ? b4 : bp;
}

static inline int pokey_period(const A78Audio& a, int ch, bool fast, bool joined) {
    if (joined) return (a.pk[ch * 2] << 8 | a.pk[(ch - 1) * 2]) + (fast ? 7 : 1);
    return a.pk[ch * 2] + (fast ? 4 : 1);
}

static int pokey_cycle(A78Audio& a) {
    uint8_t ctl = a.pk[8];
    if ((a.pk[15] & 3) == 0) return 0;            // SKCTL: held in reset
    uint8_t b4 = lfsr4(a.p4), b5 = lfsr5(a.p5), b9 = lfsr9(a.p9);
    a.p17 = ((a.p17 << 1) | (((a.p17 >> 16) ^ (a.p17 >> 11)) & 1)) & 0x1FFFF;
    uint8_t bp = (ctl & 0x80) ? b9 : (uint8_t)(a.p17 & 1);
    bool tick = false;
    if (++a.base >= ((ctl & 1) ? 114 : 28)) { a.base = 0; tick = true; }
    bool j12 = ctl & 0x10, j34 = ctl & 0x08;
    bool clk[4] = { (ctl & 0x40) ? true : tick, tick, (ctl & 0x20) ? true : tick, tick };
    if (j12) clk[1] = clk[0];
    if (j34) clk[3] = clk[2];
    bool fired[4] = { false, false, false, false };
    for (int ch = 0; ch < 4; ch++) {
        if (!clk[ch]) continue;
        if ((ch == 0 && j12) || (ch == 2 && j34)) continue;      // the low half of a pair
        bool joined = (ch == 1 && j12) || (ch == 3 && j34);
        bool fast = joined ? ((ch == 1 && (ctl & 0x40)) || (ch == 3 && (ctl & 0x20)))
                           : ((ch == 0 && (ctl & 0x40)) || (ch == 2 && (ctl & 0x20)));
        if (--a.pdiv[ch] <= 0) {
            a.pdiv[ch] = pokey_period(a, ch, fast, joined);
            pokey_fire(a, ch, b4, b5, bp);
            fired[ch] = true;
        }
    }
    if (fired[2]) a.hp[0] = a.pout[0];
    if (fired[3]) a.hp[1] = a.pout[1];
    int level = 0;
    for (int ch = 0; ch < 4; ch++) {
        uint8_t c = a.pk[ch * 2 + 1];
        if ((ch == 0 && j12) || (ch == 2 && j34)) continue;
        uint8_t o = a.pout[ch];
        if (ch == 0 && (ctl & 4)) o ^= a.hp[0];
        if (ch == 1 && (ctl & 2)) o ^= a.hp[1];
        if (c & 0x10) o = 1;                       // volume only
        if (o) level += c & 0x0F;
    }
    return level;
}

void A78Audio::frame(A78& c, std::vector<int16_t>& out) {
    const uint64_t tps = c.lines == 313 ? 7093788ull : 7159090ull;   // ticks per second
    const uint64_t end = c.cyc;
    uint32_t e = 0;
    while (t < end) {
        while (e < c.snd_n && c.snd_ev[e].t <= t) { if (c.snd_ev[e].reg != 0xFF) apply(*this, c.snd_ev[e].reg, c.snd_ev[e].val); e++; }
        int level = 0;
        if (has_pokey) level += pokey_cycle(*this);
        tia_acc += TICKS_CYC;
        if (tia_acc >= 227) { tia_acc -= 227; tia_clock(tia[0]); tia_clock(tia[1]); }
        level += tia[0].out * tia[0].v + tia[1].out * tia[1].v;
        acc += level; nacc++;
        t += TICKS_CYC;
        sacc += (uint64_t)TICKS_CYC * rate;
        if (sacc >= tps) {
            sacc -= tps;
            double x = nacc ? acc / nacc : 0;
            acc = 0; nacc = 0;
            dc_y = x - dc_x + 0.995 * dc_y; dc_x = x;          // no DC
            double s = dc_y * (32767.0 / 60.0);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out.push_back((int16_t)s); out.push_back((int16_t)s);
        }
    }
    while (e < c.snd_n) { if (c.snd_ev[e].reg != 0xFF) apply(*this, c.snd_ev[e].reg, c.snd_ev[e].val); e++; }
    c.snd_n = 0;
}

void a78_write_wav(const char* path, const std::vector<int16_t>& s, int rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    uint32_t data = (uint32_t)s.size() * 2, riff = 36 + data, br = rate * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f);
    std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}
