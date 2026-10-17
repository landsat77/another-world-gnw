/*
 * Another World for the Game & Watch (Retro-Go SD), as a GWHB homebrew.
 *
 * The game itself is the raw engine (src/raw, Gregory Montoir's
 * reimplementation of Eric Chahi's virtual machine, NEO-RAW version), run
 * single threaded by aw_core.cpp. This file is the retro-go side: it maps the
 * asset pack (the .aw file next to this .bin, see tools/aw_pack.py) from the
 * SD card into the external flash, gives the free RAM to the game's heap,
 * and implements the platform hooks (display, audio, buttons, menu,
 * savestates).
 *
 * Everything that reaches the firmware goes through the ABI bridge
 * (sdk/src/gw_core_bridge.*): this binary is not linked with the firmware.
 *
 * Distributed under the GPLv2 or later (same license as raw).
 */
#include <odroid_system.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "rg_storage.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"

#include "aw_platform.h"
#include "aw_pack.h"

#include "gw_core_i18n.h"
#include "gw_core_bridge.h" /* last: rewrites ACTIVE_FILE, common_emu_state, dma_counter */

#define AW_SAMPLE_RATE         22050
#define AW_AUDIO_BUFFER_LENGTH 552      /* 25 ms per DMA half buffer */
#define AW_TICK_MS             16       /* buttons / overlay rate */

static odroid_gamepad_state_t joystick;
static odroid_gamepad_state_t latched_buttons;

static const uint8_t *frame_page;      /* last presented page (4 bpp) */
static uint16_t frame_pal[16];
static uint32_t pair_lut[256];         /* two pixels (one byte) -> two RGB565 */
static uint16_t lut_pal[16];
static bool lut_valid;

static uint32_t last_dma_counter;
static uint32_t last_tick_ms;
static int in_service;
static bool game_running;

static const char *boot_state_path;
static const uint8_t *pack_base;

/* ------------------------------------------------------------------ */
/* Platform: time, files                                              */
/* ------------------------------------------------------------------ */
uint32_t aw_plat_time_ms(void)
{
    return get_elapsed_time();
}

int aw_plat_audio_rate(void)
{
    return AW_SAMPLE_RATE;
}

bool aw_plat_find_file(const char *name, const uint8_t **data, uint32_t *size)
{
    return aw_pack_find(name, data, size);
}

const uint8_t *aw_plat_pack_base(void)
{
    return pack_base;
}

static void show_error(const char *msg, const char *detail);

