/*
 * Save states for PICO-8: its whole memory, copied out and put back. Ported from
 * josegonzalez/minui-pico-8-pak's shim/state.c (MIT licence, shipped as
 * LICENSE-minui-pico-8-pak.txt): the arena, the page format, the big blocks left out
 * and the pointer words kept are its design and mostly its code. What changed here: a
 * state goes to and from a buffer (p8ipc's state[]) instead of a file, the core's
 * header (p8state.h) carries the clock and the checks against pico8_64 and the cart,
 * and ASLR is turned off by the core when it starts pico8_64 instead of by a re-exec.
 *
 * PICO-8's memory is its executable's writable segment and its heap. Every allocation
 * PICO-8 makes itself is served from an arena at a fixed address, so the heap is one
 * block that can be copied and put back where it was. The shim's own allocations, and
 * those of SDL and libc, stay on libc's heap and are never part of a state. pico8_64
 * is position independent, and starts with address randomisation off, so its image
 * lands at the same address each run.
 *
 * A pointer the old process held into SDL or libc means nothing to the new one, so a
 * word of the writable segment that held one keeps the value it has now. The handles
 * PICO-8 opens (window, renderer, texture, audio) are such words, and stay the ones
 * this process opened.
 *
 * States are taken and put back only while the shim holds PICO-8 at a present: those
 * come from its main loop with no Lua running.
 *
 * Vendored, unmodified, in vendor/: dlmalloc 2.8.6 (Doug Lea, MIT-0; malloc.c, sha256
 * be7be56b...508d) and LZ4 1.10.0 (BSD-2; lz4/lz4 at ebb370ca, lib/lz4.c + lz4.h),
 * the same pins the pak uses.
 */

#define _GNU_SOURCE
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <unistd.h>

#include "state.h"

/*
 * dlmalloc runs the arena and keeps its own bookkeeping inside it, so a state holds
 * that too. It is given the whole arena up front and never asks for more. With
 * FOOTERS off nothing in the arena depends on this run's random magic. Its warnings
 * are its own, so they are silenced for the include alone, and with the shim's hidden
 * visibility none of it is exported.
 */
#define ONLY_MSPACES 1
#define MSPACES 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define USE_LOCKS 1
#define NO_MALLOC_STATS 1
#define MALLOC_FAILURE_ACTION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "vendor/dlmalloc.c"
#pragma GCC diagnostic pop

/*
 * LZ4 packs a state to about a fifth of its size in tens of milliseconds on these
 * cores, where zlib took seconds. It is built in hidden, like dlmalloc.
 */
#define LZ4LIB_VISIBILITY
/* dlmalloc's assert is an empty statement, where LZ4 needs one that is an expression */
#undef assert
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wempty-body"
#include "vendor/lz4.c"
#pragma GCC diagnostic pop

#define LOG(...) fprintf(stderr, "pico8shim: " __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

/*
 * Clear of where the kernel puts anything for PICO-8, and under 2^38 on arm64, so it
 * fits the smallest address space these kernels are built with. PICO-8 uses about
 * 25 MB of it.
 */
#define ARENA_ADDRESS ((uintptr_t)0x2000000000ull)
#define ARENA_SIZE ((size_t)256 << 20)

#define PAGE 4096
#define PAGE_UP(x) (((x) + PAGE - 1) & ~(uintptr_t)(PAGE - 1))

extern void* __libc_malloc(size_t size);
extern void* __libc_calloc(size_t count, size_t size);
extern void* __libc_realloc(void* pointer, size_t size);
extern void __libc_free(void* pointer);

static int on;

/* PICO-8's image, where it is loaded this run */
static uintptr_t image_base, image_lo, image_hi, text_lo, text_hi, data_lo, data_hi;
static unsigned char build_id[20];
static size_t build_id_size;

/* the GOT holds this process's library addresses, which a state never replaces */
static uintptr_t got_lo, got_hi;
/* PICO-8's machine, its RAM and registers, which holds no pointers and is put back whole */
static uintptr_t machine_lo, machine_hi;
/* the cart's Lua state, NULL until the cart starts, and the downloads running */
static uintptr_t cart_lua;
static uintptr_t download_threads;
/* presents since the cart started, up to 2: a state waits for its first frame to have run */
static int started_presents;

static char* arena;
static mspace space;
/* set when the arena ran out, after which PICO-8's memory is no longer all in it */
static int arena_full;

