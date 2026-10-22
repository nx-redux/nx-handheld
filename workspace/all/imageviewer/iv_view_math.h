#ifndef IV_VIEW_MATH_H
#define IV_VIEW_MATH_H

// Zoom/pan geometry of the viewer (iv_viewer.c), split out so it can be
// unit-tested on the host: see tests/test_iv_view_math.c.

typedef struct {
	int x, y, w, h;
} IvRect;

// The part of an img_w x img_h image shown on a screen_w x screen_h screen at
// `scale`, centered on (cx, cy) in image coordinates: at most the image's own
// size, and moved back inside the image when the center is near an edge.
static inline IvRect IvView_srcRect(int screen_w, int screen_h, float scale, int img_w, int img_h,
									int cx, int cy) {
	int vis_w = (int)(screen_w / scale), vis_h = (int)(screen_h / scale);
	IvRect src = {cx - vis_w / 2, cy - vis_h / 2,
				  vis_w > img_w ? img_w : vis_w,
				  vis_h > img_h ? img_h : vis_h};
	if (src.x < 0)
		src.x = 0;
	if (src.y < 0)
		src.y = 0;
	if (src.x + src.w > img_w)
		src.x = img_w - src.w;
	if (src.y + src.h > img_h)
		src.y = img_h - src.h;
	return src;
}

#endif
