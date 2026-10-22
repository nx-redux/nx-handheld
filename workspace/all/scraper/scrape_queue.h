#ifndef SCRAPE_QUEUE_H
#define SCRAPE_QUEUE_H
// The Artwork Manager's scrape queue, kept free of SDL and threads so the
// host test (common/tests/test_scrape_queue.c) covers it. Not locked: the
// caller (scraper.c) holds its queue mutex around every call.
//
// The worker never keeps a pointer into the queue: ScrapeQueue_takeNext hands
// it a copy, and it reports back by rom_path. Finished (Done) items leave the
// list on their own, which moves the rows behind them; a worker holding a slot
// pointer would then write into another game's row. Failed items stay listed
// so they can be seen, and queueing one again retries it.
#include <stdbool.h>

typedef enum {
	SCRAPE_STATUS_IDLE,
	SCRAPE_STATUS_SEARCHING,
	SCRAPE_STATUS_DOWNLOADING,
	SCRAPE_STATUS_SAVING,
	SCRAPE_STATUS_COMPOSITING,
	SCRAPE_STATUS_DONE,
	SCRAPE_STATUS_NOT_FOUND,
	SCRAPE_STATUS_ERROR,
} ScrapeStatus;

typedef struct {
	char filename[256];
	char rom_path[512];
	char system_path[512];
	char system_name[128];
	char out_png[512];
	int system_id;
	ScrapeStatus status;
} ScrapeQueueItem;

typedef struct {
	ScrapeQueueItem* items; // queued, in progress and failed items, in queue order
	int count;
	int cap;
	int done; // items scraped this session; their rows are gone, the count stays for Progress
	// rom paths scraped since the UI last asked (ScrapeQueue_popFinished), so
	// the ROM list can refresh those rows
	char** finished;
	int finished_count;
	int finished_cap;
} ScrapeQueue;

// true for Done, Not Found and Error
bool ScrapeQueue_isTerminal(ScrapeStatus s);

// Index of rom_path's item, or -1.
int ScrapeQueue_find(const ScrapeQueue* q, const char* rom_path);

// Queues a copy of item (its status is set to IDLE). A failed item with the
// same rom_path is queued again. Returns 1 when queued, 0 when the ROM is
// already waiting or in progress, -1 when out of memory.
int ScrapeQueue_add(ScrapeQueue* q, const ScrapeQueueItem* item);

// Claims the first IDLE item: marks it SEARCHING (so it never looks idle or
// finished while the worker runs) and copies it to out. Returns false when
// nothing is waiting.
bool ScrapeQueue_takeNext(ScrapeQueue* q, ScrapeQueueItem* out);

// Progress of an in-flight item, by rom_path. Returns false if it is gone.
bool ScrapeQueue_setStatus(ScrapeQueue* q, const char* rom_path, ScrapeStatus s);

// The worker's result for rom_path: Done removes the row, counts it and
// records the path for ScrapeQueue_popFinished; Not Found / Error stay listed.
void ScrapeQueue_finish(ScrapeQueue* q, const char* rom_path, ScrapeStatus result);

// True while any item is waiting or in progress.
bool ScrapeQueue_isBusy(const ScrapeQueue* q);

// done = scraped this session, total = done + listed items, failed = listed
// failures. Any pointer may be NULL.
void ScrapeQueue_stats(const ScrapeQueue* q, int* done, int* total, int* failed);

// Next rom path scraped since the last call (caller frees), or NULL.
char* ScrapeQueue_popFinished(ScrapeQueue* q);

// Forgets finished work: failed rows and the done count. Waiting and
// in-progress items stay.
void ScrapeQueue_dropFinished(ScrapeQueue* q);

void ScrapeQueue_free(ScrapeQueue* q);
#endif
