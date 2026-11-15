// libmeleepvr.so — LD_PRELOAD synchronous window presenter for zalo/melee on PowerVR.
//
// Why this exists:
// 1. On PortMaster, melee.aarch64 runs through bmdhacks' SDL3-over-SDL2 shim
//    (SDL_VIDEODRIVER=sdl2), where MeleeFlipNativeWindow() is NULL. Dawn's
//    SwapChainEGL therefore creates an offscreen EGLPbufferSurface and relies on
//    present_worker.cpp's second shared GLES context to blit each rendered texture
//    into SDL's real window EGLSurface.
// 2. On Imagination PowerVR Rogue GE8300 (pvrsrvkm 1.19, Trimui Brick / Brick Pro /
//    Smart Pro), present_worker.cpp's cross-context glWaitSync + glClientWaitSync
//    stalls on the kernel driver lock for 148-465 ms every frame (~2.15 FPS).
// 3. Setting MELEE_FLIP_PRESENT_THREAD=0 eliminates the cross-context stall (~1 ms
//    present, full 60 Hz logic speed), but disables present_worker.cpp — leaving
//    Dawn blitting into its invisible offscreen pbuffer while the physical screen
//    stays completely black.
//
// Fix:
// Capture SDL2's real window EGLSurface during display.cpp's initSdl()
// (eglGetCurrentSurface(EGL_DRAW)), and return that window EGLSurface directly
// when Dawn's SwapChainEGL requests its (>1x1) pbuffer surface. Because PowerVR
// supports EGL_KHR_no_config_context and EGL_KHR_surfaceless_context, Dawn's own
// render context binds, blits (reverse-Y + GL_LINEAR), and swaps the physical
// panel surface directly on a single context with zero cross-context sync waits.
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef unsigned int EGLBoolean;
typedef int32_t EGLint;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);

typedef int32_t GLint;
typedef int32_t GLsizei;
typedef uint32_t GLbitfield;
typedef uint32_t GLenum;
typedef float GLfloat;

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NONE 0x3038
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_DRAW 0x3059

#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LINEAR 0x2601
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D