/* the address ranges of everything that is neither PICO-8's image nor the arena */
struct range {
	uint64_t lo, hi;
};

/*
 * The big blocks PICO-8 allocates before its cart starts, such as the 8.6 MB it reads
 * from pico8.dat. Each is hashed once the cart is running, and one that still matches
 * when a state is taken is left out of it: every run of the same PICO-8 loads it the
 * same, so the run a state is loaded into holds it too.
 */
#define BIG_BLOCK (1u << 20)
#define BIG_BLOCKS 16

static struct big_block {
	uintptr_t at;
	size_t size;
	uint64_t hash;
} big_blocks[BIG_BLOCKS];
static int big_blocks_hashed;

/* a block a state leaves out, by where it is in the arena and what it hashes to */
struct state_kept {
	uint64_t offset, size, hash;
};

/*
 * The snapshot, after the core's p8state_hdr: this header, the foreign ranges, the
 * kept blocks, a byte per page, then the stored pages packed in chunks. Every field is
 * checked before any of it is put back.
 */
#define SNAP_MAGIC "P8NSNAP1"

struct snap_header {
	char magic[8];
	uint32_t build_id_size;
	unsigned char build_id[20];
	uint64_t data_lo, data_hi;
	uint64_t arena, arena_end;
	uint64_t range_count, kept_count, page_total;
	/* the stored pages, before and after packing */
	uint64_t stored_size, packed_size;
	uint32_t page_size, reserved;
};

/*
 * The pages are packed in chunks of this much, each after its size before and after
 * packing (0 after: kept as they are, where packing would not make them smaller). A
 * page never spans two chunks.
 */
#define CHUNK (256u << 10)

/* what a page of a state is: all zero, stored, or kept from the run it is loaded into */
enum { PAGE_ZERO,
	   PAGE_STORED,
	   PAGE_KEPT };

/* ---- finding PICO-8 ---- */

static void read_build_id(const ElfW(Phdr) * phdr) {
	const unsigned char* at = (const unsigned char*)(image_base + phdr->p_vaddr);
	const unsigned char* end = at + phdr->p_memsz;

	while (at + sizeof(ElfW(Nhdr)) <= end) {
		const ElfW(Nhdr)* note = (const void*)at;
		const unsigned char* desc = at + sizeof *note + ((note->n_namesz + 3) & ~3u);

		if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && memcmp(at + sizeof *note, "GNU", 4) == 0 &&
			note->n_descsz <= sizeof build_id) {
			memcpy(build_id, desc, note->n_descsz);
			build_id_size = note->n_descsz;
			return;
		}
		at = desc + ((note->n_descsz + 3) & ~3u);
	}
}

/* the first object the loader lists is the executable */
static int find_executable(struct dl_phdr_info* info, size_t size, void* data) {
	uintptr_t relro_hi = 0;

	(void)size;
	(void)data;
	image_base = info->dlpi_addr;
	for (int i = 0; i < info->dlpi_phnum; i++) {
		const ElfW(Phdr)* phdr = &info->dlpi_phdr[i];
		uintptr_t lo = image_base + phdr->p_vaddr, hi = lo + phdr->p_memsz;

		if (phdr->p_type == PT_LOAD) {
			if (image_lo == 0 || lo < image_lo)
				image_lo = lo & ~(uintptr_t)(PAGE - 1);
			if (hi > image_hi)
				image_hi = PAGE_UP(hi);
			if (phdr->p_flags & PF_X) {
				text_lo = lo;
				text_hi = hi;
			} else if (phdr->p_flags & PF_W) {
				data_lo = lo;
				data_hi = hi;
			}
		} else if (phdr->p_type == PT_GNU_RELRO) {
			relro_hi = hi;
		} else if (phdr->p_type == PT_NOTE && build_id_size == 0) {
			read_build_id(phdr);
		}
	}

	/* RELRO is made read-only once relocated, so it is never put back */
	if (relro_hi > data_lo && relro_hi < data_hi)
		data_lo = PAGE_UP(relro_hi);
	return 1;
}

static void* read_at(int fd, off_t offset, size_t size) {
	char* buffer = __libc_malloc(size + 1);

	if (buffer == NULL || pread(fd, buffer, size, offset) != (ssize_t)size) {
		__libc_free(buffer);
		return NULL;
	}
	buffer[size] = '\0';
	return buffer;
}

/*
 * PICO-8 keeps its symbols, so its own names find what a state needs in any build. A
 * program without pstate is not PICO-8, and is left alone.
 */
