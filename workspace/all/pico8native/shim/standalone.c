/*
 * Standalone mode: Splore runs pico8_64 straight on the device's SDL (see standalone.h).
 *
 * PICO-8 draws through an SDL renderer: it uploads its 128x128 screen to a streaming
 * texture, copies that to the window and calls SDL_RenderPresent. The dialog runs inside
 * that present, on PICO-8's main thread, so the game is frozen while it is open: the
 * finished frame is read back from the renderer, and the dialog's own loop polls the real
 * events, draws the frame dimmed with the dialog over it and presents, until A or B.
 * PICO-8's audio device is paused meanwhile and its clock (SDL_GetTicks) does not see the
 * time pass. Every event the loop polls is the dialog's, and buttons still held when it
 * closes stay hidden from PICO-8 until let go (dialog.h). Quit hands PICO-8 an SDL_QUIT,
 * which it exits on as it does on SIGTERM, and every frame from then on is black.
 *
 * The text is NX Redux's font1.ttf through the device's SDL2_ttf, dlopen'ed when the
 * dialog first opens; without either, a small built-in bitmap font draws it instead.
 *
 * Reading the frame back, the letterbox state saved and cleared around drawing, and Menu
 * opening only on a release with nothing else pressed (not with NextUI's Menu+volume
 * brightness) follow josegonzalez/minui-pico-8-pak's shim/sdl-display.c (MIT licence).
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dialog.h"
#include "standalone.h"

#define LOG(...) fprintf(stderr, "pico8shim: " __VA_ARGS__)

// From SDL_events.h, SDL_gamecontroller.h, SDL_pixels.h, SDL_render.h, SDL_blendmode.h, SDL_audio.h
#define SDL_EVENT_SIZE 56
#define SDL_QUIT_EVENT 0x100
#define SDL_JOYBUTTONDOWN 0x603
#define SDL_JOYBUTTONUP 0x604
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP 0x652
#define PAD_A 0
#define PAD_B 1
#define PAD_GUIDE 5
#define PAD_BUTTONS 21
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_BLENDMODE_NONE 0
#define SDL_BLENDMODE_BLEND 1
#define SDL_AUDIO_PLAYING 1

// The TrimUI pad as the controller mapping Splore's launcher writes has it: SDL's A
// (south, PICO-8's O) is raw button 1, the pad's B; SDL's B (east, PICO-8's X) is raw
// 0, the pad's A; Menu (guide) raw 8. Used when PICO-8 opened the joystick only.
#define RAW_A 1
#define RAW_B 0
#define RAW_MENU 8

// How long PICO-8 has to exit after Quit: the SDL_QUIT goes again every second, and
// after this the shim ends it, so Splore's launcher never waits on a hung PICO-8
#define QUIT_MS 10000
#define INPUT_DEVICES 16

typedef struct {
	int x, y, w, h;
} SDL_Rect;
typedef struct {
	uint8_t r, g, b, a;
} SDL_Color;

int standalone;

static struct {
	void (*RenderPresent)(void*);
	int (*PollEvent)(void*);
	uint32_t (*GetTicks)(void);
	void (*Delay)(uint32_t);
	uint8_t (*GameControllerGetButton)(void*, int);
	void* (*GameControllerGetJoystick)(void*);
	uint8_t (*JoystickGetButton)(void*, int);
	int (*JoystickNumButtons)(void*);
	void (*PauseAudioDevice)(uint32_t, int);
	int (*GetAudioDeviceStatus)(uint32_t);
	int (*GetRendererOutputSize)(void*, int*, int*);
	int (*RenderReadPixels)(void*, const SDL_Rect*, uint32_t, void*, int);
	void* (*CreateTexture)(void*, uint32_t, int, int, int);
	int (*UpdateTexture)(void*, const SDL_Rect*, const void*, int);
	void (*DestroyTexture)(void*);
	void* (*CreateTextureFromSurface)(void*, void*);
	void (*FreeSurface)(void*);
	int (*QueryTexture)(void*, uint32_t*, int*, int*, int*);
	int (*SetTextureBlendMode)(void*, int);
	int (*RenderCopy)(void*, void*, const SDL_Rect*, const SDL_Rect*);
	int (*RenderClear)(void*);
	int (*RenderFillRect)(void*, const SDL_Rect*);
	int (*GetRenderDrawColor)(void*, uint8_t*, uint8_t*, uint8_t*, uint8_t*);
	int (*SetRenderDrawColor)(void*, uint8_t, uint8_t, uint8_t, uint8_t);
	int (*GetRenderDrawBlendMode)(void*, int*);
	int (*SetRenderDrawBlendMode)(void*, int);
	void* (*GetRenderTarget)(void*);
	int (*SetRenderTarget)(void*, void*);
	void (*RenderGetLogicalSize)(void*, int*, int*);
	int (*RenderSetLogicalSize)(void*, int, int);
	void (*RenderGetViewport)(void*, SDL_Rect*);
	int (*RenderSetViewport)(void*, const SDL_Rect*);
	void (*RenderGetScale)(void*, float*, float*);
	int (*RenderSetScale)(void*, float, float);
	int (*RenderIsClipEnabled)(void*);
	void (*RenderGetClipRect)(void*, SDL_Rect*);
	int (*RenderSetClipRect)(void*, const SDL_Rect*);
	const char* (*GetError)(void);
} sdl;
static int can_draw;

// SDL2_ttf, from the device's own libraries
static struct {
	int (*Init)(void);
	void* (*OpenFont)(const char*, int);
	void (*CloseFont)(void*);
	void* (*RenderUTF8_Blended)(void*, const char*, SDL_Color);
} ttf;

static struct dlg dlg;
static int open_pending;
static int quitting;
static uint32_t quit_at, quit_sent_at;
static int quit_sent;
static uint32_t paused_ms; // time the dialog was open, kept from PICO-8's clock
static uint32_t audio_dev;
static void *pad, *joy;
static int input_fds[INPUT_DEVICES];
static int input_count = -1;

#define RESOLVE(name) (*(void**)& sdl.name = dlsym(RTLD_NEXT, "SDL_" #name))

static void resolve(void) {
	static int done;
	if (done)
		return;
	done = 1;
	RESOLVE(RenderPresent);
	RESOLVE(PollEvent);
	RESOLVE(GetTicks);
	RESOLVE(Delay);
	RESOLVE(GameControllerGetButton);
	RESOLVE(GameControllerGetJoystick);
	RESOLVE(JoystickGetButton);
	RESOLVE(JoystickNumButtons);
	RESOLVE(PauseAudioDevice);
	RESOLVE(GetAudioDeviceStatus);
	RESOLVE(SetTextureBlendMode);
	RESOLVE(GetError);
	// what drawing the dialog needs; without it the dialog still works, over a frozen screen
	can_draw = RESOLVE(GetRendererOutputSize) && RESOLVE(RenderReadPixels) && RESOLVE(CreateTexture) &&
			   RESOLVE(UpdateTexture) && RESOLVE(DestroyTexture) && RESOLVE(CreateTextureFromSurface) &&
			   RESOLVE(FreeSurface) && RESOLVE(QueryTexture) && RESOLVE(RenderCopy) && RESOLVE(RenderClear) &&
			   RESOLVE(RenderFillRect) && RESOLVE(GetRenderDrawColor) && RESOLVE(SetRenderDrawColor) &&
			   RESOLVE(GetRenderDrawBlendMode) && RESOLVE(SetRenderDrawBlendMode) && RESOLVE(GetRenderTarget) &&
			   RESOLVE(SetRenderTarget) && RESOLVE(RenderGetLogicalSize) && RESOLVE(RenderSetLogicalSize) &&
			   RESOLVE(RenderGetViewport) && RESOLVE(RenderSetViewport) && RESOLVE(RenderGetScale) &&
			   RESOLVE(RenderSetScale) && RESOLVE(RenderIsClipEnabled) && RESOLVE(RenderGetClipRect) &&
			   RESOLVE(RenderSetClipRect);
	if (!can_draw)
		LOG("this SDL2 lacks a renderer function the quit dialog draws with; it will not show\n");
	LOG("standalone: SDL goes straight through, Menu opens the quit dialog\n");
}

static uint32_t now_ms(void) {
	return sdl.GetTicks ? sdl.GetTicks() : 0;
}

/* ---- input ---- */

