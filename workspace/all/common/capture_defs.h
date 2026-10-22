#ifndef CAPTURE_DEFS_H
#define CAPTURE_DEFS_H

// Paths and checks shared by the screen capture tools (screenshot,
// screenrecorder) and the ffmpeg users (Image Viewer rotate).

#include <string.h>

#ifndef FFMPEG_PATH
#define FFMPEG_PATH "/usr/bin/ffmpeg"
#endif

// The frame mirror generic_video.c writes while a capture tool runs: raw
// bottom-up RGBA frames, and its "WxH" geometry.
#define FB_MIRROR_PATH "/tmp/fb_mirror.raw"
#define FB_MIRROR_INFO_PATH "/tmp/fb_mirror.info"

// On tg5050 the display engine never scans out of fb0 — apps render through
// GL/DRM and fb0 stays black (its only writer is the zeroing in
// PLAT_quitVideo) — so an fbdev grab is guaranteed to be empty there. On
// tg5040 the PowerVR fbdev EGL renders straight into fb0, so the visible pane
// IS the screen.
#ifdef PLATFORM
static inline int fbdev_usable(void) {
	return strcmp(PLATFORM, "tg5050") != 0;
}
#endif

#endif // CAPTURE_DEFS_H