static int read_symbols(void) {
	int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
	ElfW(Ehdr) header;
	ElfW(Shdr)* sections = NULL;
	char* names = NULL;
	int found = 0;

	if (fd < 0 || pread(fd, &header, sizeof header, 0) != (ssize_t)sizeof header ||
		memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_shentsize != sizeof *sections)
		goto done;
	sections = read_at(fd, header.e_shoff, (size_t)header.e_shnum * sizeof *sections);
	if (sections == NULL || header.e_shstrndx >= header.e_shnum)
		goto done;
	names = read_at(fd, sections[header.e_shstrndx].sh_offset, sections[header.e_shstrndx].sh_size);
	if (names == NULL)
		goto done;

	for (int i = 0; i < header.e_shnum; i++) {
		const ElfW(Shdr)* section = &sections[i];
		const char* name = section->sh_name < sections[header.e_shstrndx].sh_size ? names + section->sh_name : "";

		if (strcmp(name, ".got") == 0 || strcmp(name, ".got.plt") == 0) {
			uintptr_t lo = image_base + section->sh_addr, hi = lo + section->sh_size;
			got_lo = got_lo == 0 || lo < got_lo ? lo : got_lo;
			got_hi = hi > got_hi ? hi : got_hi;
		}

		if (section->sh_type == SHT_SYMTAB && section->sh_link < header.e_shnum &&
			section->sh_entsize == sizeof(ElfW(Sym))) {
			const ElfW(Shdr)* strings = &sections[section->sh_link];
			ElfW(Sym)* symbols = read_at(fd, section->sh_offset, section->sh_size);
			char* text = read_at(fd, strings->sh_offset, strings->sh_size);
			size_t count = section->sh_size / sizeof *symbols;

			for (size_t s = 0; symbols != NULL && text != NULL && s < count; s++) {
				const char* symbol = symbols[s].st_name < strings->sh_size ? text + symbols[s].st_name : "";
				uintptr_t at = image_base + symbols[s].st_value;

				if (symbols[s].st_shndx == SHN_UNDEF)
					continue;
				if (strcmp(symbol, "pstate") == 0) {
					machine_lo = at;
					machine_hi = at + symbols[s].st_size;
					found = 1;
				} else if (strcmp(symbol, "L0_cart") == 0 && symbols[s].st_size == sizeof(void*)) {
					cart_lua = at;
				} else if (strcmp(symbol, "num_threads") == 0 && symbols[s].st_size == sizeof(int)) {
					download_threads = at;
				}
			}
			__libc_free(symbols);
			__libc_free(text);
		}
	}

done:
	__libc_free(names);
	__libc_free(sections);
	if (fd >= 0)
		close(fd);
	return found;
}

/*
 * Runs before PICO-8's own code, so nothing PICO-8 allocates is ever left outside the
 * arena. Every other process LD_PRELOAD reaches finds no pstate and keeps libc's malloc.
 */
