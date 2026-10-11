// The 7800 executable: the recompiled game, or (built with no native code)
// the interpreter and the tracer.
//
//   a78 ROM.a78 [--frames N] [--snap F1,F2,..] [--out DIR]   headless, the monkey pad
//   a78 ROM.a78 --interp ...        the interpreter alone (the reference)
//   a78 ROM.a78 --lockstep ...      native and interpreter side by side: the
//                                   machine state compared at every frame
//   a78 ROM.a78 --trace DIR ...     the interpreter, measuring for the recompiler
//   a78 ROM.a78 --window [--scale 3]  in a window, with the keyboard and sound (SDL2)
//   a78 ROM.a78 --wav FILE ...      headless, the sound written to a WAV
//   --mapper NAME, --pal            override what the header says
//
// Keys: arrows, Z / X = the two buttons, Enter = Reset, Shift = Select,
// P = Pause, F1 / F2 = the difficulty switches, Esc quits.
#include "a78.h"
#include "a78_audio.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef A78_SDL
#include <SDL2/SDL.h>
#endif

// ---- the monkey: a deterministic pad ----
static uint32_t rng = 12345;
static uint32_t rnd() { rng = rng * 1103515245u + 12345u; return rng >> 16; }
struct Pad { uint8_t joy, buttons, console; };
static Pad monkey(long f) {
    static uint8_t held = 0, fire = 0;
    Pad p = { 0xFF, 0, 0xFF };
    if (f < 120) return p;
    if ((f >= 120 && f < 128) || (f % 3000 >= 1500 && f % 3000 < 1508)) p.console &= ~1;   // Reset
    int ph = (int)((f - 120) % 300);
    if ((ph >= 30 && ph < 36) || (ph >= 90 && ph < 96)) { p.buttons = 0x0A; return p; }    // fire
    if (ph % 15 == 0) {
        static const uint8_t dirs[] = { 0, 1, 2, 4, 8, 1 | 8, 2 | 8, 1 | 4, 2 | 4 };   // U D L R
        held = dirs[rnd() % 9];
        fire = 0;
        if (rnd() % 100 < 40) fire |= 0x08;
        if (rnd() % 100 < 25) fire |= 0x02;
    }
    // P0 is the high nibble: R L D U = bits 7 6 5 4
    uint8_t j = 0;
    if (held & 1) j |= 0x10;
    if (held & 2) j |= 0x20;
    if (held & 4) j |= 0x40;
    if (held & 8) j |= 0x80;
    p.joy = (uint8_t)~j;
    p.buttons = fire;
    return p;
}

struct Opts {
    std::string rom, out = ".", wav, trace, mapper;
    long frames = 3600;
    bool interp = false, lockstep = false, window = false, quiet = false, pal = false, pal_set = false;
    int scale = 3;
    int watch = -1; long watch_from = 0;
    std::vector<long> snaps;
};
static Opts O;
static A78* g_ref = nullptr;     // the interpreter, in lockstep
static A78Audio g_audio;
static std::vector<int16_t> g_samples;
static long g_mismatch = -1;