void aw_plat_fatal(const char *msg)
{
    audio_stop_playing();
    show_error("Another World error:", msg);
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* Display                                                            */
/* ------------------------------------------------------------------ */
static inline uint16_t blend565(uint16_t a, uint16_t b, uint32_t w /* 0..256 weight of b */)
{
    uint32_t x = (a | ((uint32_t)a << 16)) & 0x07E0F81F;
    uint32_t y = (b | ((uint32_t)b << 16)) & 0x07E0F81F;
    uint32_t r = (x + (((y - x) * (w >> 3)) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}

static void update_lut(void)
{
    if (lut_valid && memcmp(lut_pal, frame_pal, sizeof(lut_pal)) == 0)
        return;
    memcpy(lut_pal, frame_pal, sizeof(lut_pal));
    for (int i = 0; i < 256; i++)
        pair_lut[i] = lut_pal[i >> 4] | ((uint32_t)lut_pal[i & 15] << 16);
    lut_valid = true;
}

/* one line of the page (160 bytes) -> 320 RGB565 pixels */
static inline void expand_line(uint16_t *dst, const uint8_t *src)
{
    uint32_t *d = (uint32_t *)dst;
    for (int x = 0; x < AW_WIDTH / 2; x += 4) {
        d[x + 0] = pair_lut[src[x + 0]];
        d[x + 1] = pair_lut[src[x + 1]];
        d[x + 2] = pair_lut[src[x + 2]];
        d[x + 3] = pair_lut[src[x + 3]];
    }
}

/* Draw the 320x200 game screen into a 320x240 framebuffer. */
static void draw_frame(uint16_t *fb)
{
    if (!frame_page) {
        memset(fb, 0, GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);
        return;
    }
    update_lut();
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    if (scaling == ODROID_DISPLAY_SCALING_OFF) {
        /* 1:1, letterboxed */
        int top = (GW_LCD_HEIGHT - AW_HEIGHT) / 2;
        memset(fb, 0, top * GW_LCD_WIDTH * 2);
        for (int y = 0; y < AW_HEIGHT; y++)
            expand_line(fb + (top + y) * GW_LCD_WIDTH, frame_page + y * (AW_WIDTH / 2));
        memset(fb + (top + AW_HEIGHT) * GW_LCD_WIDTH, 0, (GW_LCD_HEIGHT - top - AW_HEIGHT) * GW_LCD_WIDTH * 2);
        return;
    }
    /* 320x200 -> 320x240: the 4:3 aspect ratio of the original game */
    bool smooth = odroid_display_get_filter_mode() != ODROID_DISPLAY_FILTER_OFF;
    static uint16_t line0[AW_WIDTH], line1[AW_WIDTH];
    int have0 = -1, have1 = -1;
    for (int y = 0; y < GW_LCD_HEIGHT; y++) {
        uint16_t *dst = fb + y * GW_LCD_WIDTH;
        uint32_t pos = (uint32_t)y * AW_HEIGHT * 256 / GW_LCD_HEIGHT; /* 8.8 fixed point */
        int sy = pos >> 8;
        uint32_t frac = pos & 0xFF;
        if (!smooth || frac == 0 || sy + 1 >= AW_HEIGHT) {
            expand_line(dst, frame_page + sy * (AW_WIDTH / 2));
            continue;
        }
        if (have0 != sy) {
            if (have1 == sy) {
                memcpy(line0, line1, sizeof(line0));
            } else {
                expand_line(line0, frame_page + sy * (AW_WIDTH / 2));
            }
            have0 = sy;
        }
        if (have1 != sy + 1) {
            expand_line(line1, frame_page + (sy + 1) * (AW_WIDTH / 2));
            have1 = sy + 1;
        }
        for (int x = 0; x < AW_WIDTH; x++) {
            uint16_t a = line0[x], b = line1[x];
            dst[x] = (a == b) ? a : blend565(a, b, frac);
        }
    }
}

void aw_plat_present(const uint8_t *page, const uint16_t *palette)
{
    frame_page = page;
    memcpy(frame_pal, palette, sizeof(frame_pal));
    /* The engine draws the next frame into another page: show this one
     * now (waits for the previous swap, at most one LCD refresh). */
    lcd_sleep_while_swap_pending();
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    common_ingame_overlay();
    lcd_swap();
}

static void repaint(void)
{
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    common_ingame_overlay();
}

static void *screenshot(void)
{
    lcd_wait_for_vblank();
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    return fb;
}

/* ------------------------------------------------------------------ */
/* Audio                                                              */
/* ------------------------------------------------------------------ */
static void feed_audio(void)
{
    if (dma_counter == last_dma_counter)
        return;
    last_dma_counter = dma_counter;
    int16_t *buf = audio_get_active_buffer();
    uint16_t len = audio_get_buffer_length();
    /* always render: the music player runs on this sample clock */
    aw_core_audio_render(buf, len);
    if (common_emu_sound_loop_is_muted()) {
        audio_clear_active_buffer();
        return;
    }
    int32_t factor = common_emu_sound_get_volume();
    for (int i = 0; i < len; i++)
        buf[i] = (int16_t)(((int32_t)buf[i] * factor) >> 8);
}

void aw_plat_poll(void)
{
    if (in_service)
        return;
    wdog_refresh();
    if (dma_counter != last_dma_counter) {
        in_service = 1;
        feed_audio();
        in_service = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Buttons                                                            */
/* ------------------------------------------------------------------ */
/*
 * Game & Watch      Another World
 *   D-pad            move (up = jump, down = crouch)
 *   A                action: run (with a direction), shoot, kick, charge
 *                    the gun (held)
 *   B                jump (same as up)
 *   GAME / START     pause
 *   PAUSE/SET        retro-go menu (savestates, ...)
 */
static uint32_t map_buttons(const odroid_gamepad_state_t *j)
{
    uint32_t b = 0;
    if (j->values[ODROID_INPUT_LEFT])  b |= AW_BTN_LEFT;
    if (j->values[ODROID_INPUT_RIGHT]) b |= AW_BTN_RIGHT;
    if (j->values[ODROID_INPUT_UP] || j->values[ODROID_INPUT_B]) b |= AW_BTN_UP;
    if (j->values[ODROID_INPUT_DOWN])  b |= AW_BTN_DOWN;
    if (j->values[ODROID_INPUT_A])     b |= AW_BTN_ACTION;
    if (j->values[ODROID_INPUT_START] || j->values[ODROID_INPUT_X]) b |= AW_BTN_PAUSE;
    return b;
}

/* ------------------------------------------------------------------ */
/* Service: called whenever the game waits or reads its inputs        */
/* ------------------------------------------------------------------ */
/*
 * The pause menu (common_emu_input_loop) must run where the game can be
 * saved: the safe point of the main loop (aw_plat_safe_point(), once per
 * game frame). Elsewhere the buttons are only remembered (latched_buttons)
 * so that a short press on PAUSE/SET is not lost. In the game's own pause
 * (GAME), the safe point is not reached: the menu opens from the service,
 * without savestates.
 */
static void run_input_loop(void)
{
    for (int i = 0; i < ODROID_INPUT_MAX; i++)
        joystick.values[i] |= latched_buttons.values[i];
    memset(&latched_buttons, 0, sizeof(latched_buttons));

    common_emu_input_loop(&joystick, NULL, &repaint);

    /* the time spent in the menu does not count */
    last_tick_ms = get_elapsed_time();
    last_dma_counter = dma_counter;
}

void aw_plat_service(int in_delay)
{
    if (in_service)
        return;
    in_service = 1;

    wdog_refresh();
    feed_audio();

    uint32_t now = get_elapsed_time();
    if (now - last_tick_ms >= AW_TICK_MS) {
        last_tick_ms = now;
        odroid_input_read_gamepad(&joystick);
        if (aw_core_game_paused()) {
            run_input_loop();
        } else {
            for (int i = 0; i < ODROID_INPUT_MAX; i++)
                latched_buttons.values[i] |= joystick.values[i];
        }
        aw_core_set_buttons(map_buttons(&joystick));
        /* volume / brightness overlays while the game waits */
        if (common_emu_state.overlay != 0 && !lcd_is_swap_pending()) {
            repaint();
            lcd_swap();
        }
    }

    in_service = 0;
    if (in_delay)
        __WFI(); /* SysTick (1 kHz) or the audio DMA wake us up */
}

void aw_plat_safe_point(void)
{
    if (in_service)
        return;
    in_service = 1;

    wdog_refresh();
    feed_audio();

    odroid_input_read_gamepad(&joystick);
    run_input_loop();
    aw_core_set_buttons(map_buttons(&joystick));

    in_service = 0;
}

/* ------------------------------------------------------------------ */
/* Savestates                                                         */
/* ------------------------------------------------------------------ */
static bool aw_load_state(const char *path)
{
    if (!game_running) {
        /* Called before the game starts: restored once it runs. */
        static char path_copy[256];
        snprintf(path_copy, sizeof(path_copy), "%s", path);
        boot_state_path = path_copy;
        return true;
    }
    return aw_core_load_state(path);
}

static bool aw_save_state(const char *path)
{
    return aw_core_save_state(path);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */
static const gw_i18n_entry_t i18n_no_pack[] = {
    { "en", "Game data not found:" }, { "fr", "Donn\xc3\xa9" "es du jeu introuvables :" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_bad_pack[] = {
    { "en", "Not a valid .aw file:" }, { "fr", "Fichier .aw invalide :" }, GW_I18N_END
};

static void show_error(const char *msg, const char *detail)
{
    printf("aw: %s %s\n", msg, detail);
    odroid_dialog_choice_t choices[] = {
        {0, msg, "", -1, NULL},
        {0, detail, "", -1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {1, "OK", "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST,
    };
    lcd_clear_buffers();
    odroid_overlay_dialog("Another World", choices, 3, NULL, 0);
    odroid_system_switch_app(0);
}

/* ".../Another World.bin" -> ".../Another World.aw" */
static void pack_path_for(const char *bin_path, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", bin_path);
    char *slash = strrchr(out, '/');
    char *dot = strrchr(out, '.');
    if (dot && (!slash || dot > slash))
        *dot = '\0';
    size_t len = strlen(out);
    snprintf(out + len, out_size - len, ".aw");
}

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    gw_core_bridge_init();
    printf("Another World start\n");

    odroid_system_init(APPID_HOMEBREW, AW_SAMPLE_RATE);
    odroid_system_emu_init(&aw_load_state, &aw_save_state, &screenshot, NULL, NULL, NULL, NULL);

    /* Same name as this .bin */
    static char pack_path[256];
    pack_path_for(ACTIVE_FILE->path, pack_path, sizeof(pack_path));
    const char *pack_name = strrchr(pack_path, '/');
    pack_name = pack_name ? pack_name + 1 : pack_path;

    /* Map the asset pack into the external flash */
    FILE *probe = fopen(pack_path, "rb");
    if (!probe) {
        show_error(gw_i18n(i18n_no_pack), pack_name);
        return;
    }
    fclose(probe);
    uint32_t pack_size = 0;
    uint8_t *pack = odroid_overlay_cache_file_in_flash(pack_path, &pack_size, false);
    int r = pack ? aw_pack_open(pack, pack_size) : AW_PACK_ERR_MAGIC;
    if (r != AW_PACK_OK) {
        show_error(gw_i18n(i18n_bad_pack), pack_name);
        return;
    }
    pack_base = pack;

    /* Everything left in RAM_EMU is the game's heap */
    size_t heap_size = ram_get_free_size();
    void *heap = ram_malloc(heap_size);
    aw_heap_init(heap, heap_size);
    printf("aw: pack %lu bytes, heap %u bytes\n", (unsigned long)pack_size, (unsigned)heap_size);

    if (load_state)
        odroid_system_emu_load_state(save_slot); /* only records the path */

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / 50 + 0.5f);

    lcd_clear_buffers();
    audio_start_playing(AW_AUDIO_BUFFER_LENGTH);
    last_dma_counter = dma_counter;
    last_tick_ms = get_elapsed_time();

    aw_core_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.load_state = boot_state_path;
    game_running = true;
    aw_core_run(&opts); /* never returns */
}
