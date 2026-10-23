#include "ma_internal.h"
#include "utils.h"
#include "config.h"
#include "ma_saves.h"
#include "ma_save_paths.h"
#include "ma_state_read.h"
#include "notification.h"
#include "ra_integration.h"
#include <SDL2/SDL_image.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "ma_rewind.h"
#include "ma_emutime.h"
#include "streams/rzip_stream.h"
#include "streams/file_stream.h"

// Saves, RTC data and states are staged to a sibling .tmp, fsynced and renamed
// over the old file, so a full card, a power cut or a kill mid-write keeps the
// previous save instead of leaving it empty or cut short. Returns 1 on success.
static int writeSaveFile(const char* path, const void* data, size_t size, int compressed) {
	if (!data)
		return 0;
	if (compressed) {
		char tmp[MAX_PATH + 16];
		snprintf(tmp, sizeof(tmp), "%s.tmp", path);
		int ok = rzipstream_write_file(tmp, data, size);
		if (ok) {
			// rzipstream closes without syncing; flush the data before the
			// rename makes it the live file
			int fd = open(tmp, O_RDONLY);
			ok = fd >= 0 && fsync(fd) == 0;
			if (fd >= 0)
				close(fd);
		}
		if (!ok || rename(tmp, path) != 0) {
			remove(tmp);
			return 0;
		}
		return 1;
	}
	return writeFileAtomic(path, (const char*)data, size);
}

// filename must be a MAX_PATH-sized buffer
static void formatSavePath(char* work_name, char* filename, const char* suffix) {
	SavePaths_stripExtension(work_name);
	snprintf(filename, MAX_PATH, "%s/%s%s", core.saves_dir, work_name, suffix);
}

static void SRAM_getPath(char* filename) {
	char work_name[MAX_PATH];

	if (CFG_getSaveFormat() == SAVE_FORMAT_SRM || CFG_getSaveFormat() == SAVE_FORMAT_SRM_UNCOMPRESSED) {
		strcpy(work_name, game.alt_name);
		formatSavePath(work_name, filename, ".srm");
	} else if (CFG_getSaveFormat() == SAVE_FORMAT_GEN) {
		strcpy(work_name, game.alt_name);
		formatSavePath(work_name, filename, ".sav");
	} else {
		snprintf(filename, MAX_PATH, "%s/%s.sav", core.saves_dir, game.alt_name);
	}
}

void SRAM_read(void) {
	size_t sram_size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (!sram_size)
		return;

	char filename[MAX_PATH];
	SRAM_getPath(filename);
	printf("sav path (read): %s\n", filename);

	void* sram = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);

	// Always read through rzipstream, regardless of the save-format setting: it
	// detects the #RZIPv# magic and falls back to a raw read when absent, so it
	// handles both compressed and uncompressed .srm files. Choosing the reader
	// from the *setting* corrupted compressed saves read under an uncompressed
	// setting (both formats share the same .srm filename) — and SRAM_write()
	// then destroyed the original by rewriting it raw. The write side stays
	// setting-driven on purpose: the first save after a format switch converts
	// the file losslessly.
	rzipstream_t* sram_file = rzipstream_open(filename, RETRO_VFS_FILE_ACCESS_READ);
	if (!sram_file)
		return;

	if (!sram || rzipstream_read(sram_file, sram, sram_size) < 0)
		LOG_error("rzipstream: Error reading SRAM data\n");

	rzipstream_close(sram_file);
}

void SRAM_write(void) {
	size_t sram_size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (!sram_size)
		return;

	char filename[MAX_PATH];
	SRAM_getPath(filename);
	printf("sav path (write): %s\n", filename);

	void* sram = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);

	// srm is compressed; the other formats are raw
	if (!writeSaveFile(filename, sram, sram_size, CFG_getSaveFormat() == SAVE_FORMAT_SRM))
		LOG_error("Error writing SRAM data to file: %s (%s)\n", filename, strerror(errno));
	sync();
}

///////////////////////////////////////

