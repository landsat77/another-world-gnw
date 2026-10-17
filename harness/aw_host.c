/*
 * Headless Linux harness for the Another World homebrew.
 *
 * Runs the engine through exactly the same core, heap and asset pack code as
 * the Game & Watch, with a virtual clock (the game runs as fast as the host
 * can), scripted buttons and frame/audio dumps. The pack is mapped read-only,
 * like the external flash of the console: a write into the game data
 * crashes here too.
 *
 *   ./aw_host <pack.aw> [options]
 *     -t <ms>          stop after <ms> of game time (default 30000)
 *     -s <script>      button script: "ms:BTN:1,ms:BTN:0,..." (BTN = LEFT,
 *                      RIGHT, UP, DOWN, A (action), B (jump = up), GAME
 *                      (pause))
 *     -S <ms,ms,...>   write frame_<ms>.ppm at these times
 *     -e <ms>          also write a frame every <ms>
 *     -w <file.wav>    write the audio output
 *     -o <dir>         output directory (default .)
 *     -H <kb>          heap size in KB (default 600)
 *     -p <part>        start at this part (16000..16009, see raw/parts.h)
 *     -q <file>        savestate test: save to <file> at the time given by -Q
 *     -Q <ms>          time of the save
 *     -j <file>        load <file> at the time given by -J (pause menu load)
 *     -J <ms>          time of the load
 *     -L <file>        load this savestate at the start (like a resume)
 *     -F <ms>          slow device: every presented frame costs <ms> more
 *
 * Distributed under the GPLv2 or later (same license as raw).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "aw_platform.h"
#include "aw_pack.h"

#define RATE 22050

static uint64_t now_us;
static uint64_t end_us = 30000000ull;
static char out_dir[256] = ".";
static uint8_t last_page[AW_WIDTH * AW_HEIGHT / 2];
static uint16_t last_pal[16];
static bool have_frame;
static uint64_t frames_presented;
static uint64_t frame_cost_us;

typedef struct { uint64_t t; uint32_t mask; int down; } btn_ev_t;
static btn_ev_t evs[1024];
static int n_evs, next_ev;
static uint32_t buttons;

static uint64_t shots[256];
static int n_shots, next_shot;
static uint64_t shot_every, next_every;

static FILE *wav;
static uint64_t audio_frames_done;
static uint32_t wav_bytes;
static int32_t peak;

static const char *save_path, *load_path, *boot_load_path;
static uint64_t save_at, load_at;
static bool saved, loaded;

static const uint8_t *pack;
static size_t pack_size;

uint32_t aw_plat_time_ms(void) { return (uint32_t)(now_us / 1000); }
int aw_plat_audio_rate(void) { return RATE; }
const uint8_t *aw_plat_pack_base(void) { return pack; }

bool aw_plat_find_file(const char *name, const uint8_t **data, uint32_t *size)
{
    return aw_pack_find(name, data, size);
}

static void write_ppm(const char *name)
{
    if (!have_frame)
        return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < AW_WIDTH * AW_HEIGHT; i++) {
        uint8_t b = last_page[i / 2];
        uint16_t p = last_pal[(i & 1) ? (b & 15) : (b >> 4)];
        uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 0x1F) << 3), (uint8_t)(((p >> 5) & 0x3F) << 2), (uint8_t)((p & 0x1F) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void wav_header(uint32_t data_bytes)
{
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data_bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);
    s = 1; memcpy(h + 22, &s, 2);
    v = RATE; memcpy(h + 24, &v, 4);
    v = RATE * 2; memcpy(h + 28, &v, 4);
    s = 2; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

static void __attribute__((noreturn)) finish(int code)
{
    write_ppm("frame_end.ppm");
    if (wav) {
        wav_header(wav_bytes);
        fclose(wav);
    }
    printf("aw_host: %llu ms, %llu frames presented, part 0x%04X, audio peak %d, heap used %zu / peak %zu / size %zu\n",
           (unsigned long long)(now_us / 1000), (unsigned long long)frames_presented,
           aw_core_current_part(), (int)peak, aw_heap_used(), aw_heap_peak(), aw_heap_size());
    exit(code);
}

void aw_plat_fatal(const char *msg)
{
    printf("aw_host: FATAL at %llu ms: %s\n", (unsigned long long)(now_us / 1000), msg);
    finish(1);
}

static void feed_audio(void)
{
    uint64_t due = now_us * RATE / 1000000ull;
    while (audio_frames_done + 256 <= due) {
        int16_t buf[256];
        aw_core_audio_render(buf, 256);
        for (int i = 0; i < 256; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak)
                peak = a;
        }
        if (wav) {
            fwrite(buf, 2, 256, wav);
            wav_bytes += 512;
        }
        audio_frames_done += 256;
    }
}

static int in_service;

void aw_plat_service(int in_delay)
{
    if (in_service)
        return;
    in_service = 1;
    now_us += in_delay ? 1000 : 50;

    while (next_ev < n_evs && evs[next_ev].t <= now_us) {
        if (evs[next_ev].down)
            buttons |= evs[next_ev].mask;
        else
            buttons &= ~evs[next_ev].mask;
        next_ev++;
    }
    aw_core_set_buttons(buttons);
    while (next_shot < n_shots && shots[next_shot] <= now_us) {
        char name[64];
        snprintf(name, sizeof(name), "frame_%06llu.ppm", (unsigned long long)(shots[next_shot] / 1000));
        write_ppm(name);
        next_shot++;
    }
    if (shot_every && now_us >= next_every) {
        char name[64];
        snprintf(name, sizeof(name), "frame_%06llu.ppm", (unsigned long long)(next_every / 1000));
        write_ppm(name);
        next_every += shot_every;
    }
    feed_audio();
    {
        static uint16_t last_part;
        uint16_t part = aw_core_current_part();
        if (part != last_part) {
            printf("aw_host: %llu ms: part 0x%04X\n", (unsigned long long)(now_us / 1000), part);
            last_part = part;
        }
    }
    in_service = 0;
    if (now_us >= end_us)
        finish(0);
}

void aw_plat_poll(void)
{
    /* the drawing costs no virtual time */
}

