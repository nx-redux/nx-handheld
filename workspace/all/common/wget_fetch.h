#ifndef WGET_FETCH_H
#define WGET_FETCH_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Fetch URL content into memory buffer using wget.
 * Drop-in replacement for radio_net_fetch() for non-radio callers.
 *
 * @param url         The URL to fetch
 * @param buffer      Buffer to store response body
 * @param buffer_size Size of buffer
 * @return            Bytes read on success, -1 on failure
 */
int wget_fetch(const char* url, uint8_t* buffer, int buffer_size);

/**
 * Fetch URL content into a new malloc'd buffer (NUL-terminated, caller frees).
 * Unlike wget_fetch, a response larger than max_size is rejected rather than
 * cut, and a transfer wget did not finish cleanly is a failure (wget_fetch
 * accepts its partial file), so callers never parse a truncated body as if it
 * were complete.
 *
 * @param url      The URL to fetch
 * @param max_size Largest accepted response in bytes
 * @param out      Receives the buffer (NULL on failure)
 * @return         Bytes read on success, -2 when over max_size, -1 on other failures
 */
int wget_fetch_alloc(const char* url, int max_size, uint8_t** out);

/**
 * Fetch only the response headers of a URL WITHOUT following redirects
 * (wget -S --max-redirect=0). Used to read a redirect's Location target,
 * e.g. GitHub's /releases/latest -> /releases/tag/<TAG>, without touching
 * the rate-limited api.github.com.
 *
 * @param url         The URL to probe
 * @param buffer      Buffer to store the raw header dump (NUL-terminated)
 * @param buffer_size Size of buffer
 * @return            Bytes read on success, -1 on failure
 */
int wget_fetch_headers_noredirect(const char* url, char* buffer, int buffer_size);

/**
 * Download URL to file with progress reporting and cancellation.
 * Drop-in replacement for http_download_file() using wget.
 * Supports resume via -c flag. Partial files are kept on failure for resume.
 *
 * @param url           The URL to download
 * @param filepath      Local path to save the file
 * @param progress_pct  Optional pointer to receive progress (0-100), can be NULL
 * @param should_stop   Optional pointer to cancellation flag, can be NULL
 * @param speed_bps_out Optional pointer to receive current speed in bytes/sec, can be NULL
 * @param eta_sec_out   Optional pointer to receive estimated time remaining in seconds, can be NULL
 * @return              Bytes downloaded on success, -1 on failure
 */
int wget_download_file(const char* url, const char* filepath,
					   volatile int* progress_pct, volatile bool* should_stop,
					   volatile int* speed_bps_out, volatile int* eta_sec_out);

/**
 * A wget_download_file run on a background thread, polled by a UI loop
 * (Settings > Updater, the Cheat Database pak). The thread owns the volatile
 * fields until `done`; the loop reads progress/speed/eta, may set `cancel`,
 * and calls wget_job_join once `done` (or after setting `cancel`).
 */
typedef struct {
	char url[512];
	char filepath[1024];
	volatile int progress; // 0-100; the start value until wget reports
	volatile int speed;	   // bytes/sec
	volatile int eta;	   // seconds remaining
	volatile bool cancel;
	volatile bool done;
	int result; // wget_download_file's return, valid once done
	pthread_t thread;
} WgetDownloadJob;

/**
 * Reset `job` and start downloading url to filepath on a new thread.
 * progress starts at initial_progress (e.g. -1 for "no bar yet").
 * Returns false (job not running) when the thread can't be created.
 */
bool wget_job_start(WgetDownloadJob* job, const char* url, const char* filepath, int initial_progress);

/** Wait for the job's thread (after `done`, or after setting `cancel`). */
void wget_job_join(WgetDownloadJob* job);

/** "1.5 MB/s", "12 KB/s", "512 B/s"; empty for 0 or less. */
static inline void wget_format_speed(int speed_bps, char* out, size_t out_size) {
	if (speed_bps >= 1024 * 1024)
		snprintf(out, out_size, "%.1f MB/s", speed_bps / (1024.0 * 1024.0));
	else if (speed_bps >= 1024)
		snprintf(out, out_size, "%.0f KB/s", speed_bps / 1024.0);
	else if (speed_bps > 0)
		snprintf(out, out_size, "%d B/s", speed_bps);
	else
		out[0] = '\0';
}

/** "1:02:03", "2:05", "42s"; empty for 0 or less. */
static inline void wget_format_eta(int eta_sec, char* out, size_t out_size) {
	if (eta_sec <= 0)
		out[0] = '\0';
	else if (eta_sec >= 3600)
		snprintf(out, out_size, "%d:%02d:%02d", eta_sec / 3600, (eta_sec % 3600) / 60, eta_sec % 60);
	else if (eta_sec >= 60)
		snprintf(out, out_size, "%d:%02d", eta_sec / 60, eta_sec % 60);
	else
		snprintf(out, out_size, "%ds", eta_sec);
}

#endif // WGET_FETCH_H