static void RTC_getPath(char* filename) {
	snprintf(filename, MAX_PATH, "%s/%s.rtc", core.saves_dir, game.alt_name);
}
void RTC_read(void) {
	size_t rtc_size = core.get_memory_size(RETRO_MEMORY_RTC);
	if (!rtc_size)
		return;

	char filename[MAX_PATH];
	RTC_getPath(filename);
	printf("rtc path (read): %s\n", filename);

	FILE* rtc_file = fopen(filename, "r");
	if (!rtc_file)
		return;

	void* rtc = core.get_memory_data(RETRO_MEMORY_RTC);

	if (!rtc || !fread(rtc, 1, rtc_size, rtc_file)) {
		LOG_error("Error reading RTC data\n");
	}

	fclose(rtc_file);
}
void RTC_write(void) {
	size_t rtc_size = core.get_memory_size(RETRO_MEMORY_RTC);
	if (!rtc_size)
		return;

	char filename[MAX_PATH];
	RTC_getPath(filename);
	printf("rtc path (write) size(%zu): %s\n", rtc_size, filename);

	void* rtc = core.get_memory_data(RETRO_MEMORY_RTC);
	if (!writeSaveFile(filename, rtc, rtc_size, 0))
		LOG_error("Error writing RTC data to file: %s (%s)\n", filename, strerror(errno));

	sync();
}

///////////////////////////////////////

int state_slot = 0;

// filename must be a MAX_PATH-sized buffer; returns -1 if the path was
// truncated (it may then name a different slot)
int State_getSlotPath(int slot, char* filename) {
	return SavePaths_state(filename, MAX_PATH, core.states_dir, game.alt_name, CFG_getStateFormat(), slot);
}
// filename must be a MAX_PATH-sized buffer
void State_getPath(char* filename) {
	State_getSlotPath(state_slot, filename);
}

static int64_t State_readRz(void* stream, void* buf, size_t len) {
	return rzipstream_read((rzipstream_t*)stream, buf, len);
}

// The whole state first; a core that wants exactly its serialize_size (and refuses a longer
// state before touching anything) then gets the state cut to it, as before.
static bool State_unserialize(void* state, size_t data_size, size_t state_size) {
	if (core.unserialize(state, data_size))
		return true;
	return data_size > state_size && core.unserialize(state, state_size);
}

int State_read(void) { // from picoarch
	// Block load states in RetroAchievements hardcore mode
	if (RA_isHardcoreModeActive()) {
		Notification_push(NOTIFICATION_ACHIEVEMENT, "Load states disabled in Hardcore mode", NULL);
		return 0;
	}

	int success = 0;
	size_t state_size = core.serialize_size();
	if (!state_size)
		return 0;

	int was_ff = fast_forward;
	fast_forward = 0;

	char filename[MAX_PATH];
	State_getPath(filename);

	uint8_t rastate_header[RASTATE_HEADER_SIZE] = {0};

	// declared (and initialized) before the first goto so the error paths
	// never see an indeterminate handle
	rzipstream_t* state_rzfile = NULL;

	void* state = NULL;
	size_t data_size = 0;

	state_rzfile = rzipstream_open(filename, RETRO_VFS_FILE_ACCESS_READ);
	if (!state_rzfile) {
		if (state_slot != RESUME_SLOT_DEFAULT) { // default state in MiniUI, may not exist
			LOG_error("Error opening state file: %s (%s)\n", filename, strerror(errno));
		}
		goto error;
	}
	if (rzipstream_read(state_rzfile, rastate_header, RASTATE_HEADER_SIZE) < RASTATE_HEADER_SIZE) {
		LOG_error("Error reading rastate header from file: %s (%s)\n", filename, strerror(errno));
		goto error;
	}

	if (memcmp(rastate_header, "RASTATE", 7) != 0) {
		// This file only contains raw core state data
		rzipstream_rewind(state_rzfile);
	}
	// No need to parse the header any further
	// (we only need MEM section which will always be the first one)

	// some cores report the wrong serialize size initially for some games, eg. mgba: Wario Land 4,
	// so a shorter state is padded to state_size, and a longer one is read whole
	state = State_readData(State_readRz, state_rzfile, state_size, State_memSize(rastate_header), &data_size);
	if (!state) {
		LOG_error("Error reading state data from file: %s (%s)\n", filename, strerror(errno));
		goto error;
	}

	if (!State_unserialize(state, data_size, state_size)) {
		LOG_error("Error restoring save state: %s\n", filename);
		goto error;
	}
	success = 1;
	EmuTime_reset(); // the loaded state starts a new emulated timeline

error:
	if (state)
		free(state);
	if (state_rzfile)
		rzipstream_close(state_rzfile);
	fast_forward = was_ff;
	return success;
}

