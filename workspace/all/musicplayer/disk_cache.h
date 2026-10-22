#ifndef DISK_CACHE_H
#define DISK_CACHE_H
// Size cap for the flat file caches on the SD card (album art, lyrics): one
// file per track, written once and otherwise never removed.

// Deletes the oldest (by mtime) regular files in dir until at most max_files
// remain and they total at most max_bytes. Hidden (dot) files are ignored.
// Returns how many files were removed.
int DiskCache_trim(const char* dir, int max_files, long long max_bytes);

// Total bytes of the non-hidden entries in dir (0 when it cannot be read).
long DiskCache_size(const char* dir);

// Removes every non-hidden entry in dir.
void DiskCache_clear(const char* dir);
#endif
