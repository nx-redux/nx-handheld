#include "scrape_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool ScrapeQueue_isTerminal(ScrapeStatus s) {
	return s == SCRAPE_STATUS_DONE || s == SCRAPE_STATUS_NOT_FOUND || s == SCRAPE_STATUS_ERROR;
}

int ScrapeQueue_find(const ScrapeQueue* q, const char* rom_path) {
	for (int i = 0; i < q->count; i++) {
		if (strcmp(q->items[i].rom_path, rom_path) == 0)
			return i;
	}
	return -1;
}

int ScrapeQueue_add(ScrapeQueue* q, const ScrapeQueueItem* item) {
	int i = ScrapeQueue_find(q, item->rom_path);
	if (i >= 0) {
		if (!ScrapeQueue_isTerminal(q->items[i].status))
			return 0;
		// a failed item: retry it at the back of the queue
		memmove(&q->items[i], &q->items[i + 1], (size_t)(q->count - i - 1) * sizeof(ScrapeQueueItem));
		q->count--;
	}
	if (q->count >= q->cap) {
		int ncap = q->cap ? q->cap * 2 : 64;
		ScrapeQueueItem* grown = realloc(q->items, (size_t)ncap * sizeof(ScrapeQueueItem));
		if (!grown)
			return -1;
		q->items = grown;
		q->cap = ncap;
	}
	q->items[q->count] = *item;
	q->items[q->count].status = SCRAPE_STATUS_IDLE;
	q->count++;
	return 1;
}

bool ScrapeQueue_takeNext(ScrapeQueue* q, ScrapeQueueItem* out) {
	for (int i = 0; i < q->count; i++) {
		if (q->items[i].status == SCRAPE_STATUS_IDLE) {
			q->items[i].status = SCRAPE_STATUS_SEARCHING;
			*out = q->items[i];
			return true;
		}
	}
	return false;
}

bool ScrapeQueue_setStatus(ScrapeQueue* q, const char* rom_path, ScrapeStatus s) {
	int i = ScrapeQueue_find(q, rom_path);
	if (i < 0)
		return false;
	q->items[i].status = s;
	return true;
}

static void recordFinished(ScrapeQueue* q, const char* rom_path) {
	if (q->finished_count >= q->finished_cap) {
		int ncap = q->finished_cap ? q->finished_cap * 2 : 16;
		char** grown = realloc(q->finished, (size_t)ncap * sizeof(char*));
		if (!grown)
			return; // the ROM list just misses one refresh
		q->finished = grown;
		q->finished_cap = ncap;
	}
	char* dup = strdup(rom_path);
	if (dup)
		q->finished[q->finished_count++] = dup;
}

void ScrapeQueue_finish(ScrapeQueue* q, const char* rom_path, ScrapeStatus result) {
	int i = ScrapeQueue_find(q, rom_path);
	if (result != SCRAPE_STATUS_DONE) {
		if (i >= 0)
			q->items[i].status = result;
		return;
	}
	if (i >= 0) {
		memmove(&q->items[i], &q->items[i + 1], (size_t)(q->count - i - 1) * sizeof(ScrapeQueueItem));
		q->count--;
	}
	q->done++;
	recordFinished(q, rom_path);
}

bool ScrapeQueue_isBusy(const ScrapeQueue* q) {
	for (int i = 0; i < q->count; i++) {
		if (!ScrapeQueue_isTerminal(q->items[i].status))
			return true;
	}
	return false;
}

void ScrapeQueue_stats(const ScrapeQueue* q, int* done, int* total, int* failed) {
	int f = 0;
	for (int i = 0; i < q->count; i++) {
		if (q->items[i].status == SCRAPE_STATUS_NOT_FOUND || q->items[i].status == SCRAPE_STATUS_ERROR)
			f++;
	}
	if (done)
		*done = q->done;
	if (total)
		*total = q->done + q->count;
	if (failed)
		*failed = f;
}

char* ScrapeQueue_popFinished(ScrapeQueue* q) {
	if (q->finished_count == 0)
		return NULL;
	char* path = q->finished[0];
	q->finished_count--;
	memmove(&q->finished[0], &q->finished[1], (size_t)q->finished_count * sizeof(char*));
	return path;
}

void ScrapeQueue_dropFinished(ScrapeQueue* q) {
	int w = 0;
	for (int i = 0; i < q->count; i++) {
		if (ScrapeQueue_isTerminal(q->items[i].status))
			continue;
		if (w != i)
			q->items[w] = q->items[i];
		w++;
	}
	q->count = w;
	q->done = 0;
}

void ScrapeQueue_free(ScrapeQueue* q) {
	free(q->items);
	for (int i = 0; i < q->finished_count; i++)
		free(q->finished[i]);
	free(q->finished);
	memset(q, 0, sizeof(*q));
}