int State_write(void) { // from picoarch
	// Block save states in RetroAchievements hardcore mode
	if (RA_isHardcoreModeActive()) {
		Notification_push(NOTIFICATION_ACHIEVEMENT, "Save states disabled in Hardcore mode", NULL);
		return 0;
	}

	int success = 0;
	size_t state_size = core.serialize_size();
	if (!state_size)
		return 0;

	int was_ff = fast_forward;
	fast_forward = 0;

	char filename[MAX_PATH];
	State_getPath(filename);

	void* state = calloc(1, state_size);
	if (!state) {
		LOG_error("Couldn't allocate memory for state\n");
		goto error;
	}

	if (!core.serialize(state, state_size)) {
		LOG_error("Error serializing save state\n");
		goto error;
	}

	if (!writeSaveFile(filename, state, state_size,
					   CFG_getStateFormat() == STATE_FORMAT_SRM || CFG_getStateFormat() == STATE_FORMAT_SRM_EXTRADOT)) {
		LOG_error("Error writing state data to file: %s (%s)\n", filename, strerror(errno));
		goto error;
	}
	success = 1;

error:
	if (state)
		free(state);

	sync();
	fast_forward = was_ff;
	return success;
}

///////////////////////////////////////

// Keep the state from immediately before the last successful manual load in
// memory. Undo swaps this snapshot with the current state, so invoking it a
// second time returns to the state that was originally loaded.
static uint8_t* undo_state = NULL;
static size_t undo_state_size = 0;
static int undo_state_valid = 0;

static int State_captureUndo(void) {
	size_t state_size = core.serialize_size();
	if (!state_size)
		return 0;

	if (state_size != undo_state_size) {
		uint8_t* buffer = realloc(undo_state, state_size);
		if (!buffer) {
			LOG_error("Couldn't allocate memory for undo state\n");
			return 0;
		}
		undo_state = buffer;
		undo_state_size = state_size;
	}

	if (!core.serialize(undo_state, undo_state_size)) {
		LOG_error("Error serializing undo state\n");
		return 0;
	}

	return 1;
}

int State_readWithUndo(void) {
	// Let State_read emit the standard Hardcore notification without doing
	// unnecessary serialization or replacing an existing undo snapshot.
	if (RA_isHardcoreModeActive())
		return State_read();

	int captured = State_captureUndo();
	int success = State_read();
	undo_state_valid = captured && success;
	return success;
}

int State_hasUndo(void) {
	if (RA_isHardcoreModeActive() || !undo_state_valid)
		return 0;

	// Some cores can change their serialized-state size at runtime. Once that
	// happens, the snapshot is no longer safe even if the size later changes
	// back to its previous value.
	if (undo_state_size != core.serialize_size()) {
		undo_state_valid = 0;
		return 0;
	}

	return 1;
}

int State_undoLoad(void) {
	// Undoing is another state load, so it follows the same Hardcore rule.
	if (RA_isHardcoreModeActive()) {
		LOG_info("Undo load blocked - hardcore mode active\n");
		Notification_push(NOTIFICATION_ACHIEVEMENT, "Load states disabled in Hardcore mode", NULL);
		return 0;
	}

	if (!State_hasUndo())
		return 0;

	int was_ff = fast_forward;
	fast_forward = 0;

	// Preserve the current state so another undo swaps back to it. If this
	// allocation or serialization fails, the first undo can still succeed, but
	// there will be no redo snapshot.
	uint8_t* current_state = malloc(undo_state_size);
	if (current_state && !core.serialize(current_state, undo_state_size)) {
		free(current_state);
		current_state = NULL;
	}

	int success = core.unserialize(undo_state, undo_state_size);
	if (!success) {
		LOG_error("Error restoring undo state\n");
		undo_state_valid = 0;
	} else if (current_state) {
		memcpy(undo_state, current_state, undo_state_size);
	} else {
		undo_state_valid = 0;
	}

	free(current_state);
	fast_forward = was_ff;
	return success;
}

