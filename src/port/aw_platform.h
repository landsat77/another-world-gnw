/*
 * Interface between the Another World engine (src/raw + aw_core.cpp) and the
 * platform it runs on.
 *
 * Two implementations exist:
 *  - src/port/aw_main.c   : the Game & Watch homebrew (retro-go)
 *  - harness/aw_host.c    : a headless Linux harness used to test the port
 *                           without hardware
 *
 * Distributed under the GPLv2 or later (same license as raw).
 */
#ifndef AW_PLATFORM_H
#define AW_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AW_WIDTH  320
#define AW_HEIGHT 200

/* Buttons, as seen by the game (aw_core_set_buttons()) */
enum {
    AW_BTN_LEFT   = 1 << 0,
    AW_BTN_RIGHT  = 1 << 1,
    AW_BTN_UP     = 1 << 2,   /* also jump */
    AW_BTN_DOWN   = 1 << 3,
    AW_BTN_ACTION = 1 << 4,   /* run, shoot, kick */
    AW_BTN_PAUSE  = 1 << 5,
};

/* ---- implemented by the platform ---- */

/* Monotonic time in milliseconds. */
uint32_t aw_plat_time_ms(void);

/* Called while the game waits (in_delay = 1, a sleep of the engine) and once
 * per game frame when it reads its inputs (in_delay = 0). The platform feeds
 * its audio output with aw_core_audio_render(), reads its buttons and gives
 * them to aw_core_set_buttons(). */
void aw_plat_service(int in_delay);

/* Called very often while the game draws (once per polygon): the platform
 * may refill its audio output there, nothing else. Must be cheap. */
void aw_plat_poll(void);

/* Called once per game frame at the point where the game can be saved and
 * loaded (aw_core_save_state() / aw_core_load_state()): the platform runs
 * its pause menu here. */
void aw_plat_safe_point(void);

/* Show a 320x200 frame: `page` is the game's video page (4 bits per pixel,
 * two pixels per byte, high nibble first, 160 bytes per line), `palette`
 * the 16 colors in RGB565. */
void aw_plat_present(const uint8_t *page, const uint16_t *palette);

/* Output sample rate (mono, signed 16 bit). */
int aw_plat_audio_rate(void);

/* Look up a file of the asset pack ("MEMLIST.BIN", "RES017", ...). The
 * returned memory is read-only and stays valid while the game runs. */
bool aw_plat_find_file(const char *name, const uint8_t **data, uint32_t *size);

/* Start of the asset pack in memory (savestates store the pointers into the
 * game data as offsets from there). */
const uint8_t *aw_plat_pack_base(void);

/* Unrecoverable error (bad data). Never returns. */
void aw_plat_fatal(const char *msg) __attribute__((noreturn));

/* ---- implemented by the core (aw_core.cpp) ---- */

typedef struct {
    /* restore this savestate as soon as the game runs (NULL: none) */
    const char *load_state;
    /* first part of the game: 0 = default (the intro, the copy protection
     * screen is skipped), else a part id (0x3E80..0x3E89) */
    uint16_t start_part;
} aw_core_options_t;

/* Run the game. Never returns. */
void aw_core_run(const aw_core_options_t *opts) __attribute__((noreturn));

/* Buttons currently held (AW_BTN_*). */
void aw_core_set_buttons(uint32_t buttons);

/* Render `frames` mono samples (sound effects and music). The music player
 * is driven by this sample clock, so the music keeps its tempo whatever the
 * speed of the game. */
void aw_core_audio_render(int16_t *out, int frames);

/* Savestates: only valid from aw_plat_safe_point(). */
bool aw_core_can_save(void);
bool aw_core_save_state(const char *path);
bool aw_core_load_state(const char *path);

/* The game is in its own pause (GAME button): it waits in a loop that only
 * calls aw_plat_service(), the platform may run its menu from there (no
 * savestates: aw_core_can_save() is false). */
bool aw_core_game_paused(void);

/* Current part of the game (0x3E80..0x3E89), 0 before the game starts. */
uint16_t aw_core_current_part(void);

/* ---- implemented by aw_alloc.c ---- */
void   aw_heap_init(void *base, size_t size);
void  *aw_malloc(size_t size);
void  *aw_calloc(size_t n, size_t size);
void  *aw_realloc(void *ptr, size_t size);
void   aw_free(void *ptr);
size_t aw_heap_used(void);
size_t aw_heap_peak(void);
size_t aw_heap_size(void);
/* Host debug builds only (AW_HOST_SYSTEM_MALLOC): never free pointers in this range. */
void   aw_heap_ignore_range(const void *lo, const void *hi);

#ifdef __cplusplus
}
#endif

#endif