__attribute__((constructor)) static void state_init(void) {
	const char* states = getenv("P8NATIVE_STATES");

	if (states == NULL || strcmp(states, "1") != 0)
		return;
	dl_iterate_phdr(find_executable, NULL);
	if (text_lo == 0 || data_lo == 0 || !read_symbols())
		return;
	if (!(personality(0xffffffff) & ADDR_NO_RANDOMIZE))
		LOG("address randomisation is on: a state will only load in this run of PICO-8\n");

	/* a hint, as the oldest of these kernels has no MAP_FIXED_NOREPLACE */
	void* placed = mmap((void*)ARENA_ADDRESS, ARENA_SIZE, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (placed != (void*)ARENA_ADDRESS) {
		if (placed != MAP_FAILED)
			munmap(placed, ARENA_SIZE);
		LOG("cannot place the save state arena at %p, save states are off\n", (void*)ARENA_ADDRESS);
		return;
	}
	space = create_mspace_with_base(placed, ARENA_SIZE, 1);
	if (space == NULL) {
		munmap(placed, ARENA_SIZE);
		LOG("cannot start the save state arena, save states are off\n");
		return;
	}
	arena = placed;
	on = 1;
	LOG("save states on: PICO-8's image at %p, its arena at %p\n", (void*)image_lo, (void*)arena);
}

/* ---- PICO-8's allocations ---- */

static int in_arena(const void* pointer) {
	return arena != NULL && (uintptr_t)pointer >= (uintptr_t)arena && (uintptr_t)pointer < (uintptr_t)arena + ARENA_SIZE;
}

static int for_arena(const void* caller) {
	return on && !arena_full && (uintptr_t)caller >= text_lo && (uintptr_t)caller < text_hi;
}

/* PICO-8 carries on without states rather than without memory */
static void arena_ran_out(size_t size) {
	if (!arena_full)
		LOG("the save state arena is full at %zu more bytes, save states are off\n", size);
	arena_full = 1;
}

static void note_big_block(void* at, size_t size) {
	if (at == NULL || size < BIG_BLOCK || big_blocks_hashed)
		return;
	for (int i = 0; i < BIG_BLOCKS; i++) {
		if (big_blocks[i].at == 0) {
			big_blocks[i].at = (uintptr_t)at;
			big_blocks[i].size = size;
			return;
		}
	}
}

static void forget_big_block(const void* at) {
	for (int i = 0; i < BIG_BLOCKS; i++)
		if (big_blocks[i].at == (uintptr_t)at)
			big_blocks[i].at = 0;
}

static void* allocate(const void* caller, size_t count, size_t size, int clear) {
	if (for_arena(caller)) {
		size_t total = clear ? count * size : size;
		void* got = clear ? mspace_calloc(space, count, size) : mspace_malloc(space, size);
		if (got != NULL || total == 0) {
			note_big_block(got, total);
			return got;
		}
		arena_ran_out(total);
	}
	return clear ? __libc_calloc(count, size) : __libc_malloc(size);
}

EXPORT void* malloc(size_t size) {
	return allocate(__builtin_return_address(0), 1, size, 0);
}

EXPORT void* calloc(size_t count, size_t size) {
	return allocate(__builtin_return_address(0), count, size, 1);
}

EXPORT void* realloc(void* pointer, size_t size) {
	if (pointer == NULL)
		return allocate(__builtin_return_address(0), 1, size, 0);
	if (!in_arena(pointer))
		return __libc_realloc(pointer, size);

	forget_big_block(pointer);
	void* got = mspace_realloc(space, pointer, size);
	if (got != NULL || size == 0)
		return got;
	arena_ran_out(size);
	got = __libc_malloc(size);
	if (got != NULL) {
		size_t had = mspace_usable_size(pointer);
		memcpy(got, pointer, had < size ? had : size);
		mspace_free(space, pointer);
	}
	return got;
}

EXPORT void free(void* pointer) {
	if (in_arena(pointer)) {
		forget_big_block(pointer);
		mspace_free(space, pointer);
		return;
	}
	__libc_free(pointer);
}

/* ---- the state ---- */

int state_available(void) {
	return on && !arena_full;
}

static int cart_started(void) {
	return cart_lua != 0 && *(void* const*)cart_lua != NULL;
}

static int busy(void) {
	return download_threads != 0 && *(const int*)download_threads > 0;
}

int state_ready(void) {
	return state_available() && started_presents >= 2 && cart_started() && !busy();
}

static const char* not_ready(void) {
	if (!state_available())
		return "save states are off in this run of PICO-8";
	if (busy())
		return "PICO-8 is downloading";
	return "the cart has not started yet";
}

/* FNV-1a over whole words, which on these cores hashes 8.6 MB in a few milliseconds */
static uint64_t hash_bytes(const unsigned char* at, size_t size) {
	uint64_t hash = 0xcbf29ce484222325ull, word;
	size_t i;

	for (i = 0; i + sizeof word <= size; i += sizeof word) {
		memcpy(&word, at + i, sizeof word);
		hash = (hash ^ word) * 0x100000001b3ull;
	}
	for (; i < size; i++)
		hash = (hash ^ at[i]) * 0x100000001b3ull;
	return hash;
}

void state_frame(void) {
	if (!state_available() || !cart_started())
		return;
	if (started_presents < 2)
		started_presents++;
	if (big_blocks_hashed)
		return;
	size_t total = 0;
	for (int i = 0; i < BIG_BLOCKS; i++) {
		if (big_blocks[i].at != 0) {
			big_blocks[i].hash = hash_bytes((const unsigned char*)big_blocks[i].at, big_blocks[i].size);
			total += big_blocks[i].size;
		}
	}
	big_blocks_hashed = 1;
	LOG("%zu KB of PICO-8's own data can be left out of its states\n", total >> 10);
}

/* everything mapped that is neither PICO-8's image nor the arena, from /proc/self/maps */
static struct range* read_foreign(uint64_t* count) {
	FILE* maps = fopen("/proc/self/maps", "r");
	struct range *ranges = NULL, *grown;
	size_t capacity = 0;
	char line[512];

	*count = 0;
	if (maps == NULL)
		return NULL;
	while (fgets(line, sizeof line, maps) != NULL) {
		unsigned long lo, hi;

		if (sscanf(line, "%lx-%lx", &lo, &hi) != 2)
			continue;
		/* the brk heap can be merged with the end of the image's bss */
		if (lo < image_hi && hi > image_lo) {
			if (hi <= image_hi)
				continue;
			lo = image_hi;
		}
		if (in_arena((void*)lo))
			continue;
		if (*count == capacity) {
			capacity = capacity ? capacity * 2 : 256;
			grown = __libc_realloc(ranges, capacity * sizeof *ranges);
			if (grown == NULL) {
				__libc_free(ranges);
				fclose(maps);
				*count = 0;
				return NULL;
			}
			ranges = grown;
		}
		ranges[*count].lo = lo;
		ranges[*count].hi = hi;
		(*count)++;
	}
	fclose(maps);
	return ranges;
}

/* /proc/self/maps lists the ranges in address order, so they are searched by halves */
static int is_foreign(uint64_t value, const struct range* ranges, uint64_t count) {
	uint64_t lo = 0, hi = count;

	while (lo < hi) {
		uint64_t mid = lo + (hi - lo) / 2;
		if (value < ranges[mid].lo)
			hi = mid;
		else if (value >= ranges[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

/* the part of the arena dlmalloc has handed out, up to and including the header of its top chunk */
static uintptr_t arena_end(void) {
	return PAGE_UP((uintptr_t)((mstate)space)->top + TOP_FOOT_SIZE);
}

static int is_zero_page(const unsigned char* page, size_t size) {
	static const unsigned char zero[PAGE];
	return memcmp(page, zero, size) == 0;
}

/* the writable segment and then the arena, in pages */
struct region {
	unsigned char* at;
	size_t size;
};

static size_t page_size_at(size_t region_size, size_t page) {
	size_t left = region_size - page * PAGE;
	return left < PAGE ? left : PAGE;
}

static int page_is_kept(const struct state_kept* kept, uint64_t kept_count, uintptr_t page) {
	for (uint64_t i = 0; i < kept_count; i++) {
		uintptr_t lo = (uintptr_t)arena + kept[i].offset, hi = lo + kept[i].size;
		if (page >= lo && page + PAGE <= hi)
			return 1;
	}
	return 0;
}

/* the big blocks still as they were when the cart started */
static uint64_t find_kept(struct state_kept* kept) {
	uint64_t count = 0;

	if (!big_blocks_hashed)
		return 0;
	for (int i = 0; i < BIG_BLOCKS; i++) {
		const struct big_block* block = &big_blocks[i];

		if (block->at == 0 || hash_bytes((const unsigned char*)block->at, block->size) != block->hash)
			continue;
		kept[count].offset = block->at - (uintptr_t)arena;
		kept[count].size = block->size;
		kept[count].hash = block->hash;
		count++;
	}
	return count;
}

/* packs raw (size bytes) as the next chunk at out + *packed, out holding cap bytes */
static int pack_chunk(uint8_t* out, size_t cap, uint64_t* packed, uint64_t* stored, const unsigned char* raw,
					  size_t size) {
	uint32_t sizes[2];

	if (*packed + sizeof sizes > cap)
		return 0;
	uint8_t* at = out + *packed + sizeof sizes;
	size_t room = cap - *packed - sizeof sizes;
	int bound = LZ4_compressBound((int)size);
	int got = LZ4_compress_default((const char*)raw, (char*)at, (int)size, room < (size_t)bound ? (int)room : bound);
	sizes[0] = (uint32_t)size;
	sizes[1] = got > 0 && (size_t)got < size ? (uint32_t)got : 0;
	if (sizes[1] == 0) {
		if (size > room)
			return 0;
		memcpy(at, raw, size);
	}
	memcpy(out + *packed, sizes, sizeof sizes);
	*packed += sizeof sizes + (sizes[1] != 0 ? sizes[1] : sizes[0]);
	*stored += size;
	return 1;
}

size_t state_capture(uint8_t* out, size_t cap, const char** why) {
	struct snap_header header;
	struct state_kept kept[BIG_BLOCKS];
	struct region regions[2];
	struct range* ranges;
	unsigned char* chunk;
	size_t at = 0, filled = 0;
	int ok;

	if (!state_ready()) {
		*why = not_ready();
		return 0;
	}
	memset(&header, 0, sizeof header);
	memcpy(header.magic, SNAP_MAGIC, sizeof header.magic);
	header.build_id_size = (uint32_t)build_id_size;
	memcpy(header.build_id, build_id, build_id_size);
	header.data_lo = data_lo;
	header.data_hi = data_hi;
	header.arena = (uintptr_t)arena;
	header.arena_end = arena_end();
	header.page_size = PAGE;
	header.kept_count = find_kept(kept);
	ranges = read_foreign(&header.range_count);
	if (ranges == NULL) {
		*why = "cannot read /proc/self/maps";
		return 0;
	}

	regions[0].at = (unsigned char*)data_lo;
	regions[0].size = data_hi - data_lo;
	regions[1].at = (unsigned char*)arena;
	regions[1].size = header.arena_end - (uintptr_t)arena;
	for (int r = 0; r < 2; r++)
		header.page_total += (regions[r].size + PAGE - 1) / PAGE;

	size_t o_ranges = sizeof header;
	size_t o_kept = o_ranges + header.range_count * sizeof(struct range);
	size_t o_flags = o_kept + header.kept_count * sizeof(struct state_kept);
	size_t o_packed = o_flags + header.page_total;
	if (o_packed > cap) {
		__libc_free(ranges);
		*why = "the state is too large";
		return 0;
	}
	memcpy(out + o_ranges, ranges, header.range_count * sizeof(struct range));
	memcpy(out + o_kept, kept, header.kept_count * sizeof(struct state_kept));
	__libc_free(ranges);

	unsigned char* flags = out + o_flags;
	for (int r = 0; r < 2; r++) {
		for (size_t page = 0; page * PAGE < regions[r].size; page++) {
			unsigned char* from = regions[r].at + page * PAGE;
			size_t size = page_size_at(regions[r].size, page);

			if (r == 1 && size == PAGE && page_is_kept(kept, header.kept_count, (uintptr_t)from))
				flags[at++] = PAGE_KEPT;
			else if (is_zero_page(from, size))
				flags[at++] = PAGE_ZERO;
			else
				flags[at++] = PAGE_STORED;
		}
	}

	/* only the stored pages are copied, packed as they go */
	chunk = __libc_malloc(CHUNK);
	ok = chunk != NULL;
	*why = ok ? "the state is too large" : "out of memory";
	at = 0;
	for (int r = 0; ok && r < 2; r++) {
		for (size_t page = 0; ok && page * PAGE < regions[r].size; page++) {
			size_t size = page_size_at(regions[r].size, page);

			if (flags[at++] != PAGE_STORED)
				continue;
			if (filled + size > CHUNK) {
				ok = pack_chunk(out + o_packed, cap - o_packed, &header.packed_size, &header.stored_size, chunk, filled);
				filled = 0;
			}
			memcpy(chunk + filled, regions[r].at + page * PAGE, size);
			filled += size;
		}
	}
	if (ok && filled > 0)
		ok = pack_chunk(out + o_packed, cap - o_packed, &header.packed_size, &header.stored_size, chunk, filled);
	__libc_free(chunk);
	if (!ok)
		return 0;
	memcpy(out, &header, sizeof header);
	LOG("took a state: %lu KB stored, packed to %lu KB\n", (unsigned long)(header.stored_size >> 10),
		(unsigned long)(header.packed_size >> 10));
	return o_packed + header.packed_size;
}

/* everything in the header has to match this run of PICO-8 before anything is put back */
static const char* header_mismatch(const struct snap_header* header, size_t len) {
	if (memcmp(header->magic, SNAP_MAGIC, sizeof header->magic) != 0 || header->page_size != PAGE)
		return "not a state this shim can load";
	if (header->build_id_size != build_id_size || memcmp(header->build_id, build_id, build_id_size) != 0)
		return "saved by another build of PICO-8";
	if (header->data_lo != data_lo || header->data_hi != data_hi || header->arena != (uintptr_t)arena)
		return "saved with PICO-8 at another address";
	if (header->arena_end <= header->arena || header->arena_end > header->arena + ARENA_SIZE ||
		(header->arena_end - header->arena) % PAGE != 0 || header->range_count > 1u << 20 ||
		header->kept_count > BIG_BLOCKS)
		return "damaged";
	uint64_t pages = (data_hi - data_lo + PAGE - 1) / PAGE + (header->arena_end - header->arena) / PAGE;
	uint64_t need = sizeof *header + header->range_count * sizeof(struct range) +
					header->kept_count * sizeof(struct state_kept) + header->page_total;
	if (header->page_total != pages || header->packed_size > len || need > len - header->packed_size)
		return "damaged";
	return NULL;
}

/*
 * What a state left out has to be in this run of PICO-8 as it was in the one that saved
 * it, though maybe somewhere else in the arena, as PICO-8 reads its config before
 * pico8.dat and a different config moves everything after it. Finds where each block is now.
 */
static const char* find_kept_sources(const struct state_kept* kept, uint64_t count, const unsigned char** sources) {
	for (uint64_t i = 0; i < count; i++) {
		sources[i] = NULL;
		if (kept[i].size > ARENA_SIZE || kept[i].offset > ARENA_SIZE - kept[i].size)
			return "damaged";
		for (int b = 0; b < BIG_BLOCKS && sources[i] == NULL && big_blocks_hashed; b++) {
			if (big_blocks[b].at != 0 && big_blocks[b].size == kept[i].size && big_blocks[b].hash == kept[i].hash &&
				hash_bytes((const unsigned char*)big_blocks[b].at, big_blocks[b].size) == kept[i].hash)
				sources[i] = (const unsigned char*)big_blocks[b].at;
		}
		if (sources[i] == NULL)
			return "it left out PICO-8 data this run does not hold";
	}
	return NULL;
}

/* where the page at offset of the arena comes from in this run, for a page a state left out */
static const unsigned char* kept_source(const struct state_kept* kept, const unsigned char* const* sources,
										uint64_t count, size_t offset) {
	for (uint64_t i = 0; i < count; i++)
		if (offset >= kept[i].offset && offset + PAGE <= kept[i].offset + kept[i].size)
			return sources[i] + (offset - kept[i].offset);
	return NULL;
}

/* the stored pages, unpacked a chunk at a time */
struct unpacker {
	const uint8_t* in;
	size_t left;
	unsigned char* chunk;
	size_t have, used;
};

static const unsigned char* unpack_next(struct unpacker* u, size_t size) {
	if (u->used == u->have) {
		uint32_t sizes[2];
		if (u->left < sizeof sizes)
			return NULL;
		memcpy(sizes, u->in, sizeof sizes);
		u->in += sizeof sizes;
		u->left -= sizeof sizes;
		size_t packed = sizes[1] != 0 ? sizes[1] : sizes[0];
		if (sizes[0] == 0 || sizes[0] > CHUNK || packed > u->left)
			return NULL;
		if (sizes[1] == 0)
			memcpy(u->chunk, u->in, sizes[0]);
		else if (LZ4_decompress_safe((const char*)u->in, (char*)u->chunk, (int)sizes[1], (int)sizes[0]) != (int)sizes[0])
			return NULL;
		u->in += packed;
		u->left -= packed;
		u->have = sizes[0];
		u->used = 0;
	}
	if (size > u->have - u->used)
		return NULL;
	const unsigned char* got = u->chunk + u->used;
	u->used += size;
	return got;
}

static int read_pages(const unsigned char* flags, const uint8_t* packed, size_t packed_size, unsigned char** into,
					  const size_t* sizes, const struct state_kept* kept, const unsigned char* const* sources,
					  uint64_t kept_count) {
	struct unpacker u = {packed, packed_size, __libc_malloc(CHUNK), 0, 0};
	size_t at = 0;
	int ok = u.chunk != NULL;

	for (int r = 0; ok && r < 2; r++) {
		for (size_t page = 0; ok && page * PAGE < sizes[r]; page++) {
			size_t size = page_size_at(sizes[r], page);
			unsigned char* to = into[r] + page * PAGE;
			const unsigned char* from;

			switch (flags[at++]) {
			case PAGE_STORED:
				from = unpack_next(&u, size);
				ok = from != NULL;
				if (ok)
					memcpy(to, from, size);
				break;
			case PAGE_KEPT:
				from = r == 1 && size == PAGE ? kept_source(kept, sources, kept_count, page * PAGE) : NULL;
				ok = from != NULL;
				if (ok)
					memcpy(to, from, size);
				break;
			case PAGE_ZERO:
				memset(to, 0, size);
				break;
			default:
				ok = 0;
				break;
			}
		}
	}
	__libc_free(u.chunk);
	return ok && u.left == 0 && u.used == u.have;
}

/*
 * A word that pointed outside PICO-8 when the state was saved keeps the value it has
 * now, unless what is there now is not such a pointer, which makes the saved one only a
 * number that happened to look like one.
 */
static void put_back_data(const unsigned char* saved, const struct range* old_ranges, uint64_t old_count,
						  const struct range* ranges, uint64_t count) {
	size_t offset, size = data_hi - data_lo, kept = 0;

	for (offset = 0; offset + sizeof(uintptr_t) <= size; offset += sizeof(uintptr_t)) {
		uintptr_t at = data_lo + offset, was, now;

		memcpy(&was, saved + offset, sizeof was);
		memcpy(&now, (const void*)at, sizeof now);
		if (was == now || (at >= got_lo && at < got_hi))
			continue;
		if ((at < machine_lo || at >= machine_hi) && is_foreign(was, old_ranges, old_count) &&
			(now == 0 || is_foreign(now, ranges, count))) {
			kept++;
			continue;
		}
		memcpy((void*)at, &was, sizeof was);
	}
	/* the bytes after the last whole word */
	memcpy((void*)(data_lo + offset), saved + offset, size - offset);
	LOG("kept %zu of PICO-8's pointers to this run's libraries\n", kept);
}

int state_load(const uint8_t* buf, size_t len, const char** why) {
	struct snap_header header;
	struct state_kept kept[BIG_BLOCKS];
	const unsigned char* sources[BIG_BLOCKS];
	struct range *old_ranges = NULL, *ranges = NULL;
	unsigned char* staged[2] = {NULL, NULL};
	size_t sizes[2];
	uint64_t count = 0;

	if (!state_ready()) {
		*why = not_ready();
		return 0;
	}
	if (len < sizeof header) {
		*why = "damaged";
		return 0;
	}
	memcpy(&header, buf, sizeof header);
	if ((*why = header_mismatch(&header, len)) != NULL)
		return 0;

	size_t o_ranges = sizeof header;
	size_t o_kept = o_ranges + header.range_count * sizeof(struct range);
	size_t o_flags = o_kept + header.kept_count * sizeof(struct state_kept);
	size_t o_packed = o_flags + header.page_total;
	memcpy(kept, buf + o_kept, header.kept_count * sizeof(struct state_kept));
	if ((*why = find_kept_sources(kept, header.kept_count, sources)) != NULL)
		return 0;

	sizes[0] = data_hi - data_lo;
	sizes[1] = header.arena_end - header.arena;
	old_ranges = __libc_malloc((header.range_count + 1) * sizeof *old_ranges);
	staged[0] = __libc_malloc(sizes[0]);
	staged[1] = __libc_malloc(sizes[1]);
	int ok = old_ranges != NULL && staged[0] != NULL && staged[1] != NULL;
	if (!ok)
		*why = "out of memory";
	if (ok) {
		memcpy(old_ranges, buf + o_ranges, header.range_count * sizeof *old_ranges);
		ok = read_pages(buf + o_flags, buf + o_packed, header.packed_size, staged, sizes, kept, sources,
						header.kept_count);
		if (!ok)
			*why = "damaged";
	}
	if (ok) {
		ranges = read_foreign(&count);
		ok = ranges != NULL;
		if (!ok)
			*why = "cannot read /proc/self/maps";
	}
	if (ok) {
		uintptr_t end_now = arena_end();
		put_back_data(staged[0], old_ranges, header.range_count, ranges, count);
		memcpy(arena, staged[1], sizes[1]);
		/* what PICO-8 had used past the state's end is free now, and zero as it was then */
		if (end_now > header.arena_end)
			memset((void*)(uintptr_t)header.arena_end, 0, end_now - header.arena_end);
		/* the big blocks are now where the state had them */
		memset(big_blocks, 0, sizeof big_blocks);
		for (uint64_t i = 0; i < header.kept_count; i++) {
			big_blocks[i].at = (uintptr_t)arena + kept[i].offset;
			big_blocks[i].size = kept[i].size;
			big_blocks[i].hash = kept[i].hash;
		}
		LOG("put back a state: %lu KB stored\n", (unsigned long)(header.stored_size >> 10));
	}

	__libc_free(old_ranges);
	__libc_free(ranges);
	__libc_free(staged[0]);
	__libc_free(staged[1]);
	return ok;
}