void State_invalidateUndo(void) {
	undo_state_valid = 0;
}

void State_freeUndo(void) {
	free(undo_state);
	undo_state = NULL;
	undo_state_size = 0;
	undo_state_valid = 0;
}

///////////////////////////////////////

void State_autosave(void) {
	int last_state_slot = state_slot;
	state_slot = AUTO_RESUME_SLOT;
	State_write();
	state_slot = last_state_slot;
}
// A resume the core refused before its first frame (mupen64plus-next only
// accepts states once its emulation thread runs): retried from run_frame.
#define RESUME_RETRY_EVERY 10 // frames between attempts
#define RESUME_RETRY_MAX 30	  // ~5 s at 60 fps, then the game just starts fresh
static int pending_resume_slot = -1;
static int pending_resume_wait = 0;
static int pending_resume_tries = 0;

static int State_readSlot(int slot) {
	int last_state_slot = state_slot;
	state_slot = slot;
	int ok = State_read();
	state_slot = last_state_slot;
	if (ok)
		Rewind_on_state_change();
	return ok;
}

SDL_Surface* State_loadResumePreview(const char* rom_path, const char* tag_name) {
	// netplay starts from a clean boot (see main), and a client's slot
	// markers live in its isolated dir
	if (getenv("NETPLAY_ROLE") || getenv("NX_CORE_NETPLAY") || getenv("NETPLAY_SAVES_DIR"))
		return NULL;
	if (!exists(RESUME_SLOT_PATH))
		return NULL;
	int slot = getInt(RESUME_SLOT_PATH);
	if (slot < 0 || slot > AUTO_RESUME_SLOT)
		return NULL;
	// named like Menu_init's slot files: the m3u's name for a multi-disc game
	char m3u_path[MAX_PATH];
	const char* name = M3U_findForRom(rom_path, m3u_path, sizeof(m3u_path)) ? baseName(m3u_path) : baseName(rom_path);
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/.minui/%s/%s.%d.bmp", SHARED_USERDATA_PATH, tag_name, name, slot);
	if (!exists(path))
		return NULL;
	SDL_Surface* image = IMG_Load(path); // a PNG despite the name
	if (!image)
		return NULL;
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(image);
	if (converted)
		SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
	return converted;
}

void State_resume(void) {
	if (!exists(RESUME_SLOT_PATH))
		return;

	int slot = getInt(RESUME_SLOT_PATH);
	unlink(RESUME_SLOT_PATH);
	if (slot < 0 || slot > AUTO_RESUME_SLOT)
		return; // corrupt slot file

	if (State_readSlot(slot) || RA_isHardcoreModeActive())
		return;
	char filename[MAX_PATH];
	int last_state_slot = state_slot;
	state_slot = slot;
	State_getPath(filename);
	state_slot = last_state_slot;
	if (!exists(filename))
		return;
	pending_resume_slot = slot;
	pending_resume_wait = RESUME_RETRY_EVERY;
	pending_resume_tries = 0;
	LOG_info("Resume state not accepted yet, retrying once the core runs\n");
}

int State_isResumePending(void) {
	return pending_resume_slot >= 0;
}

void State_resumePending(void) {
	if (pending_resume_slot < 0 || --pending_resume_wait > 0)
		return;
	pending_resume_wait = RESUME_RETRY_EVERY;
	if (State_readSlot(pending_resume_slot) || ++pending_resume_tries >= RESUME_RETRY_MAX)
		pending_resume_slot = -1;
}