typedef char GLchar;
typedef unsigned int GLuint;
typedef __eglMustCastToProperFunctionPointerType (*pfn_eglGetProcAddress)(const char*);
typedef EGLDisplay (*pfn_eglGetCurrentDisplay)(void);
typedef EGLSurface (*pfn_eglGetCurrentSurface)(EGLint);
typedef EGLBoolean (*pfn_eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint*);
typedef EGLSurface (*pfn_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLBoolean (*pfn_eglDestroySurface)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*pfn_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*pfn_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*pfn_eglSwapInterval)(EGLDisplay, EGLint);
typedef void (*pfn_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (*pfn_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*pfn_glClear)(GLbitfield);
typedef void (*pfn_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*pfn_glCompileShader)(GLuint);
typedef void (*pfn_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void (*pfn_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*pfn_glLinkProgram)(GLuint);
typedef void (*pfn_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*pfn_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*pfn_glProgramBinary)(GLuint, GLenum, const void*, GLsizei);
typedef void (*pfn_glUniform1uiv)(GLint, GLsizei, const GLuint*);
typedef GLenum (*pfn_glGetError)(void);
typedef GLint (*pfn_glGetUniformLocation)(GLuint, const char*);

static pfn_eglGetProcAddress real_eglGetProcAddress;
static pfn_eglGetCurrentDisplay real_eglGetCurrentDisplay;
static pfn_eglGetCurrentSurface real_eglGetCurrentSurface;
static pfn_eglQuerySurface real_eglQuerySurface;
static pfn_eglCreatePbufferSurface real_eglCreatePbufferSurface;
static pfn_eglDestroySurface real_eglDestroySurface;
static pfn_eglMakeCurrent real_eglMakeCurrent;
static pfn_eglSwapBuffers real_eglSwapBuffers;
static pfn_eglSwapInterval real_eglSwapInterval;
static pfn_glBlitFramebuffer real_glBlitFramebuffer;
static pfn_glClearColor real_glClearColor;
static pfn_glClear real_glClear;
static pfn_glShaderSource real_glShaderSource;
static pfn_glCompileShader real_glCompileShader;
static pfn_glGetShaderiv real_glGetShaderiv;
static pfn_glGetShaderInfoLog real_glGetShaderInfoLog;
static pfn_glLinkProgram real_glLinkProgram;
static pfn_glGetProgramiv real_glGetProgramiv;
static pfn_glGetProgramInfoLog real_glGetProgramInfoLog;
static pfn_glProgramBinary real_glProgramBinary;
static pfn_glUniform1uiv real_glUniform1uiv;
static pfn_glGetError real_glGetError;
static pfn_glGetUniformLocation real_glGetUniformLocation;

static pthread_mutex_t g_glsl_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_checked_active = 0;
static int g_active = 0;

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_window_surface = EGL_NO_SURFACE;
static EGLContext g_interval_ctx = EGL_NO_CONTEXT;
static EGLint g_panel_w = 0;
static EGLint g_panel_h = 0;
static int g_hijacked = 0;
static __thread int t_in_window_present = 0;

static int is_active(void) {
	if (g_checked_active)
		return g_active;

	const char* dis = getenv("MELEEPVR_DISABLE");
	if (dis && strcmp(dis, "1") == 0) {
		g_active = 0;
		g_checked_active = 1;
		return 0;
	}
	const char* force = getenv("MELEEPVR_FORCE_ENABLE");
	if (force && strcmp(force, "1") == 0) {
		g_active = 1;
		g_checked_active = 1;
		return 1;
	}

	char comm[64] = {0};
	int fd = open("/proc/self/comm", O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, comm, sizeof(comm) - 1);
		close(fd);
		if (n > 0) {
			comm[n] = '\0';
			if (strncmp(comm, "melee", 5) == 0)
				g_active = 1;
		}
	}
	g_checked_active = 1;
	return g_active;
}

static void* resolve_sym(const char* name, void* self_fn, int try_egl_proc) {
	void* fn = dlsym(RTLD_NEXT, name);
	if (fn && fn != self_fn)
		return fn;

	const char* libs[] = {"libEGL.so.1", "libEGL.so", "libGLESv2.so.2", "libGLESv2.so", NULL};
	for (int i = 0; libs[i]; i++) {
		void* h = dlopen(libs[i], RTLD_LAZY | RTLD_NOLOAD);
		if (!h)
			continue;
		fn = dlsym(h, name);
		dlclose(h);
		if (fn && fn != self_fn)
			return fn;
	}

	if (try_egl_proc) {
		if (!real_eglGetProcAddress)
			real_eglGetProcAddress = (pfn_eglGetProcAddress)resolve_sym("eglGetProcAddress", NULL, 0);
		if (real_eglGetProcAddress) {
			fn = (void*)real_eglGetProcAddress(name);
			if (fn && fn != self_fn)
				return fn;
		}
	}
	return NULL;
}

static void capture_window_surface(EGLDisplay dpy, EGLSurface surf) {
	if (!surf || g_window_surface != EGL_NO_SURFACE || g_hijacked)
		return;
	if (!dpy) {
		if (!real_eglGetCurrentDisplay)
			real_eglGetCurrentDisplay = (pfn_eglGetCurrentDisplay)resolve_sym("eglGetCurrentDisplay", NULL, 0);
		if (real_eglGetCurrentDisplay)
			dpy = real_eglGetCurrentDisplay();
	}
	if (!real_eglQuerySurface)
		real_eglQuerySurface = (pfn_eglQuerySurface)resolve_sym("eglQuerySurface", NULL, 0);

	EGLint w = 0, h = 0;
	if (dpy && real_eglQuerySurface) {
		real_eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
		real_eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
	}
	// Ignore tiny 1x1 scratch surfaces; SDL's panel window surface is >= 320x240.
	if (w > 1 && h > 1) {
		g_display = dpy;
		g_window_surface = surf;
		g_panel_w = w;
		g_panel_h = h;
	} else if (!real_eglQuerySurface) {
		g_display = dpy;
		g_window_surface = surf;
	}
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
	if (!real_eglGetCurrentSurface)
		real_eglGetCurrentSurface = (pfn_eglGetCurrentSurface)resolve_sym("eglGetCurrentSurface", (void*)eglGetCurrentSurface, 0);
	if (!real_eglGetCurrentSurface)
		return EGL_NO_SURFACE;

	EGLSurface surf = real_eglGetCurrentSurface(readdraw);
	if (is_active() && readdraw == EGL_DRAW && surf != EGL_NO_SURFACE)
		capture_window_surface(EGL_NO_DISPLAY, surf);
	return surf;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
	if (!real_eglCreatePbufferSurface)
		real_eglCreatePbufferSurface = (pfn_eglCreatePbufferSurface)resolve_sym("eglCreatePbufferSurface", (void*)eglCreatePbufferSurface, 1);

	if (is_active() && g_window_surface != EGL_NO_SURFACE && attrib_list) {
		EGLint req_w = 0, req_h = 0;
		for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
			if (attrib_list[i] == EGL_WIDTH)
				req_w = attrib_list[i + 1];
			else if (attrib_list[i] == EGL_HEIGHT)
				req_h = attrib_list[i + 1];
		}
		// SwapChainEGL requests the swapchain dimensions (e.g. 512x384 or 256x192);
		// ContextEGL's compatibility pbuffer (if ever used) is 1x1.
		if (req_w > 1 && req_h > 1) {
			g_hijacked = 1;
			fprintf(stderr, "[meleepvr] bound Dawn swapchain (%dx%d) directly to SDL window surface (%dx%d)\n",
					(int)req_w, (int)req_h, (int)g_panel_w, (int)g_panel_h);
			return g_window_surface;
		}
	}

	if (!real_eglCreatePbufferSurface)
		return EGL_NO_SURFACE;
	return real_eglCreatePbufferSurface(dpy, config, attrib_list);
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
	if (is_active() && g_hijacked && surface == g_window_surface) {
		// Keep SDL2's window EGLSurface alive until SDL_DestroyWindow cleans up.
		g_hijacked = 0;
		return EGL_TRUE;
	}
	if (!real_eglDestroySurface)
		real_eglDestroySurface = (pfn_eglDestroySurface)resolve_sym("eglDestroySurface", (void*)eglDestroySurface, 1);
	if (!real_eglDestroySurface)
		return EGL_FALSE;
	return real_eglDestroySurface(dpy, surface);
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
	if (!real_eglMakeCurrent)
		real_eglMakeCurrent = (pfn_eglMakeCurrent)resolve_sym("eglMakeCurrent", (void*)eglMakeCurrent, 1);
	if (!real_eglMakeCurrent)
		return EGL_FALSE;

	EGLBoolean ok = real_eglMakeCurrent(dpy, draw, read, ctx);
	if (!ok || !is_active()) {
		t_in_window_present = 0;
		return ok;
	}

	if (draw != EGL_NO_SURFACE && g_window_surface == EGL_NO_SURFACE && !g_hijacked)
		capture_window_surface(dpy, draw);

	if (g_hijacked && draw == g_window_surface && ctx != EGL_NO_CONTEXT) {
		t_in_window_present = 1;
		if (ctx != g_interval_ctx) {
			g_interval_ctx = ctx;
			if (!real_eglSwapInterval)
				real_eglSwapInterval = (pfn_eglSwapInterval)resolve_sym("eglSwapInterval", NULL, 1);
			if (real_eglSwapInterval) {
				const char* iv = getenv("MELEE_FLIP_SWAP_INTERVAL");
				int interval = (iv && iv[0]) ? atoi(iv) : 0;
				real_eglSwapInterval(dpy, interval);
			}
		}
	} else {
		t_in_window_present = 0;
	}
	return ok;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
	if (!real_eglSwapBuffers)
		real_eglSwapBuffers = (pfn_eglSwapBuffers)resolve_sym("eglSwapBuffers", (void*)eglSwapBuffers, 1);
	if (!real_eglSwapBuffers)
		return EGL_FALSE;
	return real_eglSwapBuffers(dpy, surface);
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
					   GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
					   GLbitfield mask, GLenum filter) {
	if (!real_glBlitFramebuffer)
		real_glBlitFramebuffer = (pfn_glBlitFramebuffer)resolve_sym("glBlitFramebuffer", (void*)glBlitFramebuffer, 1);
	if (!real_glBlitFramebuffer)
		return;

	// Preserve aspect ratio (4:3 pillarboxed on 16:9 Smart Pro; full 1024x768 on Brick/Brick Pro)
	// during SwapChainEGL::PresentImpl()'s reverse-Y blit onto the panel surface.
	if (is_active() && t_in_window_present && g_panel_w > 0 && g_panel_h > 0 &&
		srcX0 == 0 && srcY0 == 0 && srcX1 > 0 && srcY1 > 0 &&
		dstX0 == 0 && dstY0 == g_panel_h && dstX1 == g_panel_w && dstY1 == 0) {
		double frame_aspect = (double)srcX1 / (double)srcY1;
		GLint dw = g_panel_w;
		GLint dh = g_panel_h;
		if (frame_aspect * (double)g_panel_h > (double)g_panel_w)
			dh = (GLint)((double)g_panel_w / frame_aspect + 0.5);
		else
			dw = (GLint)((double)g_panel_h * frame_aspect + 0.5);

		GLint x0 = (g_panel_w - dw) / 2;
		GLint y0 = (g_panel_h - dh) / 2;
		if (dw != g_panel_w || dh != g_panel_h) {
			if (!real_glClearColor)
				real_glClearColor = (pfn_glClearColor)resolve_sym("glClearColor", NULL, 1);
			if (!real_glClear)
				real_glClear = (pfn_glClear)resolve_sym("glClear", NULL, 1);
			if (real_glClearColor && real_glClear) {
				real_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				real_glClear(GL_COLOR_BUFFER_BIT);
			}
		}
		real_glBlitFramebuffer(0, 0, srcX1, srcY1, x0, y0 + dh, x0 + dw, y0, mask, GL_LINEAR);
		return;
	}

	real_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

// PowerVR Rogue GE8300's GLSL linker shares the same explicit-location uniform
// array trimming bug documented in zalo/melee's mesa_quirks.h: Dawn's Tint
// backend emits `layout(location = 0) uniform uint tint_immediates[16];` where
// batched GX shaders only read `tint_immediates[3u]` (_pad = uniform record
// index 0..3). Trimming unused trailing elements [4..15] leaves location 0
// marked inactive inside the driver (`loc[3] = -1`), so glUniform1uiv(0, 16, .)
// is silently ignored with GL_NO_ERROR and 3 out of every 4 uniform-table slots
// read slot 0's matrices, colors, and texture atlas cells (missing buildings,
// missing body parts, and mixed textures).
//
// Referencing the last element `tint_immediates[N-1]` in an unreachable runtime
// branch at the end of main() keeps the full N-element array live at location 0
// without disabling PowerVR's hardware Early-Z / HSR.
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
	if (!real_glShaderSource)
		real_glShaderSource = (pfn_glShaderSource)resolve_sym("glShaderSource", (void*)glShaderSource, 1);
	if (!real_glShaderSource)
		return;

	if (!is_active() || count <= 0 || !string) {
		real_glShaderSource(shader, count, string, length);
		return;
	}

	size_t total = 0;
	for (GLsizei i = 0; i < count; i++) {
		if (!string[i])
			continue;
		if (length && length[i] >= 0)
			total += (size_t)length[i];
		else
			total += strlen(string[i]);
	}

	char* buf = (char*)malloc(total + 512);
	if (!buf) {
		pthread_mutex_lock(&g_glsl_mutex);
		real_glShaderSource(shader, count, string, length);
		pthread_mutex_unlock(&g_glsl_mutex);
		return;
	}
	size_t off = 0;
	for (GLsizei i = 0; i < count; i++) {
		if (!string[i])
			continue;
		size_t seg = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
		memcpy(buf + off, string[i], seg);
		off += seg;
	}
	buf[off] = '\0';

	int modified = 0;

	// Bug 1 (PowerVR Rogue GE8300 libglslcompiler): Tint emits module-scope
	// `bool continue_execution = true;` for every GX alphaCompare (`discard`)
	// fragment shader without assigning `true` inside main(). PowerVR leaves
	// uninitialized-in-main globals at 0 (`false`), causing `if (!continue_execution) discard;`
	// to discard 100% of alpha-tested geometry (stage buildings, Fox's scarf,
	// tree trunks, HUD nameplates). Initialize `continue_execution = true;` at
	// the start of main().
	if (strstr(buf, "bool continue_execution = true;") != NULL) {
		char* main_fn = strstr(buf, "void main() {");
		if (main_fn) {
			char* insert_at = main_fn + strlen("void main() {");
			const char* init_stmt = "\n  continue_execution = true;";
			size_t init_len = strlen(init_stmt);
			size_t move_len = strlen(insert_at) + 1;
			memmove(insert_at + init_len, insert_at, move_len);
			memcpy(insert_at, init_stmt, init_len);
			modified = 1;
		}
	}

	// Bug 2 (PowerVR explicit-location uniform array trimming, cf. mesa_quirks.h):
	const char* decl = strstr(buf, "uniform uint tint_immediates[");
	if (decl) {
		unsigned int arr_len = 0;
		if (sscanf(decl, "uniform uint tint_immediates[%u]", &arr_len) == 1 && arr_len > 1) {
			char target[80] = {0};
			if (strstr(buf, "gl_Position") != NULL) {
				strcpy(target, "gl_Position");
			} else {
				const char* out_decl = strstr(buf, "layout(location = 0) out vec4 ");
				if (out_decl) {
					out_decl += strlen("layout(location = 0) out vec4 ");
					size_t k = 0;
					while (out_decl[k] && out_decl[k] != ';' && out_decl[k] != ' ' && k + 1 < sizeof(target)) {
						target[k] = out_decl[k];
						k++;
					}
					target[k] = '\0';
				}
			}

			char* last_brace = strrchr(buf, '}');
			if (target[0] && last_brace) {
				char tail_copy[128];
				size_t tail_len = strlen(last_brace);
				if (tail_len < sizeof(tail_copy)) {
					memcpy(tail_copy, last_brace, tail_len + 1);
					int n = snprintf(last_brace, 256,
									 "  if (tint_immediates[%uu] == 3735928559u) { %s.x += 1e-30f; }\n%s",
									 arr_len - 1u, target, tail_copy);
					if (n > 0)
						modified = 1;
				}
			}
		}
	}

	if (modified) {
		const GLchar* patched = buf;
		pthread_mutex_lock(&g_glsl_mutex);
		real_glShaderSource(shader, 1, &patched, NULL);
		pthread_mutex_unlock(&g_glsl_mutex);
		free(buf);
		return;
	}

	free(buf);
	pthread_mutex_lock(&g_glsl_mutex);
	real_glShaderSource(shader, count, string, length);
	pthread_mutex_unlock(&g_glsl_mutex);
}

