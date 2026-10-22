#include "ratools_prefetch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zip.h>

#include <rcheevos/rc_api_runtime.h>
#include <rcheevos/rc_api_user.h> // rc_api_start_session_request_t
#include <rcheevos/rc_hash.h>
#include <rcheevos/rc_consoles.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "http.h"
#include "ra_consoles.h"
#include "wifi.h"
#include "ra_hash_cdreader.h"
#include "ra_offline.h"
#include "ra_offline_net.h"
#include "ra_badge_path.h"
#include "ra_hash_cache.h"
#include "ra_paths.h"
#include "ui_buttonhintbar.h"
#include "utils.h"
#include "ui_downloadprogress.h"
#include "ui_menubar.h"
#include "ui_listview.h"
#include "ui_loadingoverlay.h"
#include "cjson/cJSON.h"

// An archived rom is unpacked here for hashing, then deleted (/tmp is RAM)
#define RAT_UNPACK_DIR "/tmp/ratools_unpack"
// bigger roms inside an archive are left alone rather than unpacked to RAM
#define RAT_UNPACK_MAX (128LL * 1024 * 1024)
// and one is only unpacked while this much of /tmp stays free besides it
#define RAT_UNPACK_RESERVE (48LL * 1024 * 1024)

typedef struct {
	char path[512];
	// the rom inside a .zip/.7z (path within the archive), "" for a plain file
	char entry[256];
	long long entry_size; // unpacked size of entry
	int console_id;
} RAT_RomFile;

// grows as the scan finds roms: no cap on the library size
typedef struct {
	RAT_RomFile* items;
	int count, cap;
} RAT_RomList;

static RAT_RomFile* rat_rom_add(RAT_RomList* list) {
	if (list->count == list->cap) {
		int cap = list->cap ? list->cap * 2 : 256;
		RAT_RomFile* grown = realloc(list->items, (size_t)cap * sizeof(*grown));
		if (!grown)
			return NULL;
		list->items = grown;
		list->cap = cap;
	}
	return &list->items[list->count++];
}

// Only prefetch systems whose emulator pak is actually installed, mirroring
// nxredux's launchability rule (getEmuPath: the system paks first, then SD
// Emus/<TAG>.pak, then Emus/<platform>/<TAG>.pak) - there is no point caching achievements for roms the device
// cannot launch.
static bool rat_has_emu(const char* tag) {
	char pak_path[512];
	getEmuPath((char*)tag, pak_path);
	return exists(pak_path);
}

// "Game Boy (GB)" -> "GB"
static bool rat_dir_tag(const char* dirname, char* tag, size_t n) {
	const char* o = strrchr(dirname, '(');
	if (!o)
		return false;
	const char* c = strchr(o, ')');
	if (!c || c <= o + 1)
		return false;
	size_t len = (size_t)(c - o - 1);
	if (len >= n)
		len = n - 1;
	memcpy(tag, o + 1, len);
	tag[len] = '\0';
	return true;
}

static bool rat_skip_extension(const char* name) {
	const char* ext = strrchr(name, '.');
	if (!ext)
		return true;
	ext++;
	// raw/sub are only ever disc tracks (CD-DA / subchannel), never a game
	static const char* deny[] = {"txt", "dat", "png", "jpg", "jpeg", "bmp", "xml",
								 "db", "sav", "srm", "st", "cfg", "log", "bak",
								 "raw", "sub", NULL};
	for (int i = 0; deny[i]; i++)
		if (strcasecmp(ext, deny[i]) == 0)
			return true;
	return false;
}

// true if `name` is a raw disc track (bin/img/raw/sub) that belongs to a
// cue/gdi/ccd/toc index rather than being a game on its own.
static bool rat_is_track_file(const char* name) {
	const char* ext = strrchr(name, '.');
	if (!ext)
		return false;
	ext++;
	static const char* tracks[] = {"bin", "img", "raw", "sub", NULL};
	for (int i = 0; tracks[i]; i++)
		if (strcasecmp(ext, tracks[i]) == 0)
			return true;
	return false;
}

static bool rat_is_archive(const char* name) {
	const char* ext = strrchr(name, '.');
	return ext && (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".7z"));
}

// The rom an archive holds, the way minarch picks it: the first file that
// isn't a directory, an archive or a skipped extension. Its path within the
// archive goes to `entry`, its uncompressed size to *size.
static bool rat_zip_entry(const char* path, char* entry, size_t n, long long* size) {
	int ze;
	zip_t* za = zip_open(path, ZIP_RDONLY, &ze);
	if (!za)
		return false;
	bool found = false;
	zip_int64_t count = zip_get_num_entries(za, 0);
	for (zip_int64_t i = 0; i < count && !found; i++) {
		zip_stat_t sb;
		if (zip_stat_index(za, i, 0, &sb) != 0 || !(sb.valid & ZIP_STAT_NAME))
			continue;
		size_t len = strlen(sb.name);
		if (!len || sb.name[len - 1] == '/' || len >= n)
			continue;
		const char* base = strrchr(sb.name, '/');
		base = base ? base + 1 : sb.name;
		if (rat_skip_extension(base) || rat_is_archive(base))
			continue;
		snprintf(entry, n, "%s", sb.name);
		*size = (sb.valid & ZIP_STAT_SIZE) ? (long long)sb.size : 0;
		found = true;
	}
	zip_close(za);
	return found;
}

