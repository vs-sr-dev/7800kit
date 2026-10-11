// The 7800's sound: the TIA's two channels and, on the carts that have one,
// a POKEY's four, synthesized from the writes to their registers as the
// machine logs them (A78::snd_ev), each stamped with its tick.
#pragma once
#include "a78.h"
#include <vector>

struct A78Audio {
    int rate = 48000;
    uint64_t t = 0;            // ticks synthesized
    uint64_t sacc = 0;         // the sample clock, in ticks * rate
    double acc = 0; int nacc = 0;
    double dc_x = 0, dc_y = 0;
    // the TIA: AUDC, AUDF, AUDV and the waveform generators
    struct Tia { uint8_t c = 0, f = 0, v = 0, div = 0, out = 0, cnt = 0;
                 uint8_t p4 = 0xF, p5 = 0x1F; uint16_t p9 = 0x1FF; } tia[2];
    int tia_acc = 0;
    // the POKEY
    uint8_t pk[16] = { 0 };
    int pdiv[4] = { 0 }; uint8_t pout[4] = { 0 }, hp[2] = { 0 };
    uint8_t p4 = 0xF, p5 = 0x1F; uint16_t p9 = 0x1FF; uint32_t p17 = 0x1FFFF;
    int base = 0;
    bool has_pokey = false;
    void frame(A78& c, std::vector<int16_t>& out);
};

void a78_write_wav(const char* path, const std::vector<int16_t>& s, int rate);
