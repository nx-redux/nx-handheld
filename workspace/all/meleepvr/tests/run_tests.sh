#!/bin/sh
# Host unit test for workspace/all/meleepvr/meleepvr.c via LD_PRELOAD / DYLD_INSERT_LIBRARIES.
set -eu
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/fake_egl_gl.c" <<'EOF'
#include <stdint.h>
#include <string.h>

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef unsigned int EGLBoolean;
typedef int32_t EGLint;
typedef void (*egl_proc_t)(void);

#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_DRAW 0x3059

static EGLDisplay s_dpy;
static EGLSurface s_cur_draw;
static EGLSurface s_win;
static int s_win_w, s_win_h;
static int s_pbuf_calls, s_destroy_calls, s_interval_calls, s_last_interval, s_clear_calls;
static int s_bx0, s_by0, s_bx1, s_by1;
static unsigned int s_bfilter;
static char s_last_shader[4096];

const char* fake_last_shader(void) { return s_last_shader; }

void fake_reset(EGLDisplay dpy, EGLSurface win_surf, int w, int h) {
	s_dpy = dpy;
	s_win = win_surf;
	s_win_w = w;
	s_win_h = h;
	s_cur_draw = NULL;
	s_pbuf_calls = s_destroy_calls = s_interval_calls = s_clear_calls = 0;
	s_last_interval = -1;
}
int fake_pbuffer_create_calls(void) { return s_pbuf_calls; }
int fake_destroy_calls(void) { return s_destroy_calls; }
int fake_swap_interval_calls(void) { return s_interval_calls; }
int fake_last_swap_interval(void) { return s_last_interval; }
int fake_clear_calls(void) { return s_clear_calls; }
void fake_last_blit(int* dx0, int* dy0, int* dx1, int* dy1, unsigned int* filter) {
	*dx0 = s_bx0; *dy0 = s_by0; *dx1 = s_bx1; *dy1 = s_by1; *filter = s_bfilter;
}

EGLDisplay eglGetCurrentDisplay(void) { return s_dpy; }
EGLSurface eglGetCurrentSurface(EGLint readdraw) { return readdraw == EGL_DRAW ? s_cur_draw : NULL; }
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surf, EGLint attr, EGLint* val) {
	(void)dpy;
	if (surf != s_win || !val) return 0;
	if (attr == EGL_WIDTH) { *val = s_win_w; return 1; }
	if (attr == EGL_HEIGHT) { *val = s_win_h; return 1; }
	return 0;
}
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint* attribs) {
	(void)dpy; (void)cfg; (void)attribs;
	s_pbuf_calls++;
	return (EGLSurface)(uintptr_t)(0x9000 + s_pbuf_calls);
}
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surf) {
	(void)dpy; (void)surf;
	s_destroy_calls++;
	return 1;
}
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
	(void)dpy; (void)read; (void)ctx;
	s_cur_draw = draw;
	return 1;
}
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
	(void)dpy;
	return surf == s_win ? 1 : 0;
}
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
	(void)dpy;
	s_interval_calls++;
	s_last_interval = interval;
	return 1;
}
void glClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
void glClear(uint32_t mask) { (void)mask; s_clear_calls++; }
void glBlitFramebuffer(int32_t sx0, int32_t sy0, int32_t sx1, int32_t sy1,
                       int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1,
                       uint32_t mask, uint32_t filter) {
	(void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)mask;
	s_bx0 = dx0; s_by0 = dy0; s_bx1 = dx1; s_by1 = dy1; s_bfilter = filter;
}
void glShaderSource(uint32_t shader, int32_t count, const char* const* string, const int32_t* length) {
	(void)shader;
	size_t off = 0;
	for (int32_t i = 0; i < count && string && string[i]; i++) {
		size_t n = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
		if (off + n >= sizeof(s_last_shader)) n = sizeof(s_last_shader) - 1 - off;
		memcpy(s_last_shader + off, string[i], n);
		off += n;
	}
	s_last_shader[off] = '\0';
}
egl_proc_t eglGetProcAddress(const char* name) {
	(void)name;
	return NULL;
}
EOF

if [ "$(uname -s)" = "Darwin" ]; then
	cc -dynamiclib -fpic -std=gnu99 "$TMP/fake_egl_gl.c" -o "$TMP/libfakeegl.dylib"
	cc -dynamiclib -fpic -std=gnu99 "$SRC/meleepvr.c" "$TMP/libfakeegl.dylib" -o "$TMP/libmeleepvr.dylib" -ldl
	cc -std=gnu99 -Wall -Wextra "$SRC/tests/test_meleepvr.c" "$TMP/libmeleepvr.dylib" "$TMP/libfakeegl.dylib" -o "$TMP/test_meleepvr"
	MELEEPVR_FORCE_ENABLE=1 "$TMP/test_meleepvr"
else
	cc -shared -fpic -std=gnu99 "$TMP/fake_egl_gl.c" -o "$TMP/libfakeegl.so"
	cc -shared -fpic -std=gnu99 "$SRC/meleepvr.c" -o "$TMP/libmeleepvr.so" -ldl -lpthread
	cc -std=gnu99 -Wall -Wextra "$SRC/tests/test_meleepvr.c" "$TMP/libfakeegl.so" -Wl,-rpath,"$TMP" -o "$TMP/test_meleepvr"
	MELEEPVR_FORCE_ENABLE=1 LD_PRELOAD="$TMP/libmeleepvr.so" "$TMP/test_meleepvr"
fi