static void write_ppm(const A78& c, const std::string& path) {
    static uint32_t px[320 * 260];
    int w, h;
    a78_rgb(c, px, w, h);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row(w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = px[y * w + x];
            row[3 * x] = v >> 16; row[3 * x + 1] = v >> 8; row[3 * x + 2] = (uint8_t)v;
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

static void diff(const A78& a, const A78& b) {
    std::fprintf(stderr, "  interp: A=%02X X=%02X Y=%02X S=%02X P=%02X bank=%u cyc=%llu line=%d\n",
                 a.a, a.x, a.y, a.s, a78_p(a), a.bank, (unsigned long long)a.cyc, a.line);
    std::fprintf(stderr, "  native: A=%02X X=%02X Y=%02X S=%02X P=%02X bank=%u cyc=%llu line=%d\n",
                 b.a, b.x, b.y, b.s, a78_p(b), b.bank, (unsigned long long)b.cyc, b.line);
    int n = 0;
    for (int i = 0; i < 0x1000 && n < 12; i++)
        if (a.ram[i] != b.ram[i]) { std::fprintf(stderr, "  ram[%04X] %02X / %02X\n", 0x1800 + i, a.ram[i], b.ram[i]); n++; }
    for (int i = 0; i < 0x8000 && n < 16; i++)
        if (a.cram[i] != b.cram[i]) { std::fprintf(stderr, "  cram[%04X] %02X / %02X\n", i, a.cram[i], b.cram[i]); n++; }
    for (int i = 0; i < 32; i++)
        if (a.pal[i] != b.pal[i]) std::fprintf(stderr, "  pal[%02X] %02X / %02X\n", i, a.pal[i], b.pal[i]);
}

static void apply_pad(A78& c, const Pad& p) { c.joy = p.joy; c.buttons = p.buttons; c.console = p.console; }

static void history(const char* who, const A78& c) {
    std::string path = O.out + "/hist_" + who + ".txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    uint32_t n = c.hn < 4096 ? c.hn : 4096;
    for (uint32_t i = c.hn - n; i != c.hn; i++)
        std::fprintf(f, "%u:%04X %llu\n", c.hbank[i & 4095], c.hist[i & 4095], (unsigned long long)c.hcyc[i & 4095]);
    std::fclose(f);
    std::fprintf(stderr, "  %s: the last %u instructions in %s\n", who, n, path.c_str());
}

#ifdef A78_HISTORY
void a78_watch(const A78& c, uint16_t at) {
    if (c.frame < c.watch_from) return;
    std::fprintf(stderr, "  %s %04X frame %llu line %d cyc %llu S=%02X depth %d stack",
                 c.natives ? "native" : "interp", at, (unsigned long long)c.frame, c.line,
                 (unsigned long long)c.cyc, c.s, c.depth);
    for (int i = 1; i <= 6 && c.s + i < 256; i++) {
        uint8_t s = (uint8_t)(c.s + i);
        std::fprintf(stderr, " %02X", s >= 0x40 ? c.ram[0x900 + s] : 0);
    }
    std::fprintf(stderr, "\n");
}
#endif

// --lockstep-from F: from frame F on, the two are compared at every line.
static long g_line_from = -1;
static void hook_line(A78& c) {
    if ((long)c.frame < g_line_from) return;
    g_ref->line_stop = true;
    while (g_ref->frame < c.frame || (g_ref->frame == c.frame && g_ref->line < c.line))
        if (!a78_flat_frame(*g_ref)) break;
    if (a78_hash(*g_ref) != a78_hash(c)) {
        std::fprintf(stderr, "lockstep: the machines differ at frame %llu line %d\n",
                     (unsigned long long)c.frame, c.line);
        diff(*g_ref, c);
        history("interp", *g_ref);
        history("native", c);
        g_mismatch = (long)c.frame;
        throw A78Stop();
    }
}

static void frame_common(A78& c) {
    long f = (long)c.frame;
    for (long s : O.snaps)
        if (s == f) { char n[32]; std::snprintf(n, sizeof n, "/f%05ld.ppm", f); write_ppm(c, O.out + n); }
    if (f >= O.frames) throw A78Stop();
}

static void hook_headless(A78& c) {
    long f = (long)c.frame;
    if (g_ref) {
        // the reference runs to the same frame event, then the two are compared
        while ((long)g_ref->frame < f)
            if (!a78_flat_frame(*g_ref)) break;
        if (a78_hash(*g_ref) != a78_hash(c)) {
            std::fprintf(stderr, "lockstep: the machines differ at frame %ld\n", f);
            diff(*g_ref, c);
            history("interp", *g_ref);
            history("native", c);
            g_mismatch = f;
            throw A78Stop();
        }
    }
    if (c.snd_log) g_audio.frame(c, g_samples);
    Pad p = monkey(f);
    apply_pad(c, p);
    if (g_ref) apply_pad(*g_ref, p);
    frame_common(c);
}

#ifdef A78_SDL
static SDL_Window* win; static SDL_Renderer* ren; static SDL_Texture* tex;
static SDL_AudioDeviceID adev;
static std::chrono::steady_clock::time_point t_next;
static void hook_window(A78& c) {
    static uint32_t px[320 * 260];
    int w, h;
    a78_rgb(c, px, w, h);
    SDL_UpdateTexture(tex, nullptr, px, 320 * 4);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) throw A78Stop();
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    uint8_t j = 0, b = 0, con = 0xFF;
    if (k[SDL_SCANCODE_UP]) j |= 0x10;
    if (k[SDL_SCANCODE_DOWN]) j |= 0x20;
    if (k[SDL_SCANCODE_LEFT]) j |= 0x40;
    if (k[SDL_SCANCODE_RIGHT]) j |= 0x80;
    if (k[SDL_SCANCODE_Z]) b |= 0x08;
    if (k[SDL_SCANCODE_X]) b |= 0x02;
    if (k[SDL_SCANCODE_RETURN]) con &= ~0x01;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) con &= ~0x02;
    if (k[SDL_SCANCODE_P]) con &= ~0x08;
    if (k[SDL_SCANCODE_F1]) con &= ~0x40;
    if (k[SDL_SCANCODE_F2]) con &= ~0x80;
    c.joy = (uint8_t)~j; c.buttons = b; c.console = con;
    g_samples.clear();
    g_audio.frame(c, g_samples);
    if (adev) {
        uint32_t queued = SDL_GetQueuedAudioSize(adev), fr = (uint32_t)g_samples.size() * 2;
        if (queued < fr * 6) SDL_QueueAudio(adev, g_samples.data(), fr);
    }
    t_next += std::chrono::microseconds(c.lines == 313 ? 20000 : 16639);
    std::this_thread::sleep_until(t_next);
}
#endif