// Runs 7zzs with argv[1..]; its stdout goes to out_fd (-1: /dev/null).
static bool rat_run_7z(char** argv, int out_fd) {
	static char bin[512];
	snprintf(bin, sizeof(bin), "%s/7zzs.aarch64", SHARED_BIN_PATH);
	argv[0] = bin;
	pid_t pid = fork();
	if (pid == 0) {
		int fd = out_fd >= 0 ? out_fd : open("/dev/null", O_WRONLY);
		if (fd >= 0)
			dup2(fd, STDOUT_FILENO);
		execv(argv[0], argv);
		_exit(127);
	}
	if (pid < 0)
		return false;
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// rat_zip_entry for a .7z, from 7zzs' technical listing ("Path = ...",
// "Size = ...", "Attributes = D..." per entry, blank line between entries).
static bool rat_7z_entry(const char* path, char* entry, size_t n, long long* size) {
	FILE* tmp = tmpfile();
	if (!tmp)
		return false;
	char* argv[] = {NULL, "l", "-ba", "-slt", "-sccUTF-8", "--", (char*)path, NULL};
	bool ok = rat_run_7z(argv, fileno(tmp));
	bool found = false;
	if (ok) {
		rewind(tmp);
		char line[512], name[256] = {0};
		long long sz = 0;
		bool dir = false;
		// the trailing "" flushes the last entry
		while (!found) {
			bool more = fgets(line, sizeof(line), tmp) != NULL;
			line[strcspn(line, "\r\n")] = '\0';
			if (!more || !line[0]) {
				const char* base = strrchr(name, '/');
				base = base ? base + 1 : name;
				if (name[0] && !dir && !rat_skip_extension(base) && !rat_is_archive(base)) {
					snprintf(entry, n, "%s", name);
					*size = sz;
					found = true;
				}
				name[0] = '\0';
				sz = 0;
				dir = false;
				if (!more)
					break;
			} else if (!strncmp(line, "Path = ", 7)) {
				if (strlen(line + 7) < sizeof(name) && strlen(line + 7) < n)
					snprintf(name, sizeof(name), "%s", line + 7);
			} else if (!strncmp(line, "Size = ", 7)) {
				sz = atoll(line + 7);
			} else if (!strncmp(line, "Attributes = ", 13)) {
				dir = strchr(line + 13, 'D') != NULL;
			}
		}
	}
	fclose(tmp);
	return found;
}

static bool rat_archive_entry(const char* path, char* entry, size_t n, long long* out_size) {
	long long size = 0;
	const char* ext = strrchr(path, '.');
	bool found = !strcasecmp(ext, ".zip") ? rat_zip_entry(path, entry, n, &size)
										  : rat_7z_entry(path, entry, n, &size);
	if (found && size > RAT_UNPACK_MAX) {
		fprintf(stderr, "[prefetch] %s: %s too big to unpack (%lld bytes)\n", path, entry, size);
		return false;
	}
	*out_size = size;
	return found;
}

static void rat_unpack_cleanup(void) {
	DIR* d = opendir(RAT_UNPACK_DIR);
	if (!d)
		return;
	struct dirent* ent;
	char p[512];
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "%s/%s", RAT_UNPACK_DIR, ent->d_name);
		unlink(p);
	}
	closedir(d);
	rmdir(RAT_UNPACK_DIR);
}

// Unpack rom->entry into RAT_UNPACK_DIR, keeping its file name (rc_hash goes
// by extension for some consoles). out gets the unpacked file's path.
static bool rat_unpack(const RAT_RomFile* rom, char* out, size_t n) {
	rat_unpack_cleanup();
	// /tmp is RAM: skip a rom that would leave too little of it free
	struct statvfs sv;
	if (statvfs("/tmp", &sv) == 0) {
		long long avail = (long long)sv.f_bavail * (long long)sv.f_frsize;
		if (avail < rom->entry_size + RAT_UNPACK_RESERVE) {
			fprintf(stderr, "[prefetch] %s: not enough room in /tmp (%lld needed, %lld free)\n",
					rom->path, rom->entry_size, avail);
			return false;
		}
	}
	mkdir(RAT_UNPACK_DIR, 0755);
	const char* base = strrchr(rom->entry, '/');
	base = base ? base + 1 : rom->entry;
	snprintf(out, n, "%s/%s", RAT_UNPACK_DIR, base);

	const char* ext = strrchr(rom->path, '.');
	if (strcasecmp(ext, ".zip") != 0) {
		// -spd: the entry name is literal, not a wildcard. --: switches end
		// there, so an archive or entry name starting with '-' or '@' is a
		// name, not an option or a list file.
		char out_arg[512];
		snprintf(out_arg, sizeof(out_arg), "-o%s", RAT_UNPACK_DIR);
		char* argv[] = {NULL, "e", "-y", "-spd", out_arg, "--", (char*)rom->path, (char*)rom->entry, NULL};
		return rat_run_7z(argv, -1) && exists(out);
	}

	static char buf[65536];
	int ze;
	zip_t* za = zip_open(rom->path, ZIP_RDONLY, &ze);
	if (!za)
		return false;
	bool ok = false;
	zip_file_t* zf = zip_fopen(za, rom->entry, 0);
	int fd = zf ? open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
	if (fd >= 0) {
		zip_int64_t len;
		ok = true;
		while ((len = zip_fread(zf, buf, sizeof(buf))) > 0)
			if (write(fd, buf, (size_t)len) != len) {
				ok = false;
				break;
			}
		if (len < 0)
			ok = false;
		close(fd);
	}
	if (zf)
		zip_fclose(zf);
	zip_close(za);
	return ok;
}