// NextUI changes the brightness with Menu+volume; the volume keys are not on the pad, so
// they are read from the input devices, as minui-pico-8-pak does. 1 if one went down.
static int volume_pressed(void) {
	if (input_count < 0) {
		input_count = 0;
		for (int i = 0; i < INPUT_DEVICES; i++) {
			char path[32];
			snprintf(path, sizeof(path), "/dev/input/event%d", i);
			int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd >= 0)
				input_fds[input_count++] = fd;
		}
	}
	int pressed = 0;
	struct input_event e;
	for (int i = 0; i < input_count; i++)
		while (read(input_fds[i], &e, sizeof(e)) == (ssize_t)sizeof(e))
			if (e.type == EV_KEY && (e.code == KEY_VOLUMEUP || e.code == KEY_VOLUMEDOWN) && e.value == 1)
				pressed = 1;
	return pressed;
}

static int raw_down(void* j, int button) {
	return j && sdl.JoystickGetButton && (!sdl.JoystickNumButtons || sdl.JoystickNumButtons(j) > button) &&
		   sdl.JoystickGetButton(j, button);
}

// The pad as it is now, one bit per SDL controller button, and the dialog's mask in *mask
static uint32_t read_pad(unsigned* mask) {
	uint32_t held = 0;
	int menu;
	if (pad && sdl.GameControllerGetButton) {
		for (int b = 0; b < PAD_BUTTONS; b++)
			if (sdl.GameControllerGetButton(pad, b))
				held |= 1u << b;
		// Menu as the mapping's guide, or as its raw button should the mapping lack it
		menu = (held >> PAD_GUIDE) & 1 ||
			   raw_down(sdl.GameControllerGetJoystick ? sdl.GameControllerGetJoystick(pad) : NULL, RAW_MENU);
	} else {
		held = (uint32_t)raw_down(joy, RAW_A) << PAD_A | (uint32_t)raw_down(joy, RAW_B) << PAD_B;
		menu = raw_down(joy, RAW_MENU);
	}
	// the dialog goes by the labels on the pad: its A (Quit) is SDL's B, its B (Cancel) SDL's A
	*mask = (menu ? DLG_MENU : 0) | ((held >> PAD_B) & 1 ? DLG_A : 0) | ((held >> PAD_A) & 1 ? DLG_B : 0) |
			(held & ~(1u << PAD_A | 1u << PAD_B | 1u << PAD_GUIDE) ? DLG_OTHER : 0);
	return held;
}