// The cartridge: the header's mapper, as MAME reads it, and the few carts
// whose header is wrong (7800kit/cart.py has the same rules).
static uint32_t crc32(const uint8_t* p, uint32_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static int header_mapper(const uint8_t* h, uint32_t size, const std::string& path) {
    static const struct { uint32_t crc; const char* m; } by_crc[] = {
        { 0xED0A587D, "sg" }, { 0xCD1A98C5, "sg_pokey" }, { 0xA1EAA7C1, "rom" },
        { 0x63D1C7C1, "abs" }, { 0x3B1F2F47, "sg_ram" }, { 0x65C6DF3F, "sg_ram" },
        { 0x3FBE74CE, "pokey" }, { 0x8981B531, "sg_ram" }, { 0x47340DF9, "sg" } };
    uint32_t crc = crc32(h + 128, size);
    for (auto& f : by_crc) if (f.crc == crc) return a78_mapper_by_name(f.m);
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    static const char* fix[][2] = { { "Double Dragon", "act" }, { "Rampage", "act" },
                                    { "Impossible Mission", "sg_ram" } };
    for (auto& f : fix) if (base.rfind(f[0], 0) == 0) return a78_mapper_by_name(f[1]);
    int ct = h[53] << 8 | h[54];
    if ((ct & 0xFF00) == 0x0100) return MAP_ACT;
    if ((ct & 0xFF00) == 0x0200) return MAP_ABS;
    switch (ct & 0x2E) {
    case 0x00: return (ct & 1) ? MAP_POKEY : MAP_ROM;
    case 0x02: return (ct & 1) ? MAP_SG_POKEY : MAP_SG;
    case 0x06: return size > 0x8000 ? MAP_SG_RAM : MAP_MRAM;
    case 0x0A: return MAP_SG9;
    }
    return MAP_ROM;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--frames") O.frames = std::atol(val().c_str());
        else if (a == "--interp") O.interp = true;
        else if (a == "--lockstep") O.lockstep = true;
        else if (a == "--lockstep-from") { O.lockstep = true; g_line_from = std::atol(val().c_str()); }
        else if (a == "--watch") O.watch = (int)std::strtol(val().c_str(), nullptr, 16);
        else if (a == "--watch-from") O.watch_from = std::atol(val().c_str());
        else if (a == "--window") O.window = true;
        else if (a == "--quiet") O.quiet = true;
        else if (a == "--scale") O.scale = std::atoi(val().c_str());
        else if (a == "--out") O.out = val();
        else if (a == "--wav") O.wav = val();
        else if (a == "--trace") O.trace = val();
        else if (a == "--mapper") O.mapper = val();
        else if (a == "--pal") { O.pal = true; O.pal_set = true; }
        else if (a == "--ntsc") { O.pal = false; O.pal_set = true; }
        else if (a == "--snap") {
            std::string s = val(); size_t p = 0;
            while (p < s.size()) { O.snaps.push_back(std::atol(s.c_str() + p));
                                   p = s.find(',', p); if (p == std::string::npos) break; p++; }
        } else O.rom = a;
    }
    if (O.rom.empty()) { std::fprintf(stderr, "a78 ROM.a78 [--frames N] [--interp|--lockstep|--trace DIR|--window]\n"); return 1; }
    FILE* f = std::fopen(O.rom.c_str(), "rb");
    if (!f) { std::perror(O.rom.c_str()); return 1; }
    std::vector<uint8_t> file;
    { uint8_t buf[65536]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n); }
    std::fclose(f);
    if (file.size() < 128 || std::memcmp(&file[1], "ATARI7800", 9)) { std::fprintf(stderr, "%s: no a78 header\n", O.rom.c_str()); return 1; }
    uint32_t size = (uint32_t)file[49] << 24 | file[50] << 16 | file[51] << 8 | file[52];
    if (size > file.size() - 128) size = (uint32_t)file.size() - 128;
    int mapper = O.mapper.empty() ? header_mapper(file.data(), size, O.rom) : a78_mapper_by_name(O.mapper.c_str());
    if (mapper < 0) { std::fprintf(stderr, "unknown mapper %s\n", O.mapper.c_str()); return 1; }
    bool pal = O.pal_set ? O.pal : (file[57] & 1);
    const uint8_t* rom = file.data() + 128;

    a78_register_all();
    static A78 c, ref;
    a78_load(c, rom, size, mapper, pal);
    a78_reset(c);
    apply_pad(c, monkey(0));
    c.natives = !O.interp && O.trace.empty();
    c.frame_hook = hook_headless;
    c.snd_log = !O.wav.empty();
    if (!O.trace.empty()) a78_trace_begin(c);
    c.watch = ref.watch = O.watch; c.watch_from = ref.watch_from = (uint64_t)O.watch_from;
    if (O.lockstep) {
        a78_load(ref, rom, size, mapper, pal);
        a78_reset(ref);
        apply_pad(ref, monkey(0));
        ref.natives = false;
        ref.flat_stop = true;
        g_ref = &ref;
        if (g_line_from >= 0) c.line_hook = hook_line;
    }
