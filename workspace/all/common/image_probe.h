#ifndef IMAGE_PROBE_H
#define IMAGE_PROBE_H
// Image dimensions read from the file header, without decoding any pixels:
// lets a caller refuse (or plan a downscale for) an image before IMG_Load
// allocates the full bitmap. No SDL, so the host tests can cover it.
#include <stdbool.h>
#include <stdio.h>

// PNG, JPEG (SOF0-SOF15 minus C4/C8/CC, found by walking the markers with
// fseek, so any amount of EXIF/ICC/XMP in front of the frame header is fine),
// GIF and BMP. Returns false (and leaves *w/*h untouched) for other formats
// and for corrupt headers.
bool ImageProbe_dimensions(const char* path, int* w, int* h);
bool ImageProbe_dimensionsFile(FILE* f, int* w, int* h);

// Largest image (in pixels, either side) the launcher and tools decode in
// full; bigger ones are skipped. Real box art is far smaller.
#define IMAGE_PROBE_MAX_SIDE 4096

// true if the header says the image is bigger than max_side on either side.
// An unreadable header gives false: the caller decodes as before.
bool ImageProbe_isOversized(const char* path, int max_side);
#endif