void glCompileShader(GLuint shader) {
	if (!real_glCompileShader)
		real_glCompileShader = (pfn_glCompileShader)resolve_sym("glCompileShader", (void*)glCompileShader, 1);
	if (!real_glCompileShader)
		return;
	if (!is_active()) {
		real_glCompileShader(shader);
		return;
	}
	pthread_mutex_lock(&g_glsl_mutex);
	real_glCompileShader(shader);
	if (!real_glGetShaderiv)
		real_glGetShaderiv = (pfn_glGetShaderiv)resolve_sym("glGetShaderiv", NULL, 1);
	if (real_glGetShaderiv) {
		GLint ok = 1;
		real_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			if (!real_glGetShaderInfoLog)
				real_glGetShaderInfoLog = (pfn_glGetShaderInfoLog)resolve_sym("glGetShaderInfoLog", NULL, 1);
			char log[1024] = {0};
			if (real_glGetShaderInfoLog)
				real_glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
			fprintf(stderr, "[meleepvr] SHADER COMPILE FAILED (shader=%u): %s\n", shader, log);
		}
	}
	pthread_mutex_unlock(&g_glsl_mutex);
}

void glLinkProgram(GLuint program) {
	if (!real_glLinkProgram)
		real_glLinkProgram = (pfn_glLinkProgram)resolve_sym("glLinkProgram", (void*)glLinkProgram, 1);
	if (!real_glLinkProgram)
		return;
	if (!is_active()) {
		real_glLinkProgram(program);
		return;
	}
	pthread_mutex_lock(&g_glsl_mutex);
	real_glLinkProgram(program);
	if (!real_glGetProgramiv)
		real_glGetProgramiv = (pfn_glGetProgramiv)resolve_sym("glGetProgramiv", NULL, 1);
	if (real_glGetProgramiv) {
		GLint ok = 1;
		real_glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			if (!real_glGetProgramInfoLog)
				real_glGetProgramInfoLog = (pfn_glGetProgramInfoLog)resolve_sym("glGetProgramInfoLog", NULL, 1);
			char log[1024] = {0};
			if (real_glGetProgramInfoLog)
				real_glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
			fprintf(stderr, "[meleepvr] PROGRAM LINK FAILED (prog=%u): %s\n", program, log);
		}
	}
	pthread_mutex_unlock(&g_glsl_mutex);
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length) {
	if (!real_glProgramBinary)
		real_glProgramBinary = (pfn_glProgramBinary)resolve_sym("glProgramBinary", (void*)glProgramBinary, 1);
	if (!real_glProgramBinary)
		return;
	if (!is_active()) {
		real_glProgramBinary(program, binaryFormat, binary, length);
		return;
	}
	pthread_mutex_lock(&g_glsl_mutex);
	real_glProgramBinary(program, binaryFormat, binary, length);
	pthread_mutex_unlock(&g_glsl_mutex);
}