// true if the directory holds a disc index (cue/gdi/ccd/toc); its bin/img/raw/
// sub siblings are then tracks, not games. One extra O(n) pass per directory.
static bool rat_dir_has_disc_index(const char* dir) {
	DIR* d = opendir(dir);
	if (!d)
		return false;
	struct dirent* ent;
	bool found = false;
	while ((ent = readdir(d))) {
		const char* ext = strrchr(ent->d_name, '.');
		if (!ext)
			continue;
		if (!strcasecmp(ext, ".cue") || !strcasecmp(ext, ".gdi") ||
			!strcasecmp(ext, ".ccd") || !strcasecmp(ext, ".toc")) {
			found = true;
			break;
		}
	}
	closedir(d);
	return found;
}

// The libretro core a system's pak launches, as minarch's core.name: the
// EMU_EXE= value (stock and most community paks), else the name in front of
// the first "_libretro.so". Empty when the pak names neither (e.g. a
// standalone emulator) - RA_detectConsole then goes by tag and extension.
static void rat_pak_core(const char* tag, char* out, size_t n) {
	out[0] = '\0';
	char launch[512];
	getEmuPath((char*)tag, launch);
	FILE* f = fopen(launch, "r");
	if (!f)
		return;
	char line[512];
	char fallback[64] = {0};
	while (fgets(line, sizeof(line), f)) {
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "EMU_EXE=", 8) == 0) {
			p += 8;
			if (*p == '"' || *p == '\'')
				p++;
			size_t len = strcspn(p, "\"' \t\r\n;");
			if (len && len < n && !memchr(p, '$', len)) {
				memcpy(out, p, len);
				out[len] = '\0';
				break;
			}
		}
		const char* so = strstr(line, "_libretro.so");
		if (so && !fallback[0]) {
			const char* b = so;
			while (b > line && b[-1] != '/' && b[-1] != '"' && b[-1] != '\'' && b[-1] != ' ')
				b--;
			size_t len = (size_t)(so - b);
			if (len && len < sizeof(fallback) && !memchr(b, '$', len)) {
				memcpy(fallback, b, len);
				fallback[len] = '\0';
			}
		}
	}
	fclose(f);
	if (!out[0] && fallback[0])
		snprintf(out, n, "%s", fallback);
}

// peek: look inside archives for the rom (and its console) the hashing
// needs; the Library's counts only need the archive itself
static void rat_scan_dir(const char* dir, const char* tag, const char* core, int depth,
						 RAT_RomList* roms, bool peek) {
	if (depth > 2)
		return;
	// a cue/gdi/ccd/toc in this directory means its bin/img/raw/sub siblings
	// are disc tracks, not games — skip them so each one doesn't hash as a
	// failure (the index file is the game we hash instead)
	bool has_disc_index = rat_dir_has_disc_index(dir);
	DIR* d = opendir(dir);
	if (!d)
		return;
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			rat_scan_dir(path, tag, core, depth + 1, roms, peek);
		} else if (!rat_skip_extension(ent->d_name) &&
				   !(has_disc_index && rat_is_track_file(ent->d_name))) {
			// same resolver as minarch, so prefetch hashes each ROM under
			// the console the game will be loaded with
			int console_id = RA_detectConsole(tag, core, path);
			if (console_id == RC_CONSOLE_UNKNOWN)
				continue;
			// minarch unpacks an archive (arcade aside: hashed by name) and
			// detects and hashes the rom inside, so go by that rom here too
			char entry[256] = {0};
			long long entry_size = 0;
			if (peek && console_id != RC_CONSOLE_ARCADE && rat_is_archive(ent->d_name)) {
				if (!rat_archive_entry(path, entry, sizeof(entry), &entry_size))
					continue;
				char inner[512];
				snprintf(inner, sizeof(inner), "%s/%s", dir, entry);
				console_id = RA_detectConsole(tag, core, inner);
				if (console_id == RC_CONSOLE_UNKNOWN)
					continue;
			}
			RAT_RomFile* r = rat_rom_add(roms);
			if (!r)
				break;
			snprintf(r->path, sizeof(r->path), "%s", path);
			snprintf(r->entry, sizeof(r->entry), "%s", entry);
			r->entry_size = entry_size;
			r->console_id = console_id;
		}
	}
	closedir(d);
}

