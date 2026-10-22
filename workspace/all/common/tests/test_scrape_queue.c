// Host test for scraper/scrape_queue.c: the Artwork Manager queue. Done rows
// leave on their own while the worker keeps only a copy of its item, so a
// finished row moving the others can never send one game's result to another.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../scraper/scrape_queue.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static ScrapeQueueItem make(const char* name) {
	ScrapeQueueItem it;
	memset(&it, 0, sizeof(it));
	snprintf(it.filename, sizeof(it.filename), "%s", name);
	snprintf(it.rom_path, sizeof(it.rom_path), "/Roms/GB/%s", name);
	snprintf(it.out_png, sizeof(it.out_png), "/Roms/GB/.media/%s.png", name);
	it.status = SCRAPE_STATUS_DONE; // add must reset it
	return it;
}

static ScrapeStatus status_of(ScrapeQueue* q, const char* name) {
	char path[600];
	snprintf(path, sizeof(path), "/Roms/GB/%s", name);
	int i = ScrapeQueue_find(q, path);
	return i >= 0 ? q->items[i].status : (ScrapeStatus)-1;
}

int main(void) {
	ScrapeQueue q;
	memset(&q, 0, sizeof(q));
	const char* names[] = {"a.gb", "b.gb", "c.gb", "d.gb"};
	for (int i = 0; i < 4; i++) {
		ScrapeQueueItem it = make(names[i]);
		CHECK(ScrapeQueue_add(&q, &it) == 1, "queued");
	}
	ScrapeQueueItem dup = make("b.gb");
	CHECK(ScrapeQueue_add(&q, &dup) == 0, "a waiting ROM is not queued twice");
	CHECK(status_of(&q, "a.gb") == SCRAPE_STATUS_IDLE, "added items wait");
	CHECK(ScrapeQueue_isBusy(&q), "busy with waiting items");

	// a finishes, b fails, then c is claimed
	ScrapeQueueItem w;
	CHECK(ScrapeQueue_takeNext(&q, &w) && strcmp(w.filename, "a.gb") == 0, "worker claims a");
	CHECK(status_of(&q, "a.gb") == SCRAPE_STATUS_SEARCHING, "claimed item is in progress at once");
	ScrapeQueue_finish(&q, w.rom_path, SCRAPE_STATUS_DONE);
	CHECK(q.count == 3 && status_of(&q, "a.gb") == (ScrapeStatus)-1, "done row removed automatically");
	CHECK(ScrapeQueue_takeNext(&q, &w) && strcmp(w.filename, "b.gb") == 0, "worker claims b");
	ScrapeQueue_finish(&q, w.rom_path, SCRAPE_STATUS_NOT_FOUND);
	CHECK(status_of(&q, "b.gb") == SCRAPE_STATUS_NOT_FOUND, "failed row stays visible");

	CHECK(ScrapeQueue_takeNext(&q, &w) && strcmp(w.filename, "c.gb") == 0, "worker claims c");
	// while c runs unlocked, another row finishes ahead of it and the rows move
	ScrapeQueueItem e = make("e.gb");
	ScrapeQueue_add(&q, &e);
	ScrapeQueue_dropFinished(&q); // drops b, moving c down a slot
	CHECK(ScrapeQueue_setStatus(&q, w.rom_path, SCRAPE_STATUS_DOWNLOADING), "status found by path after the move");
	CHECK(status_of(&q, "c.gb") == SCRAPE_STATUS_DOWNLOADING, "c shows its own progress");
	CHECK(status_of(&q, "d.gb") == SCRAPE_STATUS_IDLE && status_of(&q, "e.gb") == SCRAPE_STATUS_IDLE,
		  "no other row got c's progress");
	CHECK(strcmp(w.out_png, "/Roms/GB/.media/c.gb.png") == 0, "worker's copy still names c's art");
	ScrapeQueue_finish(&q, w.rom_path, SCRAPE_STATUS_DONE);
	CHECK(status_of(&q, "d.gb") == SCRAPE_STATUS_IDLE, "c's result never lands on d");

	// counts: dropFinished reset done; c finished after it
	int done = -1, total = -1, failed = -1;
	ScrapeQueue_stats(&q, &done, &total, &failed);
	CHECK(done == 1 && total == 3 && failed == 0, "done count kept after rows go");

	// drain the rest; busy clears
	while (ScrapeQueue_takeNext(&q, &w))
		ScrapeQueue_finish(&q, w.rom_path, strcmp(w.filename, "e.gb") == 0 ? SCRAPE_STATUS_ERROR : SCRAPE_STATUS_DONE);
	CHECK(!ScrapeQueue_isBusy(&q), "not busy once everything finished");
	ScrapeQueue_stats(&q, &done, &total, &failed);
	CHECK(done == 2 && total == 3 && failed == 1, "2 done of 3, 1 failed");

	// a failed item can be queued again (retry)
	ScrapeQueueItem again = make("e.gb");
	CHECK(ScrapeQueue_add(&q, &again) == 1 && status_of(&q, "e.gb") == SCRAPE_STATUS_IDLE, "failed item retried");
	CHECK(q.count == 1, "retry replaces the failed row");

	// finished paths are handed to the UI once, in order
	char* p1 = ScrapeQueue_popFinished(&q);
	char* p2 = ScrapeQueue_popFinished(&q);
	char* p3 = ScrapeQueue_popFinished(&q);
	char* p4 = ScrapeQueue_popFinished(&q);
	CHECK(p1 && strcmp(p1, "/Roms/GB/a.gb") == 0 && p2 && strcmp(p2, "/Roms/GB/c.gb") == 0 && p3 &&
			  strcmp(p3, "/Roms/GB/d.gb") == 0 && !p4,
		  "finished paths popped once");
	free(p1);
	free(p2);
	free(p3);

	// no fixed cap
	for (int i = 0; i < 5000; i++) {
		char n[32];
		snprintf(n, sizeof(n), "big%d.gb", i);
		ScrapeQueueItem it = make(n);
		if (ScrapeQueue_add(&q, &it) != 1) {
			CHECK(0, "queue grows past 2048");
			break;
		}
	}
	CHECK(q.count == 5001, "5000 more queued");

	ScrapeQueue_free(&q);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
