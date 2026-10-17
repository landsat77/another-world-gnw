/*
 * Another World on the raw engine (src/raw), single threaded, for the Game &
 * Watch and the Linux harness.
 *
 * GnwSystem is the System of the engine on top of aw_platform.h:
 *  - display: the engine's 4 bit video page and its palette go to the
 *    platform as they are (it converts them to the LCD);
 *  - sound: the platform pulls the samples (aw_core_audio_render()); the
 *    timer of the music player (SDL timer in the original) runs on the sample
 *    clock of that output, so the music keeps its tempo whatever the speed
 *    of the game;
 *  - time and sleep come from the platform; every wait of the engine runs
 *    the platform service (audio refill, buttons);
 *  - the mutexes are not needed (one thread).
 *
 * The main loop is Engine::run(), plus the safe point where the platform
 * runs its pause menu (savestates).
 *
 * Distributed under the GPLv2 or later (same license as raw).
 */
#include "engine.h"
#include "sys.h"
#include "parts.h"
#include "aw_platform.h"

#define AW_MAX_TIMERS 4

struct GnwSystem : System {
	uint16_t _pal565[NUM_COLORS];
	uint32_t _buttons;
	uint32_t _prevButtons;

	struct Timer {
		bool active;
		int id;
		int64_t remaining; // in 1/1000 samples (ms * rate)
		TimerCallback cb;
		void *param;
		uint32_t interval;
	};
	Timer _timers[AW_MAX_TIMERS];
	int _nextTimerId;
	bool _removedInCallback;
	int _callbackId;

	void init(const char *title) {
		(void)title;
		memset(_pal565, 0, sizeof(_pal565));
		memset(&input, 0, sizeof(input));
		memset(_timers, 0, sizeof(_timers));
		_buttons = _prevButtons = 0;
		_nextTimerId = 1;
		_callbackId = 0;
		_removedInCallback = false;
	}
	void destroy() {}