#define MELEEPVR_LOC3_TAG 0x20000000
static int g_uniform_trim_mode = -1; // -1 = unprobed, 0 = full 16 ok, 1 = trimmed array

GLint glGetUniformLocation(GLuint program, const char* name) {
	if (!real_glGetUniformLocation)
		real_glGetUniformLocation = (pfn_glGetUniformLocation)resolve_sym("glGetUniformLocation", (void*)glGetUniformLocation, 1);
	if (!real_glGetUniformLocation)
		return -1;

	GLint loc = real_glGetUniformLocation(program, name);
	if (is_active() && name && strcmp(name, "tint_immediates") == 0) {
		GLint loc3 = real_glGetUniformLocation(program, "tint_immediates[3]");
		if (loc < 0 && loc3 >= 0)
			return MELEEPVR_LOC3_TAG | loc3;
	}
	return loc;
}

void glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
	if (!real_glUniform1uiv)
		real_glUniform1uiv = (pfn_glUniform1uiv)resolve_sym("glUniform1uiv", (void*)glUniform1uiv, 1);
	if (!real_glUniform1uiv || !value)
		return;

	if (is_active() && (location & MELEEPVR_LOC3_TAG) != 0) {
		GLint loc3 = location & ~MELEEPVR_LOC3_TAG;
		if (count > 3)
			real_glUniform1uiv(loc3, 1, &value[3]);
		return;
	}

	if (is_active() && count == 16) {
		if (g_uniform_trim_mode < 0) {
			if (!real_glGetError)
				real_glGetError = (pfn_glGetError)resolve_sym("glGetError", NULL, 1);
			if (real_glGetError) {
				while (real_glGetError() != GL_NO_ERROR) {}
				real_glUniform1uiv(location, count, value);
				GLenum err = real_glGetError();
				fprintf(stderr, "[meleepvr] glUniform1uiv(loc=%d, count=16) -> err=0x%x\n",
						(int)location, (unsigned)err);
				if (err != GL_NO_ERROR) {
					g_uniform_trim_mode = 1;
					real_glUniform1uiv(location, 4, value);
					GLenum err4 = real_glGetError();
					fprintf(stderr, "[meleepvr] fallback glUniform1uiv(loc=%d, count=4) -> err=0x%x\n",
							(int)location, (unsigned)err4);
					if (err4 != GL_NO_ERROR) {
						real_glUniform1uiv(location + 3, 1, &value[3]);
						GLenum err1 = real_glGetError();
						fprintf(stderr, "[meleepvr] fallback glUniform1uiv(loc+3=%d, count=1) -> err=0x%x\n",
								(int)(location + 3), (unsigned)err1);
						if (err1 == GL_NO_ERROR)
							g_uniform_trim_mode = 2;
					}
				} else {
					g_uniform_trim_mode = 0;
				}
				return;
			}
		} else if (g_uniform_trim_mode == 1) {
			real_glUniform1uiv(location, 4, value);
			return;
		} else if (g_uniform_trim_mode == 2) {
			real_glUniform1uiv(location + 3, 1, &value[3]);
			return;
		}
	}

	real_glUniform1uiv(location, count, value);
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) {
	if (!real_eglGetProcAddress)
		real_eglGetProcAddress = (pfn_eglGetProcAddress)resolve_sym("eglGetProcAddress", (void*)eglGetProcAddress, 0);

	if (procname && is_active()) {
		if (strcmp(procname, "eglGetCurrentSurface") == 0)
			return (__eglMustCastToProperFunctionPointerType)eglGetCurrentSurface;
		if (strcmp(procname, "eglCreatePbufferSurface") == 0)
			return (__eglMustCastToProperFunctionPointerType)eglCreatePbufferSurface;
		if (strcmp(procname, "eglDestroySurface") == 0)
			return (__eglMustCastToProperFunctionPointerType)eglDestroySurface;
		if (strcmp(procname, "eglMakeCurrent") == 0)
			return (__eglMustCastToProperFunctionPointerType)eglMakeCurrent;
		if (strcmp(procname, "eglSwapBuffers") == 0)
			return (__eglMustCastToProperFunctionPointerType)eglSwapBuffers;
		if (strcmp(procname, "glBlitFramebuffer") == 0)
			return (__eglMustCastToProperFunctionPointerType)glBlitFramebuffer;
		if (strcmp(procname, "glShaderSource") == 0)
			return (__eglMustCastToProperFunctionPointerType)glShaderSource;
		if (strcmp(procname, "glCompileShader") == 0)
			return (__eglMustCastToProperFunctionPointerType)glCompileShader;
		if (strcmp(procname, "glLinkProgram") == 0)
			return (__eglMustCastToProperFunctionPointerType)glLinkProgram;
		if (strcmp(procname, "glProgramBinary") == 0)
			return (__eglMustCastToProperFunctionPointerType)glProgramBinary;
		if (strcmp(procname, "glGetUniformLocation") == 0)
			return (__eglMustCastToProperFunctionPointerType)glGetUniformLocation;
		if (strcmp(procname, "glUniform1uiv") == 0)
			return (__eglMustCastToProperFunctionPointerType)glUniform1uiv;
	}

	if (!real_eglGetProcAddress)
		return NULL;
	return real_eglGetProcAddress(procname);
}