void aw_plat_safe_point(void)
{
    if (save_path && !saved && now_us >= save_at) {
        saved = true;
        bool ok = aw_core_save_state(save_path);
        printf("aw_host: saving %s at %llu ms (part 0x%04X): %s\n", save_path,
               (unsigned long long)(now_us / 1000), aw_core_current_part(), ok ? "ok" : "FAILED");
    }
    if (load_path && !loaded && now_us >= load_at) {
        loaded = true;
        bool ok = aw_core_load_state(load_path);
        printf("aw_host: loading %s at %llu ms: %s (part 0x%04X)\n", load_path,
               (unsigned long long)(now_us / 1000), ok ? "ok" : "FAILED", aw_core_current_part());
    }
}

void aw_plat_present(const uint8_t *page, const uint16_t *palette)
{
    memcpy(last_page, page, sizeof(last_page));
    memcpy(last_pal, palette, sizeof(last_pal));
    have_frame = true;
    frames_presented++;
    now_us += frame_cost_us;
}

static uint32_t parse_btn(const char *s)
{
    if (!strcmp(s, "LEFT")) return AW_BTN_LEFT;
    if (!strcmp(s, "RIGHT")) return AW_BTN_RIGHT;
    if (!strcmp(s, "UP") || !strcmp(s, "B")) return AW_BTN_UP;
    if (!strcmp(s, "DOWN")) return AW_BTN_DOWN;
    if (!strcmp(s, "A")) return AW_BTN_ACTION;
    if (!strcmp(s, "GAME")) return AW_BTN_PAUSE;
    fprintf(stderr, "unknown button %s\n", s);
    exit(2);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int cmp_ev(const void *a, const void *b)
{
    const btn_ev_t *x = a, *y = b;
    return x->t < y->t ? -1 : x->t > y->t;
}

int main(int argc, char **argv)
{
    size_t heap_kb = 600;
    int start_part = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pack.aw> [options]\n", argv[0]);
        return 2;
    }
    const char *pack_path = argv[1];
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "-t") && v) { end_us = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-o") && v) { snprintf(out_dir, sizeof(out_dir), "%s", v); i++; }
        else if (!strcmp(a, "-H") && v) { heap_kb = strtoul(v, NULL, 10); i++; }
        else if (!strcmp(a, "-p") && v) { start_part = (int)strtol(v, NULL, 0); i++; }
        else if (!strcmp(a, "-w") && v) { wav = fopen(v, "wb"); if (wav) { uint8_t z[44] = {0}; fwrite(z, 1, 44, wav); } i++; }
        else if (!strcmp(a, "-e") && v) { shot_every = strtoull(v, NULL, 10) * 1000; next_every = shot_every; i++; }
        else if (!strcmp(a, "-q") && v) { save_path = v; i++; }
        else if (!strcmp(a, "-Q") && v) { save_at = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-j") && v) { load_path = v; i++; }
        else if (!strcmp(a, "-J") && v) { load_at = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-L") && v) { boot_load_path = v; i++; }
        else if (!strcmp(a, "-F") && v) { frame_cost_us = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-S") && v) {
            char *s = strdup(v);
            for (char *tok = strtok(s, ","); tok && n_shots < 256; tok = strtok(NULL, ","))
                shots[n_shots++] = strtoull(tok, NULL, 10) * 1000;
            qsort(shots, n_shots, sizeof(shots[0]), cmp_u64);
            i++;
        } else if (!strcmp(a, "-s") && v) {
            char *s = strdup(v);
            for (char *tok = strtok(s, ","); tok && n_evs < 1024; tok = strtok(NULL, ",")) {
                char key[32];
                unsigned long long t;
                int down;
                if (sscanf(tok, "%llu:%31[^:]:%d", &t, key, &down) != 3) {
                    fprintf(stderr, "bad script item %s\n", tok);
                    return 2;
                }
                evs[n_evs].t = t * 1000;
                evs[n_evs].mask = parse_btn(key);
                evs[n_evs].down = down;
                n_evs++;
            }
            qsort(evs, n_evs, sizeof(evs[0]), cmp_ev);
            i++;
        } else {
            fprintf(stderr, "unknown option %s\n", a);
            return 2;
        }
    }

    int fd = open(pack_path, O_RDONLY);
    if (fd < 0) {
        perror(pack_path);
        return 1;
    }
    struct stat st;
    fstat(fd, &st);
    pack_size = (size_t)st.st_size;
    pack = mmap(NULL, pack_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (pack == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    int r = aw_pack_open(pack, (uint32_t)pack_size);
    if (r != AW_PACK_OK) {
        fprintf(stderr, "invalid pack (%d)\n", r);
        return 1;
    }

    static uint8_t *heap;
    heap = malloc(heap_kb * 1024);
    aw_heap_init(heap, heap_kb * 1024);
    aw_heap_ignore_range(pack, pack + pack_size);

    aw_core_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.load_state = boot_load_path;
    opts.start_part = (uint16_t)start_part;
    aw_core_run(&opts); /* does not return */
    return 0;
}