#ifdef A78_SDL
    if (O.window) {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        SDL_AudioSpec want{}, have{};
        want.freq = g_audio.rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0);
        c.snd_log = true;
        int h = pal ? 260 : 224;
        win = SDL_CreateWindow("7800kit", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * O.scale, h * O.scale * 2 / 2, 0);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, h);
        c.frame_hook = hook_window;
        apply_pad(c, Pad{ 0xFF, 0, 0xFF });
        t_next = std::chrono::steady_clock::now();
    }
#endif
    auto t0 = std::chrono::steady_clock::now();
    try {
        if (!c.natives) {
            c.flat_stop = false;
            for (;;) if (!a78_flat_frame(c)) break;
        } else a78_run(c);
    } catch (A78Stop&) {}
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (c.jammed && g_ref) {            // what the interpreter did up to the same point
        g_ref->line_stop = true;
        while (!g_ref->jammed && (g_ref->frame < c.frame || (g_ref->frame == c.frame && g_ref->line < c.line)))
            try { if (!a78_flat_frame(*g_ref)) break; } catch (A78Stop&) { break; }
        diff(*g_ref, c);
        history("interp", *g_ref);
        history("native", c);
    }
    write_ppm(c, O.out + "/last.ppm");
    if (!O.wav.empty()) a78_write_wav(O.wav.c_str(), g_samples, g_audio.rate);
    if (!O.trace.empty()) a78_trace_report(c, O.trace.c_str(), O.rom.c_str());
    if (!O.quiet) {
        double fps = pal ? 50.0 : 60.0;
        std::printf("frames %llu  %.2fs (%.0fx)  hash %016llx\n", (unsigned long long)c.frame, secs,
                    c.frame / fps / (secs > 0 ? secs : 1e-9), (unsigned long long)a78_hash(c));
        std::printf("interpreted insns %llu  native calls %llu  fallbacks %llu  unwinds %llu  "
                    "max depth %d  nmi %llu  wsync %llu  ticks %llu\n",
                    (unsigned long long)c.n_interp, (unsigned long long)c.n_native_calls,
                    (unsigned long long)c.n_fallback, (unsigned long long)c.n_unwind, c.max_depth,
                    (unsigned long long)c.n_nmi, (unsigned long long)c.n_wsync, (unsigned long long)c.cyc);
        if (O.lockstep)
            std::printf(g_mismatch < 0 ? "lockstep: identical for %llu frames\n"
                                       : "lockstep: MISMATCH at frame %llu\n",
                        (unsigned long long)(g_mismatch < 0 ? c.frame : g_mismatch));
    }
    return g_mismatch >= 0 ? 2 : (c.jammed ? 4 : 0);
}