// The console folders worth prefetching: an installed emulator pak and a
// system RA supports (a stock RA tag, or a custom pak whose core
// RA_detectConsole knows); core gets the pak's core.
static bool rat_console_folder(const char* name, char* tag, size_t tag_size, char* core,
							   size_t core_size) {
	if (name[0] == '.' || !rat_dir_tag(name, tag, tag_size))
		return false;
	if (!rat_has_emu(tag))
		return false; // no emulator installed for this system
	rat_pak_core(tag, core, core_size);
	return RA_getConsoleId(tag) != RC_CONSOLE_UNKNOWN || ra_core_family(core);
}

// every console folder's roms, or only those of `folder` (a Roms/ entry)
static int rat_collect_roms(RAT_RomFile** out, const char* folder, bool peek) {
	*out = NULL;
	DIR* d = opendir(ROMS_PATH);
	if (!d)
		return 0;
	RAT_RomList roms = {0};
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (folder && strcmp(ent->d_name, folder) != 0)
			continue;
		// other folders (Ports, Extras...) aren't scanned
		char tag[16], core[64];
		if (!rat_console_folder(ent->d_name, tag, sizeof(tag), core, sizeof(core)))
			continue;
		char dir[512];
		snprintf(dir, sizeof(dir), "%s/%s", ROMS_PATH, ent->d_name);
		rat_scan_dir(dir, tag, core, 0, &roms, peek);
	}
	closedir(d);

	if (roms.count == 0) {
		free(roms.items);
		return 0;
	}
	*out = roms.items;
	return roms.count;
}

// ---------------- network helpers (sync, cache-through) ----------------

// POST an rc_api request; on HTTP 200 mirror it into the offline cache and
// return the body (caller frees). NULL on failure.
static char* rat_post_and_cache(rc_api_request_t* request, size_t* out_len) {
	HTTP_Response* resp = HTTP_post(request->url, request->post_data, request->content_type);
	char* body = NULL;
	if (resp && resp->data && !resp->error && resp->http_status == 200) {
		RA_Offline_cacheResponse(request->post_data, resp->data, resp->size);
		body = (char*)malloc(resp->size + 1);
		if (body) {
			memcpy(body, resp->data, resp->size);
			body[resp->size] = '\0';
			if (out_len)
				*out_len = resp->size;
		}
	}
	if (resp)
		HTTP_freeResponse(resp);
	return body;
}

// Returns true when the badge is on disk afterwards (cached or downloaded)
// in the game's badge folder game_dir (ra_badge_path.h, same files as
// minarch's ra_badges.c)
static bool rat_download_badge(const char* game_dir, const char* url, const char* badge_name, bool locked) {
	if (!url || !*url)
		return false;
	char path[1024];
	if (!RA_BadgePath_inDir(game_dir, badge_name, locked, path, sizeof(path)))
		return false;
	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > 0)
		return true; // already cached
	bool ok = false;
	HTTP_Response* resp = HTTP_get(url);
	if (resp && resp->data && !resp->error && resp->http_status == 200 && resp->size > 0) {
		FILE* f = fopen(path, "wb");
		if (f) {
			size_t wr = fwrite(resp->data, 1, resp->size, f);
			bool close_ok = fclose(f) == 0;
			ok = wr == resp->size && close_ok;
			if (!ok)
				remove(path); // truncated write must not satisfy the size>0 skip forever
		}
	}
	if (resp)
		HTTP_freeResponse(resp);
	return ok;
}

// ---------------- UI ----------------

static void rat_pf_render(SDL_Surface* screen, const char* line1, const char* line2,
						  int done, int total) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, "RetroAchievements | Game Data");

	char detail[192];
	snprintf(detail, sizeof(detail), "%s (%d/%d)", line2 ? line2 : "", done, total);

	UIDownloadProgress info = {
		.title = NULL, // menu bar drawn above
		.status = line1,
		.detail = detail,
		.progress = total > 0 ? (done * 100) / total : 0,
		.show_bar = true,
	};
	UI_renderDownloadProgress(screen, &info);

	UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(screen);
}

