#ifndef MA_FFSHOW_H
#define MA_FFSHOW_H
// Which frames fast-forward shows; the rest are dropped (and cores that honor
// GET_AUDIO_VIDEO_ENABLE skip drawing them).
//
// Up to 4x, a pass of max_ff_speed + 1 core frames is paced to one display
// period (limitFF), so showing its last frame gives a steady 60 fps with the
// same step every frame. A time gap there dropped every other pass at random
// (each present waits for vblank), PS1 at 2x showed ~25 uneven fps.
//
// Above 4x, or with no cap (passes unpaced), one frame per
// FF_FRAME_INTERVAL_MS: each present waits for vblank (tg5040 can't turn the
// swap interval off), so a present per pass held fceumm (~2 ms a frame) under
// 8x, and a 10 ms gap left it two frames per vblank.
#define FF_FRAME_INTERVAL_MS 30
#define FF_EVERY_PASS_MAX_SPEED 3 // max_ff_speed index: 0 = None, 3 = 4x

static inline int FF_showFrame(int max_ff_speed, int last_of_pass, unsigned since_flip_ms) {
	if (max_ff_speed > 0 && max_ff_speed <= FF_EVERY_PASS_MAX_SPEED)
		return last_of_pass;
	return since_flip_ms >= FF_FRAME_INTERVAL_MS;
}
#endif