	// The palette resources hold 16 colors of 12 bits (0x0RGB, big endian)
	void setPalette(const uint8_t *p) {
		for (int i = 0; i < NUM_COLORS; ++i) {
			uint8_t c1 = p[i * 2 + 0];
			uint8_t c2 = p[i * 2 + 1];
			uint32_t r = (c1 & 0x0F) * 17, g = (c2 >> 4) * 17, b = (c2 & 0x0F) * 17;
			_pal565[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
	void updateDisplay(const uint8_t *buf) {
		aw_plat_present(buf, _pal565);
	}

	void applyButtons() {
		uint32_t b = _buttons;
		input.dirMask = 0;
		if (b & AW_BTN_LEFT)  input.dirMask |= PlayerInput::DIR_LEFT;
		if (b & AW_BTN_RIGHT) input.dirMask |= PlayerInput::DIR_RIGHT;
		if (b & AW_BTN_UP)    input.dirMask |= PlayerInput::DIR_UP;
		if (b & AW_BTN_DOWN)  input.dirMask |= PlayerInput::DIR_DOWN;
		input.button = (b & AW_BTN_ACTION) != 0;
		// the engine toggles its pause on each press
		if ((b & AW_BTN_PAUSE) && !(_prevButtons & AW_BTN_PAUSE))
			input.pause = true;
		_prevButtons = b;
	}
	void processEvents() {
		aw_plat_service(0);
		applyButtons();
	}
	void sleep(uint32_t duration) {
		uint32_t start = aw_plat_time_ms();
		do {
			aw_plat_service(1);
		} while (aw_plat_time_ms() - start < duration);
		applyButtons();
	}
	uint32_t getTimeStamp() {
		return aw_plat_time_ms();
	}

	void startAudio(AudioCallback callback, void *param) { (void)callback; (void)param; }
	void stopAudio() {}
	uint32_t getOutputSampleRate() { return aw_plat_audio_rate(); }

	int addTimer(uint32_t delay, TimerCallback callback, void *param) {
		for (int i = 0; i < AW_MAX_TIMERS; ++i) {
			Timer *t = &_timers[i];
			if (!t->active) {
				t->active = true;
				t->id = _nextTimerId++;
				if (_nextTimerId <= 0)
					_nextTimerId = 1;
				t->interval = delay ? delay : 1;
				t->remaining = (int64_t)t->interval * getOutputSampleRate();
				t->cb = callback;
				t->param = param;
				return t->id;
			}
		}
		warning("GnwSystem::addTimer() no free timer");
		return 0;
	}
	void removeTimer(int timerId) {
		if (timerId <= 0)
			return;
		for (int i = 0; i < AW_MAX_TIMERS; ++i) {
			if (_timers[i].active && _timers[i].id == timerId) {
				_timers[i].active = false;
				if (timerId == _callbackId)
					_removedInCallback = true;
			}
		}
	}

	void *createMutex() { return this; }
	void destroyMutex(void *mutex) { (void)mutex; }
	void lockMutex(void *mutex) { (void)mutex; }
	void unlockMutex(void *mutex) { (void)mutex; }

	// Samples until the next timer is due (INT32_MAX: none)
	int32_t samplesToNextTimer() {
		int64_t best = INT32_MAX;
		for (int i = 0; i < AW_MAX_TIMERS; ++i) {
			if (_timers[i].active) {
				int64_t n = (_timers[i].remaining + 999) / 1000;
				if (n < best)
					best = n;
			}
		}
		return best < 0 ? 0 : (int32_t)best;
	}
	void advanceTimers(int samples) {
		for (int i = 0; i < AW_MAX_TIMERS; ++i)
			if (_timers[i].active)
				_timers[i].remaining -= (int64_t)samples * 1000;
	}
	void fireDueTimers() {
		for (int i = 0; i < AW_MAX_TIMERS; ++i) {
			Timer *t = &_timers[i];
			int guard = 0;
			while (t->active && t->remaining <= 0 && guard++ < 16) {
				_callbackId = t->id;
				_removedInCallback = false;
				uint32_t next = t->cb(t->interval, t->param);
				_callbackId = 0;
				if (_removedInCallback || !t->active)
					break;
				if (next == 0) {
					t->active = false;
					break;
				}
				t->interval = next;
				t->remaining += (int64_t)next * getOutputSampleRate();
			}
		}
	}
};

static GnwSystem *g_sys;
static Engine *g_engine;
static bool g_at_safe_point;
static char g_state_tag[32];

/* Identifies the game data in the savestates: a state made with other data
 * (another version of the game) would crash the engine. */
static void make_state_tag(void)
{
	const uint8_t *p;
	uint32_t size;
	uint32_t h = 2166136261u;
	if (aw_plat_find_file("MEMLIST.BIN", &p, &size)) {
		for (uint32_t i = 0; i < size; i++)
			h = (h ^ p[i]) * 16777619u;
	}
	// the bytecode of the parts too (language versions share MEMLIST.BIN)
	for (int part = 0; part < GAME_NUM_PARTS; part++) {
		char name[16];
		snprintf(name, sizeof(name), "RES%03u", (unsigned)memListParts[part][MEMLIST_PART_CODE]);
		if (aw_plat_find_file(name, &p, &size)) {
			h = (h ^ size) * 16777619u;
			for (uint32_t i = 0; i < size; i += 61)
				h = (h ^ p[i]) * 16777619u;
		}
	}
	snprintf(g_state_tag, sizeof(g_state_tag), "gnw-aw1 %08lx", (unsigned long)h);
}

extern "C" void aw_core_set_buttons(uint32_t buttons)
{
	if (g_sys)
		g_sys->_buttons = buttons;
}

extern "C" void aw_core_audio_render(int16_t *out, int frames)
{
	memset(out, 0, frames * sizeof(int16_t));
	if (!g_engine)
		return;
	while (frames > 0) {
		int32_t n = g_sys->samplesToNextTimer();
		if (n == 0) {
			g_sys->fireDueTimers();
			n = g_sys->samplesToNextTimer();
			if (n == 0)
				n = 1;
		}
		if (n > frames)
			n = frames;
		g_engine->mixer.mix16(out, n);
		g_sys->advanceTimers(n);
		out += n;
		frames -= n;
	}
	g_sys->fireDueTimers();
}

extern "C" bool aw_core_can_save(void)
{
	return g_engine && g_at_safe_point;
}

extern "C" bool aw_core_save_state(const char *path)
{
	if (!aw_core_can_save())
		return false;
	return g_engine->saveGameStateToPath(path, g_state_tag);
}

extern "C" bool aw_core_load_state(const char *path)
{
	if (!aw_core_can_save())
		return false;
	return g_engine->loadGameStateFromPath(path, g_state_tag);
}

extern bool gnw_aw_game_paused; // vm.cpp

extern "C" bool aw_core_game_paused(void)
{
	return gnw_aw_game_paused;
}

extern "C" uint16_t aw_core_current_part(void)
{
	return g_engine ? g_engine->res.currentPartId : 0;
}

extern "C" void aw_core_run(const aw_core_options_t *opts)
{
	// operator new of the SDK is a bump allocator in RAM_EMU, which the
	// platform gave to the game heap: build the objects there.
	void *mem = aw_calloc(1, sizeof(GnwSystem));
	if (!mem)
		aw_plat_fatal("out of memory (system)");
	g_sys = new (mem) GnwSystem();
	mem = aw_calloc(1, sizeof(Engine));
	if (!mem)
		aw_plat_fatal("out of memory (engine)");
	Engine *e = new (mem) Engine(g_sys, "", "");

	make_state_tag();
	e->init();
	g_engine = e;
	if (opts && opts->start_part >= GAME_PART_FIRST && opts->start_part <= GAME_PART_LAST)
		e->vm.initForPart(opts->start_part);

	bool boot_load = opts && opts->load_state;
	while (1) {
		e->vm.checkThreadRequests();

		g_at_safe_point = true;
		if (boot_load) {
			boot_load = false;
			if (!e->loadGameStateFromPath(opts->load_state, g_state_tag))
				printf("aw: could not load %s\n", opts->load_state);
		}
		aw_plat_safe_point();
		g_at_safe_point = false;

		e->vm.inp_updatePlayer();
		e->vm.hostFrame();
	}
}