static void rat_pf_message(SDL_Surface* screen, const char* line1, const char* line2) {
	bool quit = false, dirty = true;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			quit = true;
		if (dirty) {
			GFX_clear(screen);
			SDL_Color white = {255, 255, 255, 255};
			SDL_Surface* s = GFX_renderText(font.medium, line1, white);
			if (s) {
				SDL_BlitSurface(s, NULL, screen,
								&(SDL_Rect){(screen->w - s->w) / 2, screen->h / 2 - SCALE1(30), s->w, s->h});
				SDL_FreeSurface(s);
			}
			if (line2) {
				s = GFX_renderText(font.small, line2, (SDL_Color){180, 180, 180, 255});
				if (s) {
					SDL_BlitSurface(s, NULL, screen,
									&(SDL_Rect){(screen->w - s->w) / 2, screen->h / 2, s->w, s->h});
					SDL_FreeSurface(s);
				}
			}
			UI_renderButtonHintBar(screen, (char*[]){"A", "OK", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// Download any badge files still missing for a parsed sets response into
// the game's badge folder in rom_path's console folder (rat_download_badge
// skips files already on disk). Returns false if the user cancelled with B
// mid-way.
static bool rat_download_set_badges(SDL_Surface* screen, const char* rom_path,
									const rc_api_fetch_game_sets_response_t* sets,
									const char* label, int done, int total) {
	char game_dir[1024];
	if (!RA_BadgePath_dir(RA_BADGE_CACHE_DIR, rom_path, sets->id, game_dir, sizeof(game_dir)) ||
		!RA_BadgePath_ensureDir(game_dir))
		return true; // game id 0 (or no folder): nothing to store badges in

	int badge_total = 0;
	for (uint32_t s = 0; s < sets->num_sets; s++)
		badge_total += (int)sets->sets[s].num_achievements * 2; // colored + locked

	int badge_done = 0;
	char sub[192];
	snprintf(sub, sizeof(sub), "%s - badge 0/%d", label, badge_total);
	rat_pf_render(screen, "Downloading badges", sub, done, total);

	for (uint32_t s = 0; s < sets->num_sets; s++) {
		for (uint32_t a = 0; a < sets->sets[s].num_achievements; a++) {
			PAD_poll();
			if (PAD_justPressed(BTN_B))
				return false;
			const rc_api_achievement_definition_t* def = &sets->sets[s].achievements[a];
			rat_download_badge(game_dir, def->badge_url, def->badge_name, false);
			rat_download_badge(game_dir, def->badge_locked_url, def->badge_name, true);
			badge_done += 2;
			// refresh every few files so large sets visibly progress
			// (cheap vs the ~0.5s per actual download)
			if ((badge_done & 7) == 0 || badge_done == badge_total) {
				snprintf(sub, sizeof(sub), "%s - badge %d/%d", label, badge_done, badge_total);
				rat_pf_render(screen, "Downloading badges", sub, done, total);
			}
		}
	}
	return true;
}

// rc_hash error sink (see RATPrefetch_run): lands in the pak's log next to
// the "[prefetch] hash failed" line naming the file.
static void rat_hash_error(const char* message) {
	fprintf(stderr, "[prefetch] rc_hash: %s\n", message ? message : "(null)");
}

static void rat_prefetch(SDL_Surface* screen, const char* folder) {
	if (!CFG_getRAAuthenticated() || strlen(CFG_getRAToken()) == 0) {
		rat_pf_message(screen, "Not authenticated",
					   "Set credentials in Settings and authenticate first.");
		return;
	}
	if (!Wifi_isConnected()) {
		rat_pf_message(screen, "No network connection", "Connect to WiFi and try again.");
		return;
	}

	// rat_download_set_badges makes each game's badge folder in the rom's
	// console folder (the fallback dir was created by RA_Offline_init())

	// CHD-aware hashing for disc images
	rc_hash_cdreader_t cdreader;
	RA_HashCdreader_get(&cdreader);
	rc_hash_init_custom_cdreader(&cdreader);
	// rc_hash only reports why a file could not be hashed through this
	// callback; without it a "failed" in the summary is undiagnosable.
	rc_hash_init_error_message_callback(rat_hash_error);

	RAT_RomFile* roms = NULL;
	int total = rat_collect_roms(&roms, folder, true);
	if (total == 0) {
		rat_pf_message(screen, "No roms found", "Nothing to download.");
		return;
	}

	const char* username = CFG_getRAUsername();
	const char* token = CFG_getRAToken();

	// The offline session in minarch needs cache/login.json; users who
	// logged in before the pak wrote it (issue #105) get repaired here, and
	// everyone else gets refreshed point totals. One request, so do it every
	// run; a failure only matters when nothing is cached yet.
	rat_pf_render(screen, "Caching login", username, 0, total);
	if (RA_OfflineNet_cacheLogin(username, token) != 0 && !RA_Offline_hasLoginCache()) {
		free(roms);
		rat_pf_message(screen, "Login check failed",
					   "Could not verify the account. Authenticate again and retry.");
		return;
	}

	int fetched = 0, cached = 0, unknown = 0, failed = 0;
	bool cancelled = false;

	for (int i = 0; i < total && !cancelled; i++) {
		PAD_poll();
		if (PAD_justPressed(BTN_B)) {
			cancelled = true;
			break;
		}

		const char* name = strrchr(roms[i].path, '/');
		name = name ? name + 1 : roms[i].path;

		// a fingerprint cached by an earlier run, minarch or the Android app
		// skips the hashing (unknown games are still re-resolved below: RA
		// may have added them since)
		char hash[33];
		uint32_t console = (uint32_t)roms[i].console_id;
		bool cacheable = true;
		uint32_t cached_game = 0;
		bool from_cache = RA_HashCache_lookup(roms[i].path, console, hash, &cached_game);
		if (from_cache) {
			rat_pf_render(screen, "Checking", name, i + 1, total);
		} else {
			// an archived rom is hashed unpacked, as minarch does; any other
			// raw archive hash is still worth resolving, just not caching
			const char* hash_path = roms[i].path;
			char unpacked[512];
			if (roms[i].entry[0]) {
				rat_pf_render(screen, "Unpacking", name, i + 1, total);
				if (!rat_unpack(&roms[i], unpacked, sizeof(unpacked))) {
					fprintf(stderr, "[prefetch] unpack failed: %s (%s)\n", roms[i].path,
							roms[i].entry);
					rat_unpack_cleanup();
					failed++;
					continue;
				}
				hash_path = unpacked;
			}
			cacheable = RA_HashCache_canStore(hash_path, console);
			rat_pf_render(screen, "Hashing", name, i + 1, total);
			bool hashed = rc_hash_generate_from_file(hash, console, hash_path);
			if (roms[i].entry[0])
				rat_unpack_cleanup();
			if (!hashed) {
				fprintf(stderr, "[prefetch] hash failed (console %d): %s\n",
						roms[i].console_id, roms[i].path);
				failed++;
				continue;
			}
		}

		// skip refetching games whose data is already cached — but still
		// back-fill the rom path and repair any badges a previous run (or
		// minarch's async in-game prefetch, cut off at quit) left missing;
		// rat_download_badge skips files already on disk, so a complete
		// game costs only stat() calls here
		char rel[192];
		snprintf(rel, sizeof(rel), "cache/games/%s/sets.json", hash);
		char* probe = NULL;
		size_t probe_len = 0;
		if (RA_Offline_readCacheFile(rel, &probe, &probe_len)) {
			RA_Offline_setGameRomPath(hash, roms[i].path);

			rc_api_server_response_t sr;
			memset(&sr, 0, sizeof(sr));
			sr.body = probe;
			sr.body_length = probe_len;
			sr.http_status_code = 200;
			rc_api_fetch_game_sets_response_t cached_sets;
			if (rc_api_process_fetch_game_sets_server_response(&cached_sets, &sr) == RC_OK &&
				cached_sets.response.succeeded) {
				if (cacheable)
					RA_HashCache_store(roms[i].path, console, hash, cached_sets.id);
				// cancel mid-repair keeps sets.json: the data is complete and
				// the missing badges are retried on the next run anyway
				if (!rat_download_set_badges(screen, roms[i].path, &cached_sets,
											 cached_sets.title ? cached_sets.title : name,
											 i + 1, total))
					cancelled = true;
			}
			rc_api_destroy_fetch_game_sets_response(&cached_sets);
			free(probe);
			cached++;
			continue;
		}

		rat_pf_render(screen, "Fetching achievement data", name, i + 1, total);

		// 1) hash -> game id
		rc_api_resolve_hash_request_t hreq;
		memset(&hreq, 0, sizeof(hreq));
		hreq.game_hash = hash;
		rc_api_request_t request;
		if (rc_api_init_resolve_hash_request(&request, &hreq) != RC_OK) {
			rc_api_destroy_request(&request);
			failed++;
			continue;
		}
		size_t blen = 0;
		char* body = rat_post_and_cache(&request, &blen);
		rc_api_destroy_request(&request);
		if (!body) {
			failed++;
			continue;
		}
		rc_api_resolve_hash_response_t hresp;
		rc_api_server_response_t sr = {.body = body, .body_length = blen, .http_status_code = 200};
		int hrc = rc_api_process_resolve_hash_server_response(&hresp, &sr);
		bool resolved = (hrc == RC_OK && hresp.response.succeeded);
		bool known = resolved && hresp.game_id != 0;
		uint32_t game_id = known ? hresp.game_id : 0;
		rc_api_destroy_resolve_hash_response(&hresp);
		free(body);
		if (!resolved) {
			failed++; // server error / corrupt response — NOT "not in RA"
			continue;
		}
		if (from_cache && !known && cached_game != 0) {
			// stored for a game, unknown now: the file changed keeping its size
			// and mtime, or RA dropped the hash. Forget it; the next launch or
			// run hashes the file again.
			RA_HashCache_forget(roms[i].path);
		} else if (cacheable) { // authoritative either way: game id, or 0 = not in RA
			RA_HashCache_store(roms[i].path, console, hash, game_id);
		}
		if (!known) {
			unknown++; // authoritatively resolved: hash not in RA database
			continue;
		}

		// 2) achievement definitions (by hash)
		rc_api_fetch_game_sets_request_t sreq;
		memset(&sreq, 0, sizeof(sreq));
		sreq.username = username;
		sreq.api_token = token;
		sreq.game_hash = hash;
		if (rc_api_init_fetch_game_sets_request(&request, &sreq) != RC_OK) {
			rc_api_destroy_request(&request);
			failed++;
			continue;
		}
		body = rat_post_and_cache(&request, &blen);
		rc_api_destroy_request(&request);
		if (!body) {
			failed++;
			continue;
		}
		rc_api_fetch_game_sets_response_t sets;
		sr = (rc_api_server_response_t){.body = body, .body_length = blen, .http_status_code = 200};
		bool sets_ok = rc_api_process_fetch_game_sets_server_response(&sets, &sr) == RC_OK &&
					   sets.response.succeeded;
		free(body);
		if (!sets_ok) {
			rc_api_destroy_fetch_game_sets_response(&sets);
			failed++;
			continue;
		}

		// 3) user unlock state
		rc_api_start_session_request_t ssreq;
		memset(&ssreq, 0, sizeof(ssreq));
		ssreq.username = username;
		ssreq.api_token = token;
		ssreq.game_id = sets.session_game_id;
		if (rc_api_init_start_session_request(&request, &ssreq) == RC_OK) {
			body = rat_post_and_cache(&request, &blen);
			rc_api_destroy_request(&request);
			free(body); // cached as a side effect; content not needed here
		} else {
			// session cache is non-critical: skip the post but keep going
			rc_api_destroy_request(&request);
		}

		// 4) badges (both variants), with cancel checks between downloads
		if (!rat_download_set_badges(screen, roms[i].path, &sets,
									 sets.title ? sets.title : name, i + 1, total))
			cancelled = true;
		if (cancelled) {
			// partial badge set: drop the cached sets.json so the next
			// prefetch run refetches this game instead of skipping it
			char sets_path[512];
			snprintf(sets_path, sizeof(sets_path), "%s/%s", RA_USER_DIR, rel);
			remove(sets_path);
		}
		rc_api_destroy_fetch_game_sets_response(&sets);
		if (!cancelled) {
			RA_Offline_setGameRomPath(hash, roms[i].path);
			fetched++;
		}
	}

	char line1[64], line2[128];
	snprintf(line1, sizeof(line1), cancelled ? "Cancelled" : "Done");
	snprintf(line2, sizeof(line2), "%d fetched, %d already cached, %d not in RA, %d failed",
			 fetched, cached, unknown, failed);
	rat_pf_message(screen, line1, line2);
	free(roms);
}

void RATPrefetch_run(SDL_Surface* screen) {
	rat_prefetch(screen, NULL);
}

// ---------------- Library ----------------

typedef struct {
	char folder[256];		   // the Roms/ entry, eg. "Game Boy (GB)"
	char label[MAX_PATH + 24]; // as the Artwork Manager's Library shows it, eg. "Game Boy (GB)"
	int done;				   // roms already identified (achievement data cached, or not in RA)
	int total;
} RAT_Console;

static int rat_console_cmp(const void* a, const void* b) {
	return strcasecmp(((const RAT_Console*)a)->folder, ((const RAT_Console*)b)->folder);
}

// How many of the folder's roms a prefetch has nothing left to do for: a
// fingerprint in hashes.json that is either unknown to RA (game 0) or has its
// achievement data cached. One hashes.json read per console, no hashing.
static void rat_console_progress(RAT_Console* c) {
	RAT_RomFile* roms = NULL;
	c->total = rat_collect_roms(&roms, c->folder, false);
	c->done = 0;
	if (!c->total)
		return;

	char dir[512], path[600];
	if (!RA_HashCache_folderDir(roms[0].path, dir, sizeof(dir))) {
		free(roms);
		return;
	}
	snprintf(path, sizeof(path), "%s/hashes.json", dir);
	char* json = NULL;
	FILE* f = fopen(path, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		long len = ftell(f);
		rewind(f);
		json = len > 0 ? malloc((size_t)len + 1) : NULL;
		if (json)
			json[fread(json, 1, (size_t)len, f)] = '\0';
		fclose(f);
	}
	cJSON* root = json ? cJSON_Parse(json) : NULL;
	free(json);
	const cJSON* files = cJSON_GetObjectItemCaseSensitive(root, "files");

	char prefix[600];
	int plen = snprintf(prefix, sizeof(prefix), "%s/%s/", ROMS_PATH, c->folder);
	for (int i = 0; files && i < c->total; i++) {
		if (strncmp(roms[i].path, prefix, (size_t)plen) != 0)
			continue;
		const cJSON* e = cJSON_GetObjectItemCaseSensitive(files, roms[i].path + plen);
		const cJSON* hash = cJSON_GetObjectItemCaseSensitive(e, "hash");
		const cJSON* game = cJSON_GetObjectItemCaseSensitive(e, "game");
		if (!cJSON_IsString(hash))
			continue;
		if (!cJSON_IsNumber(game) || game->valuedouble == 0) {
			c->done++; // not in RA: nothing to download
			continue;
		}
		char sets[256];
		snprintf(sets, sizeof(sets), "%s/%s/sets.json", RA_GAMES_DIR, hash->valuestring);
		if (exists(sets))
			c->done++;
	}
	cJSON_Delete(root);
	free(roms);
}

static int rat_collect_consoles(RAT_Console** out) {
	*out = NULL;
	DIR* d = opendir(ROMS_PATH);
	if (!d)
		return 0;
	RAT_Console* list = NULL;
	int count = 0, cap = 0;
	struct dirent* ent;
	while ((ent = readdir(d))) {
		char tag[16], core[64], dir[512];
		snprintf(dir, sizeof(dir), "%s/%s", ROMS_PATH, ent->d_name);
		struct stat st;
		if (strlen(ent->d_name) >= sizeof(list->folder) || stat(dir, &st) != 0 ||
			!S_ISDIR(st.st_mode) ||
			!rat_console_folder(ent->d_name, tag, sizeof(tag), core, sizeof(core)))
			continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 32;
			RAT_Console* grown = realloc(list, cap * sizeof(*list));
			if (!grown)
				break;
			list = grown;
		}
		RAT_Console* c = &list[count];
		snprintf(c->folder, sizeof(c->folder), "%s", ent->d_name);
		char display[MAX_PATH];
		getDisplayName(ent->d_name, display);
		char* shown = display;
		trimSortingMeta(&shown); // "01) Game Boy" -> "Game Boy"
		snprintf(c->label, sizeof(c->label), "%s (%s)", shown, tag);
		rat_console_progress(c);
		if (c->total > 0) // an empty folder has nothing to download
			count++;
	}
	closedir(d);
	// launcher order: by folder name (keeps any "01) " sort prefixes in play)
	if (count)
		qsort(list, count, sizeof(*list), rat_console_cmp);
	*out = list;
	return count;
}

static void rat_console_row(void* ctx, int i, bool selected, ListViewRow* out) {
	(void)selected;
	static char badge[32];
	RAT_Console* c = &((RAT_Console*)ctx)[i];
	snprintf(badge, sizeof(badge), "%d/%d", c->done, c->total);
	out->label = c->label;
	out->badge = badge;
}

static void rat_library_loading(SDL_Surface* screen) {
	GFX_clear(screen);
	UI_renderLoadingOverlay(screen, "Scanning library", NULL);
	GFX_flip(screen);
}

void RATPrefetch_runLibrary(SDL_Surface* screen) {
	rat_library_loading(screen);
	RAT_Console* consoles = NULL;
	int count = rat_collect_consoles(&consoles);

	// function scope: the hint arrays must outlive UI_listViewRender
	char* hints_full[] = {"B", "BACK", "Y", "DOWNLOAD ALL", "A", "DOWNLOAD", NULL};
	char* hints_empty[] = {"B", "BACK", NULL};
	static ListView view;
	UI_listViewReset(&view, count, (const void*)&view);
	view.title = "RetroAchievements | Library";
	view.font = font.large;
	view.get_row = rat_console_row;
	view.list_id = (const void*)&view;
	view.empty_title = "No consoles found";
	view.empty_subtitle = "No installed system with roms has achievements.";

	bool done = false, dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	while (!done) {
		GFX_startFrame();
		PAD_poll();

		ListViewAction act = UI_listViewHandleInput(&view);
		bool ran = false;
		if (act.type == LISTVIEW_BACK) {
			done = true;
		} else if (act.type == LISTVIEW_ACTIVATED && act.index >= 0) {
			GFX_clearLayers(LAYER_SCROLLTEXT); // a marquee must not outlive the list
			rat_prefetch(screen, consoles[act.index].folder);
			ran = true;
		} else if (act.type == LISTVIEW_BUTTON && act.btn == BTN_Y && count > 0) {
			GFX_clearLayers(LAYER_SCROLLTEXT);
			rat_prefetch(screen, NULL);
			ran = true;
		}
		if (ran) {
			// fresh counts; the selection stays on the console just downloaded
			int keep = view.selected;
			rat_library_loading(screen);
			free(consoles);
			count = rat_collect_consoles(&consoles);
			view.selected = keep < count ? keep : (count > 0 ? count - 1 : 0);
			PAD_reset(); // the A/B that closed the summary is still latched
			dirty = true;
		}
		if (UI_listViewBusy(&view))
			dirty = true;

		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			view.count = count;
			view.ctx = consoles;
			view.hint_pairs = count > 0 ? hints_full : hints_empty;
			GFX_clear(screen);
			UI_listViewRender(&view, screen);
			GFX_flip(screen);
			dirty = false;
		} else {
			UI_listViewTickIdle(&view);
			GFX_sync();
		}
	}
	GFX_clearLayers(LAYER_SCROLLTEXT);
	free(consoles);
}