// Once a frame while the game runs: Menu let go on its own asks for the dialog
static void sample(void) {
	unsigned mask;
	uint32_t held = read_pad(&mask);
	dlg_released(&dlg, held);
	if (quitting || open_pending)
		return;
	if (dlg_game(&dlg, mask, volume_pressed()) == DLG_OPEN) {
		open_pending = 1;
		LOG("Menu: opening the quit dialog\n");
	}
}

// Input PICO-8 must not see: Menu ever, a button hidden since the dialog closed, and
// everything once Quit was chosen
static int hidden_event(const uint8_t* ev) {
	uint32_t type;
	memcpy(&type, ev, 4);
	if (quitting && type >= 0x300 && type < 0x900)
		return 1;
	int button = ev[12]; // SDL_ControllerButtonEvent and SDL_JoyButtonEvent: type, timestamp, which, button
	if (type == SDL_CONTROLLERBUTTONDOWN || type == SDL_CONTROLLERBUTTONUP)
		return button == PAD_GUIDE || dlg_hides(&dlg, button);
	if (type == SDL_JOYBUTTONDOWN || type == SDL_JOYBUTTONUP)
		return button == RAW_MENU || (button == RAW_A && dlg_hides(&dlg, PAD_A)) ||
			   (button == RAW_B && dlg_hides(&dlg, PAD_B));
	return 0;
}

