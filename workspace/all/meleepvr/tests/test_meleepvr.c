// Host unit test for workspace/all/meleepvr/meleepvr.c: verifies window surface
// capture, 1x1 vs swapchain pbuffer discrimination, window surface lifetime
// protection, swap interval setup, and 4:3 aspect-ratio blitting on 16:9 panels.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef unsigned int EGLBoolean;
typedef int32_t EGLint;
typedef void (*egl_proc_t)(void);

#define EGL_NONE 0x3038
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_DRAW 0x3059
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601

// Provided by fake_egl_gl.c
void fake_reset(EGLDisplay dpy, EGLSurface win_surf, int w, int h);
int fake_pbuffer_create_calls(void);
int fake_destroy_calls(void);
int fake_swap_interval_calls(void);
int fake_last_swap_interval(void);
int fake_clear_calls(void);
void fake_last_blit(int* dstX0, int* dstY0, int* dstX1, int* dstY1, unsigned int* filter);
const char* fake_last_shader(void);

// Interposed by libmeleepvr
EGLSurface eglGetCurrentSurface(EGLint readdraw);
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint* attribs);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surf);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf);
void glBlitFramebuffer(int32_t sx0, int32_t sy0, int32_t sx1, int32_t sy1,
					   int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1,
					   uint32_t mask, uint32_t filter);
void glShaderSource(uint32_t shader, int32_t count, const char* const* string, const int32_t* length);
egl_proc_t eglGetProcAddress(const char* name);

int main(void) {
	EGLDisplay dpy = (EGLDisplay)(uintptr_t)0x1000;
	EGLSurface win = (EGLSurface)(uintptr_t)0x2000;
	EGLContext sdl_ctx = (EGLContext)(uintptr_t)0x3000;
	EGLContext dawn_ctx = (EGLContext)(uintptr_t)0x4000;

	// 1. Simulate display.cpp initSdl() on a 1280x720 16:9 Smart Pro panel
	fake_reset(dpy, win, 1280, 720);
	assert(eglMakeCurrent(dpy, win, win, sdl_ctx) == 1);
	assert(eglGetCurrentSurface(EGL_DRAW) == win);
	assert(eglMakeCurrent(dpy, NULL, NULL, NULL) == 1);

	// 2. A 1x1 compatibility pbuffer (ContextEGL) must NOT hijack SDL's window surface
	const EGLint tiny_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
	EGLSurface compat = eglCreatePbufferSurface(dpy, NULL, tiny_attribs);
	assert(compat != win);
	assert(fake_pbuffer_create_calls() == 1);

	// 3. Dawn's SwapChainEGL (>1x1) pbuffer request returns SDL's window surface directly
	const EGLint sc_attribs[] = {EGL_WIDTH, 512, EGL_HEIGHT, 384, EGL_NONE};
	typedef EGLSurface (*pfn_create_pbuf)(EGLDisplay, EGLConfig, const EGLint*);
	pfn_create_pbuf create_via_proc = (pfn_create_pbuf)eglGetProcAddress("eglCreatePbufferSurface");
	assert(create_via_proc != NULL);
	EGLSurface sc_surf = create_via_proc(dpy, NULL, sc_attribs);
	assert(sc_surf == win);
	assert(fake_pbuffer_create_calls() == 1); // no real offscreen pbuffer allocated

	// 4. Present scope binds win to dawn_ctx, sets swap interval 0 once, and pillarboxes 4:3 into 1280x720
	assert(eglMakeCurrent(dpy, sc_surf, sc_surf, dawn_ctx) == 1);
	assert(fake_swap_interval_calls() == 1);
	assert(fake_last_swap_interval() == 0);

	glBlitFramebuffer(0, 0, 512, 384, 0, 720, 1280, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
	unsigned int filt = 0;
	fake_last_blit(&dx0, &dy0, &dx1, &dy1, &filt);
	assert(dx0 == 160 && dy0 == 720 && dx1 == 1120 && dy1 == 0);
	assert(filt == GL_LINEAR);
	assert(fake_clear_calls() == 1);

	assert(eglSwapBuffers(dpy, sc_surf) == 1);
	assert(eglMakeCurrent(dpy, NULL, NULL, dawn_ctx) == 1);

	// Second frame on same context does not re-spam eglSwapInterval
	assert(eglMakeCurrent(dpy, sc_surf, sc_surf, dawn_ctx) == 1);
	assert(fake_swap_interval_calls() == 1);

	// 5. SwapChainEGL::DetachFromSurfaceImpl must NOT destroy SDL's real window surface
	assert(eglDestroySurface(dpy, sc_surf) == 1);
	assert(fake_destroy_calls() == 0);
	// Subsequent destroy (SDL_DestroyWindow at shutdown) destroys it normally
	assert(eglDestroySurface(dpy, win) == 1);
	assert(fake_destroy_calls() == 1);

	// 6. glShaderSource prevents PowerVR tint_immediates[16] trimming & initializes continue_execution
	const char* vs_src =
		"#version 320 es\n"
		"layout(location = 0) uniform uint tint_immediates[16];\n"
		"void main() {\n"
		"  gl_Position = vec4(0.0f);\n"
		"}\n";
	glShaderSource(1, 1, &vs_src, NULL);
	assert(strstr(fake_last_shader(), "tint_immediates[15u]") != NULL);

	const char* fs_src =
		"#version 320 es\n"
		"bool continue_execution = true;\n"
		"layout(location = 0) out vec4 fs_out;\n"
		"void main() {\n"
		"  fs_out = vec4(1.0f);\n"
		"}\n";
	glShaderSource(2, 1, &fs_src, NULL);
	assert(strstr(fake_last_shader(), "continue_execution = true;\n  fs_out") != NULL);

	printf("test_meleepvr: all tests passed\n");
	return 0;
}
