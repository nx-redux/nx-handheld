#include "iv_probe.h"
#include "image_probe.h"

// The header parsing lives in common/image_probe.c (shared with the launcher
// and the scraper); it walks JPEG markers with fseek, so a fat EXIF/ICC block
// in front of the frame header no longer hides the size.
bool IvProbe_dimensions(const char* path, int* w, int* h) {
	return ImageProbe_dimensions(path, w, h);
}