/* ---- drawing ---- */

// The few characters the dialog says, 5x7, for when there is no font1.ttf or SDL2_ttf
static const struct {
	char c;
	uint8_t rows[7];
} glyphs[] = {
	{'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
	{'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}},
	{'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}},
	{'t', {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}},
	{'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
	{'p', {0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10}},
	{'l', {0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
	{'o', {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}},
	{'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
	{'e', {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}},
	{'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
	{'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
	{'a', {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}},
	{'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
	{'c', {0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e}},
	{'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
	{'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
};

struct label {
	const char* text;
	SDL_Color colour;
	void* texture; // from SDL2_ttf, or NULL for the bitmap font
	int scale;	   // the bitmap font's pixel size
	int w, h;
};

static void fill(void* r, int x, int y, int w, int h, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
	SDL_Rect rect = {x, y, w, h};
	sdl.SetRenderDrawColor(r, red, green, blue, alpha);
	sdl.RenderFillRect(r, &rect);
}

static int isqrt(int v) {
	int x = 0;
	while ((x + 1) * (x + 1) <= v)
		x++;
	return x;
}

// A filled rect with corners of radius rad, one row at a time so no pixel blends twice
static void rounded(void* r, int x, int y, int w, int h, int rad, SDL_Color c) {
	if (rad * 2 > h)
		rad = h / 2;
	if (rad * 2 > w)
		rad = w / 2;
	for (int dy = 0; dy < rad; dy++) {
		int off = 2 * (rad - dy) - 1; // twice the row's distance from the corners' centre
		int inset = rad - isqrt((4 * rad * rad - off * off) / 4);
		fill(r, x + inset, y + dy, w - 2 * inset, 1, c.r, c.g, c.b, c.a);
		fill(r, x + inset, y + h - 1 - dy, w - 2 * inset, 1, c.r, c.g, c.b, c.a);
	}
	fill(r, x, y + rad, w, h - 2 * rad, c.r, c.g, c.b, c.a);
}

static void* open_font(int px) {
	static int tried;
	if (!tried) {
		tried = 1;
		void* lib = dlopen("libSDL2_ttf-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
		if (lib) {
			*(void**)&ttf.Init = dlsym(lib, "TTF_Init");
			*(void**)&ttf.OpenFont = dlsym(lib, "TTF_OpenFont");
			*(void**)&ttf.CloseFont = dlsym(lib, "TTF_CloseFont");
			*(void**)&ttf.RenderUTF8_Blended = dlsym(lib, "TTF_RenderUTF8_Blended");
		}
		if (!lib || !ttf.Init || !ttf.OpenFont || !ttf.CloseFont || !ttf.RenderUTF8_Blended || ttf.Init() != 0) {
			LOG("no SDL2_ttf (%s), the quit dialog uses its own font\n", lib ? "TTF_Init failed" : dlerror());
			ttf.OpenFont = NULL;
		}
	}
	if (!ttf.OpenFont)
		return NULL;
	const char* sd = getenv("SDCARD_PATH");
	char path[512];
	snprintf(path, sizeof(path), "%s/.system/res/font1.ttf", sd && *sd ? sd : "/mnt/SDCARD");
	void* font = ttf.OpenFont(path, px);
	if (!font)
		LOG("cannot open %s, the quit dialog uses its own font\n", path);
	return font;
}

static void make_label(void* r, void* font, int px, struct label* l) {
	l->texture = NULL;
	if (font) {
		void* surface = ttf.RenderUTF8_Blended(font, l->text, l->colour);
		if (surface) {
			l->texture = sdl.CreateTextureFromSurface(r, surface);
			sdl.FreeSurface(surface);
		}
		if (l->texture && sdl.QueryTexture(l->texture, NULL, NULL, &l->w, &l->h) == 0) {
			if (sdl.SetTextureBlendMode)
				sdl.SetTextureBlendMode(l->texture, SDL_BLENDMODE_BLEND);
			return;
		}
		if (l->texture)
			sdl.DestroyTexture(l->texture);
		l->texture = NULL;
	}
	l->scale = px / 7 > 1 ? px / 7 : 1;
	l->w = ((int)strlen(l->text) * 6 - 1) * l->scale;
	l->h = 7 * l->scale;
}

static void draw_label(void* r, const struct label* l, int x, int y) {
	if (l->texture) {
		SDL_Rect dst = {x, y, l->w, l->h};
		sdl.RenderCopy(r, l->texture, NULL, &dst);
		return;
	}
	int s = l->scale;
	for (const char* p = l->text; *p; p++, x += 6 * s) {
		const uint8_t* rows = NULL;
		for (size_t g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); g++)
			if (glyphs[g].c == *p)
				rows = glyphs[g].rows;
		for (int row = 0; rows && row < 7; row++)
			for (int col = 0; col < 5;) { // a run of lit pixels is one rect
				if (!((rows[row] >> (4 - col)) & 1)) {
					col++;
					continue;
				}
				int start = col;
				while (col < 5 && ((rows[row] >> (4 - col)) & 1))
					col++;
				fill(r, x + start * s, y + row * s, (col - start) * s, s, l->colour.r, l->colour.g, l->colour.b,
					 l->colour.a);
			}
	}
}

// The renderer state PICO-8 draws with, put aside while the dialog draws over the whole screen
struct render_state {
	void* target;
	int logical_w, logical_h, clip_on, blend;
	float scale_x, scale_y;
	SDL_Rect viewport, clip;
	uint8_t r, g, b, a;
};

static void save_state(void* r, struct render_state* s) {
	s->target = sdl.GetRenderTarget(r);
	if (s->target)
		sdl.SetRenderTarget(r, NULL);
	sdl.RenderGetLogicalSize(r, &s->logical_w, &s->logical_h);
	sdl.RenderGetViewport(r, &s->viewport);
	sdl.RenderGetScale(r, &s->scale_x, &s->scale_y);
	s->clip_on = sdl.RenderIsClipEnabled(r);
	sdl.RenderGetClipRect(r, &s->clip);
	sdl.GetRenderDrawColor(r, &s->r, &s->g, &s->b, &s->a);
	sdl.GetRenderDrawBlendMode(r, &s->blend);
	sdl.RenderSetLogicalSize(r, 0, 0);
	sdl.RenderSetScale(r, 1.0f, 1.0f);
	sdl.RenderSetViewport(r, NULL);
	sdl.RenderSetClipRect(r, NULL);
}

static void restore_state(void* r, const struct render_state* s) {
	if (s->logical_w > 0 && s->logical_h > 0)
		sdl.RenderSetLogicalSize(r, s->logical_w, s->logical_h);
	sdl.RenderSetScale(r, s->scale_x, s->scale_y);
	sdl.RenderSetViewport(r, &s->viewport);
	sdl.RenderSetClipRect(r, s->clip_on ? &s->clip : NULL);
	sdl.SetRenderDrawColor(r, s->r, s->g, s->b, s->a);
	sdl.SetRenderDrawBlendMode(r, s->blend);
	if (s->target)
		sdl.SetRenderTarget(r, s->target);
}

struct look {
	int w, h;
	void* frame; // the game's last frame, or NULL to draw over black
	struct label title, a, quit, b, cancel;
	int box_x, box_y, box_w, box_h, radius, pad, gap, dot, space, wide;
};

// Sized from the screen's height: 768 on tg5040 (1024x768), 720 on tg5050 (1280x720)
static void layout(void* r, struct look* k) {
	int title_px = k->h * 5 / 100, hint_px = k->h * 34 / 1000;
	void* title_font = open_font(title_px);
	void* hint_font = title_font ? open_font(hint_px) : NULL;
	if (!hint_font && title_font) { // one font but not the other: both bitmap, so they match
		ttf.CloseFont(title_font);
		title_font = NULL;
	}
	k->title = (struct label){"Quit Splore?", {0xff, 0xff, 0xff, 0xff}, NULL, 0, 0, 0};
	k->quit = (struct label){"Quit", {0xc8, 0xc8, 0xc8, 0xff}, NULL, 0, 0, 0};
	k->cancel = (struct label){"Cancel", {0xc8, 0xc8, 0xc8, 0xff}, NULL, 0, 0, 0};
	k->a = (struct label){"A", {0x1c, 0x1c, 0x20, 0xff}, NULL, 0, 0, 0};
	k->b = (struct label){"B", {0x1c, 0x1c, 0x20, 0xff}, NULL, 0, 0, 0};
	make_label(r, title_font, title_px, &k->title);
	make_label(r, hint_font, hint_px, &k->quit);
	make_label(r, hint_font, hint_px, &k->cancel);
	make_label(r, hint_font, hint_px * 8 / 10, &k->a);
	make_label(r, hint_font, hint_px * 8 / 10, &k->b);
	if (title_font)
		ttf.CloseFont(title_font);
	if (hint_font)
		ttf.CloseFont(hint_font);

	k->pad = k->h * 45 / 1000;
	k->gap = k->h * 3 / 100;
	k->radius = k->h * 2 / 100;
	k->dot = k->quit.h * 115 / 100; // the round button behind A and B
	k->space = k->h * 12 / 1000;
	k->wide = k->h * 45 / 1000;
	int hints_w = 2 * k->dot + 2 * k->space + k->quit.w + k->wide + k->cancel.w;
	int inner = k->title.w > hints_w ? k->title.w : hints_w;
	k->box_w = inner + 3 * k->pad;
	if (k->box_w < k->h * 55 / 100)
		k->box_w = k->h * 55 / 100;
	k->box_h = 2 * k->pad + k->title.h + k->gap + (k->dot > k->quit.h ? k->dot : k->quit.h);
	k->box_x = (k->w - k->box_w) / 2;
	k->box_y = (k->h - k->box_h) / 2;
}

static void draw_hint(void* r, const struct look* k, const struct label* letter, const struct label* text, int x,
					  int y) {
	int row = k->dot > text->h ? k->dot : text->h;
	rounded(r, x, y + (row - k->dot) / 2, k->dot, k->dot, k->dot / 2, (SDL_Color){0xf0, 0xf0, 0xf0, 0xff});
	draw_label(r, letter, x + (k->dot - letter->w) / 2, y + (row - letter->h) / 2);
	draw_label(r, text, x + k->dot + k->space, y + (row - text->h) / 2);
}

static void draw_dialog(void* r, const struct look* k) {
	sdl.SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	sdl.SetRenderDrawColor(r, 0, 0, 0, 0xff);
	sdl.RenderClear(r);
	if (k->frame)
		sdl.RenderCopy(r, k->frame, NULL, NULL);
	sdl.SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	fill(r, 0, 0, k->w, k->h, 0, 0, 0, 0xa0); // the dim
	rounded(r, k->box_x, k->box_y, k->box_w, k->box_h, k->radius, (SDL_Color){0x1c, 0x1c, 0x20, 0xf0});

	draw_label(r, &k->title, k->box_x + (k->box_w - k->title.w) / 2, k->box_y + k->pad);
	int hints_w = 2 * k->dot + 2 * k->space + k->quit.w + k->wide + k->cancel.w;
	int x = k->box_x + (k->box_w - hints_w) / 2, y = k->box_y + k->pad + k->title.h + k->gap;
	draw_hint(r, k, &k->a, &k->quit, x, y);
	draw_hint(r, k, &k->b, &k->cancel, x + k->dot + k->space + k->quit.w + k->wide, y);
}

static void free_look(struct look* k) {
	struct label* labels[] = {&k->title, &k->a, &k->quit, &k->b, &k->cancel};
	for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
		if (labels[i]->texture)
			sdl.DestroyTexture(labels[i]->texture);
	if (k->frame)
		sdl.DestroyTexture(k->frame);
}

// The game's frame as PICO-8 drew it, before it is presented
static void* read_frame(void* r, int w, int h) {
	uint32_t* pixels = malloc((size_t)w * h * 4);
	if (!pixels)
		return NULL;
	void* t = NULL;
	if (sdl.RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, w * 4) != 0)
		LOG("cannot read the game's frame, the quit dialog is drawn over black: %s\n",
			sdl.GetError ? sdl.GetError() : "");
	else if ((t = sdl.CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h))) {
		for (size_t i = 0; i < (size_t)w * h; i++)
			pixels[i] |= 0xff000000u; // the back buffer's alpha means nothing
		sdl.UpdateTexture(t, NULL, pixels, w * 4);
	}
	free(pixels);
	return t;
}

/* ---- the dialog ---- */

static int audio_pause(void) {
	if (!audio_dev || !sdl.PauseAudioDevice ||
		(sdl.GetAudioDeviceStatus && sdl.GetAudioDeviceStatus(audio_dev) != SDL_AUDIO_PLAYING))
		return 0; // a device PICO-8 paused itself stays as it is
	sdl.PauseAudioDevice(audio_dev, 1);
	return 1;
}

static void black(void* r) {
	if (!can_draw)
		return;
	uint8_t red, green, blue, alpha;
	sdl.GetRenderDrawColor(r, &red, &green, &blue, &alpha);
	sdl.SetRenderDrawColor(r, 0, 0, 0, 0xff);
	sdl.RenderClear(r);
	sdl.SetRenderDrawColor(r, red, green, blue, alpha);
}

static void run_dialog(void* r) {
	uint32_t opened = now_ms();
	int paused = audio_pause();
	LOG("quit dialog open%s\n", paused ? ", PICO-8's audio paused" : "");

	struct render_state saved;
	struct look k;
	memset(&k, 0, sizeof(k));
	int draw = can_draw && r && sdl.GetRendererOutputSize(r, &k.w, &k.h) == 0 && k.w > 0 && k.h > 0;
	if (can_draw && !draw)
		LOG("cannot read the screen size, the quit dialog will not show: %s\n", sdl.GetError ? sdl.GetError() : "");
	if (draw) {
		save_state(r, &saved);
		k.frame = read_frame(r, k.w, k.h);
		layout(r, &k);
	}

	enum dlg_action act = DLG_NONE;
	while (act == DLG_NONE) {
		uint8_t ev[SDL_EVENT_SIZE];
		int sigquit = 0;
		while (sdl.PollEvent && sdl.PollEvent(ev)) { // the dialog's: none reach PICO-8
			uint32_t type;
			memcpy(&type, ev, 4);
			sigquit |= type == SDL_QUIT_EVENT; // power-off's SIGTERM: quit as A does
		}
		unsigned mask;
		read_pad(&mask);
		act = sigquit ? DLG_QUIT : dlg_open_step(&dlg, mask);
		if (act != DLG_NONE)
			break;
		if (draw) {
			draw_dialog(r, &k);
			sdl.RenderPresent(r);
		}
		if (sdl.Delay)
			sdl.Delay(16);
	}

	unsigned mask;
	dlg_close(&dlg, read_pad(&mask));
	if (act == DLG_QUIT) {
		quitting = 1;
		quit_at = now_ms();
		LOG("quit dialog: Quit, sending PICO-8 SDL_QUIT\n");
	} else {
		if (paused)
			sdl.PauseAudioDevice(audio_dev, 0);
		LOG("quit dialog: Cancel, back to PICO-8\n");
	}
	if (draw) {
		// the back buffer goes back to the game's frame, which the present after this shows
		sdl.SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
		sdl.SetRenderDrawColor(r, 0, 0, 0, 0xff);
		sdl.RenderClear(r);
		if (k.frame && !quitting)
			sdl.RenderCopy(r, k.frame, NULL, NULL);
		free_look(&k);
		restore_state(r, &saved);
	}
	paused_ms += now_ms() - opened;
}

/* ---- hooks ---- */

void st_present(void* renderer) {
	static int logged;
	resolve();
	if (!logged) {
		logged = 1;
		LOG("first present\n");
	}
	if (!sdl.RenderPresent)
		return;
	if (!quitting) {
		sample();
		if (open_pending) {
			open_pending = 0;
			run_dialog(renderer);
		}
	}
	if (quitting) { // the screen stays dark while PICO-8 exits
		uint32_t since = now_ms() - quit_at;
		if (since > QUIT_MS) {
			LOG("PICO-8 did not exit %d s after Quit, ending it\n", QUIT_MS / 1000);
			_exit(0);
		}
		if (quit_sent && now_ms() - quit_sent_at > 1000) {
			LOG("PICO-8 still running after Quit, sending SDL_QUIT again\n");
			quit_sent = 0;
		}
		black(renderer);
	}
	sdl.RenderPresent(renderer);
}

int st_poll_event(void* event) {
	resolve();
	if (quitting && !quit_sent) {
		if (event) {
			uint32_t ev[SDL_EVENT_SIZE / 4] = {SDL_QUIT_EVENT, st_ticks()};
			memcpy(event, ev, sizeof(ev));
			quit_sent = 1;
			quit_sent_at = now_ms();
		}
		return 1;
	}
	if (!sdl.PollEvent)
		return 0;
	if (!event)
		return sdl.PollEvent(NULL); // only asks whether one is waiting; nothing to drop
	for (;;) {
		if (!sdl.PollEvent(event)) {
			if (!quitting)
				sample(); // PICO-8 has read all its events for this frame
			return 0;
		}
		if (!hidden_event(event))
			return 1;
	}
}

uint32_t st_ticks(void) {
	resolve();
	return now_ms() - paused_ms;
}

void st_audio_opened(uint32_t dev) {
	audio_dev = dev;
}

void st_pad_opened(void* opened) {
	if (opened) {
		pad = opened;
		LOG("controller opened, Menu opens the quit dialog\n");
	}
}

void st_pad_closed(void* closed) {
	if (closed == pad)
		pad = NULL;
}

void st_joystick_opened(void* opened) {
	if (opened && !joy)
		joy = opened;
}

uint8_t st_pad_button(void* p, int button) {
	resolve();
	if (quitting || button == PAD_GUIDE || dlg_hides(&dlg, button) || !sdl.GameControllerGetButton)
		return 0;
	return sdl.GameControllerGetButton(p, button);
}

uint8_t st_joy_button(void* j, int button) {
	resolve();
	if (quitting || button == RAW_MENU || (button == RAW_A && dlg_hides(&dlg, PAD_A)) ||
		(button == RAW_B && dlg_hides(&dlg, PAD_B)) || !sdl.JoystickGetButton)
		return 0;
	return sdl.JoystickGetButton(j, button);
}
