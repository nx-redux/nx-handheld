#ifndef _GNU_SOURCE
#define _GNU_SOURCE // for strcasestr
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "defines.h"
#include "utils.h"

///////////////////////////////////////

volatile bool app_quit = false;

void sig_handler(int sig) {
	if (sig == SIGINT || sig == SIGTERM)
		app_quit = true;
}

void setup_signal_handlers(void) {
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
}

///////////////////////////////////////

int prefixMatch(char* pre, const char* str) {
	return (strncasecmp(pre, str, strlen(pre)) == 0);
}
int suffixMatch(char* suf, const char* str) {
	int len = strlen(suf);
	int offset = strlen(str) - len;
	return (offset >= 0 && strncasecmp(suf, str + offset, len) == 0);
}
int exactMatch(const char* str1, const char* str2) {
	if (!str1 || !str2)
		return 0; // NULL isn't safe here
	size_t len1 = strlen(str1);
	if (len1 != strlen(str2))
		return 0;
	return (strncmp(str1, str2, len1) == 0);
}
int containsString(char* haystack, char* needle) {
	return strcasestr(haystack, needle) != NULL;
}
int hide(char* file_name) {
	return file_name[0] == '.' || suffixMatch(".disabled", file_name) || exactMatch("map.txt", file_name);
}
char* splitString(char* str, const char* delim) {
	char* p = strstr(str, delim);
	if (p == NULL)
		return NULL;		  // delimiter not found
	*p = '\0';				  // terminate string after head
	return p + strlen(delim); // return tail substring
}
void truncateString(char* string, size_t max_len) {
	if (max_len == 0)
		return; // no room even for a terminator

	size_t len = strlen(string) + 1;
	if (len <= max_len)
		return;

	if (max_len < 4) {
		// too small for an ellipsis — plain NUL truncation
		string[max_len - 1] = '\0';
		return;
	}

	strncpy(&string[max_len - 4], "...\0", 4);
}
void wrapString(char* string, size_t max_len, size_t max_lines) {
	char* line = string;

	for (size_t i = 1; i < max_lines; i++) {
		char* p = line;
		char* prev;
		do {
			prev = p;
			p = strchr(prev + 1, ' ');
		} while (p && p - line < (int)max_len);

		if (!p && strlen(line) < max_len)
			break;

		if (prev && prev != line) {
			line = prev + 1;
			*prev = '\n';
		}
	}
	truncateString(line, max_len);
}
// based on https://stackoverflow.com/a/31775567/145965
// Replaces every `search` in line (a buffer of buf_size bytes) in place.
// Returns the number of replacements, or -1 when the result does not fit; the
// matches before the one that did not fit stay replaced.
int replaceString(char* line, size_t buf_size, const char* search, const char* replace) {
	char* sp; // start of pattern
	if (!*search || (sp = strstr(line, search)) == NULL) {
		return 0;
	}
	size_t sLen = strlen(search);
	size_t rLen = strlen(replace);
	size_t tail_len = strlen(sp + sLen);
	size_t new_total = (sp - line) + rLen + tail_len + 1;
	if (new_total > buf_size) {
		return -1; // insufficient buffer space
	}
	if (sLen > rLen) {
		// move from right to left
		char* src = sp + sLen;
		char* dst = sp + rLen;
		while ((*dst = *src) != '\0') {
			dst++;
			src++;
		}
	} else if (sLen < rLen) {
		// move from left to right
		memmove(sp + rLen, sp + sLen, tail_len + 1);
	}
	memcpy(sp, replace, rLen);
	// The rest of the line only has the space left after this replacement.
	char* rest = sp + rLen;
	int more = replaceString(rest, buf_size - (size_t)(rest - line), search, replace);
	return more < 0 ? -1 : 1 + more;
}

int shellEscapeQuotes(char* dst, size_t dst_size, const char* src) {
	if (!dst || dst_size == 0)
		return -1;
	if (!src)
		src = "";
	size_t need = 0;
	for (const char* c = src; *c; c++)
		need += (*c == '\'') ? 4 : 1;
	if (need + 1 > dst_size) {
		dst[0] = '\0';
		return -1;
	}
	char* p = dst;
	for (const char* c = src; *c; c++) {
		if (*c == '\'') {
			// close quote, escaped quote, reopen quote
			memcpy(p, "'\\''", 4);
			p += 4;
		} else {
			*p++ = *c;
		}
	}
	*p = '\0';
	return (int)need;
}

char* shellQuoteAlloc(const char* src) {
	if (!src)
		src = "";
	size_t size = 3; // two quotes + NUL
	for (const char* c = src; *c; c++)
		size += (*c == '\'') ? 4 : 1;
	char* out = malloc(size);
	if (!out)
		return NULL;
	out[0] = '\'';
	shellEscapeQuotes(out + 1, size - 2, src);
	out[size - 2] = '\'';
	out[size - 1] = '\0';
	return out;
}

char* escapeSingleQuotes(char* str, size_t buf_size) {
	// All or nothing: a partly escaped string would leave the caller's
	// single-quoted argument unbalanced, so on overflow it is emptied instead.
	size_t len = strlen(str);
	size_t need = len;
	for (const char* c = str; *c; c++)
		if (*c == '\'')
			need += 3;
	if (need + 1 > buf_size) {
		str[0] = '\0';
		return str;
	}
	// Expand in place from the end so no source byte is overwritten unread.
	char* dst = str + need;
	*dst = '\0';
	for (size_t i = len; i-- > 0;) {
		if (str[i] == '\'') {
			dst -= 4;
			memcpy(dst, "'\\''", 4);
		} else {
			*--dst = str[i];
		}
	}
	return str;
}

// This one does not modify the input, cause we arent savages
char* replaceString2(const char* orig, char* rep, char* with) {
	const char* ins; // the next insert point
	char* tmp;		 // varies
	int len_rep;	 // length of rep (the string to remove)
	int len_with;	 // length of with (the string to replace rep with)
	int len_front;	 // distance between rep and end of last rep
	int count;		 // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count)
		ins = tmp + len_rep;

	char* result =
		(char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);
	tmp = result;

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}
// Stores the trimmed input string into the given output buffer, which must be
// large enough to store the result.  If it is too small, the output is
// truncated.
size_t trimString(char* out, size_t len, const char* str, bool first) {
	if (len == 0)
		return 0;

	const char* end;
	size_t out_size;
	bool is_string = false;

	// Trim leading space
	// strchr() matches the set's own terminator, so stop at the NUL first
	while (*str && strchr("\r\n\t {},", (unsigned char)*str) != NULL)
		str++;

	end = str + 1;

	if ((unsigned char)*str == '"') {
		is_string = true;
		str++;
		while (strchr("\r\n\"", (unsigned char)*end) == NULL)
			end++;
	}

	if (*str == 0) // All spaces?
	{
		*out = 0;
		return 1;
	}

	// Trim trailing space
	if (first)
		while (strchr("\r\n\t {},", (unsigned char)*end) == NULL)
			end++;
	else {
		end = str + strlen(str) - 1;
		while (end > str && strchr("\r\n\t {},", (unsigned char)*end) != NULL)
			end--;
		end++;
	}

	if (is_string && (unsigned char)*(end - 1) == '"')
		end--;

	// Set output size to minimum of trimmed string length and buffer size minus
	// 1
	out_size = (size_t)(end - str) < len - 1 ? (size_t)(end - str) : len - 1;

	// Copy trimmed string and add null terminator
	memcpy(out, str, out_size);
	out[out_size] = 0;

	return out_size;
}

void removeParentheses(char* str_out, const char* str_in) {
	char temp[STR_MAX];
	int len = strlen(str_in);
	int c = 0;
	bool inside = false;
	char end_char;

	// STR_MAX - 1: a name of STR_MAX+ chars with no brackets kept every byte and wrote the NUL past temp
	for (int i = 0; i < len && i < STR_MAX - 1; i++) {
		if (!inside && (str_in[i] == '(' || str_in[i] == '[')) {
			end_char = str_in[i] == '(' ? ')' : ']';
			inside = true;
			continue;
		} else if (inside) {
			if (str_in[i] == end_char)
				inside = false;
			continue;
		}
		temp[c++] = str_in[i];
	}

	temp[c] = '\0';

	trimString(str_out, STR_MAX - 1, temp, false);
}
void serializeTime(char* dest_str, int nTime) {
	if (nTime >= 60) {
		int h = nTime / 3600;
		int m = (nTime - 3600 * h) / 60;
		if (h > 0) {
			sprintf(dest_str, "%dh %dm", h, m);
		} else {
			sprintf(dest_str, "%dm %ds", m, nTime - 60 * m);
		}
	} else {
		sprintf(dest_str, "%ds", nTime);
	}
}
void format_time(char* buf, int seconds) {
	int hrs = seconds / 3600;
	int mins = (seconds % 3600) / 60;
	int secs = seconds % 60;
	if (hrs > 0) {
		sprintf(buf, "%d:%02d:%02d", hrs, mins, secs);
	} else {
		sprintf(buf, "%02d:%02d", mins, secs);
	}
}
int countChar(const char* str, char ch) {
	size_t i;
	int count = 0;
	for (i = 0; i <= strlen(str); i++) {
		if (str[i] == ch) {
			count++;
		}
	}
	return count;
}
char* removeExtension(const char* myStr) {
	if (myStr == NULL)
		return NULL;
	char* retStr = (char*)malloc(strlen(myStr) + 1);
	char* lastExt;
	if (retStr == NULL)
		return NULL;
	strcpy(retStr, myStr);
	if ((lastExt = strrchr(retStr, '.')) != NULL && lastExt[1] != '\0' && lastExt[1] != ' ' && lastExt[2] != '\0')
		*lastExt = '\0';
	return retStr;
}
const char* baseName(const char* filename) {
	char* p = strrchr(filename, '/');
	return p ? p + 1 : (char*)filename;
}
void folderPath(const char* path, char* result) {
	char pathCopy[MAX_PATH];
	strncpy(pathCopy, path, sizeof(pathCopy) - 1);
	pathCopy[sizeof(pathCopy) - 1] = '\0';

	char* lastSlash = strrchr(pathCopy, '/'); // Find the last slash
	if (lastSlash != NULL) {
		*lastSlash = '\0';		  // Cut off the filename
		strcpy(result, pathCopy); // Copy the remaining path
	} else {
		strcpy(result, ""); // No folder found
	}
}
void urlEncode(const char* src, char* dst, size_t dst_size) {
	const char* hex = "0123456789ABCDEF";
	size_t j = 0;
	for (size_t i = 0; src[i] && j < dst_size - 4; i++) {
		unsigned char c = (unsigned char)src[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			dst[j++] = c;
		} else if (c == ' ') {
			dst[j++] = '+';
		} else {
			dst[j++] = '%';
			dst[j++] = hex[c >> 4];
			dst[j++] = hex[c & 0x0F];
		}
	}
	dst[j] = '\0';
}
// DJB2 over the string's bytes (unsigned); used for cache filenames and hash buckets
unsigned int hashString(const char* str) {
	unsigned int h = 5381;
	for (const unsigned char* p = (const unsigned char*)str; *p; p++)
		h = h * 33 + *p;
	return h;
}
void cleanName(char* name_out, const char* file_name) {
	char* name_without_ext = removeExtension(file_name);
	char* no_underscores = replaceString2(name_without_ext, "_", " ");
	char* dot_ptr = strstr(no_underscores, ".");
	if (dot_ptr != NULL) {
		char* s = no_underscores;
		while (isdigit(*s) && s < dot_ptr)
			s++;
		if (s != dot_ptr)
			dot_ptr = no_underscores;
		else {
			dot_ptr++;
			if (dot_ptr[0] == ' ')
				dot_ptr++;
		}
	} else {
		dot_ptr = no_underscores;
	}
	removeParentheses(name_out, dot_ptr);
	free(name_without_ext);
	free(no_underscores);
}
bool pathRelativeTo(char* path_out, const char* dir_from, const char* file_to) {
	// path_out is MAX_PATH bytes. realpath() allocates: a resolved path can be up to
	// PATH_MAX, far past MAX_PATH, so it never writes into a fixed buffer here.
	path_out[0] = '\0';

	char* abs_from = realpath(dir_from, NULL);
	char* abs_to = abs_from ? realpath(file_to, NULL) : NULL;
	if (!abs_from || !abs_to) {
		free(abs_from);
		return false;
	}

	char* p1 = abs_from;
	char* p2 = abs_to;
	while (*p1 && (*p1 == *p2)) {
		++p1, ++p2;
	}
	// The shared part ends on a whole path segment: "/a/bc" and "/a/bd/x" share "/a/", not "/a/b".
	if (*p1 || (*p2 && *p2 != '/')) {
		while (p1 > abs_from && p1[-1] != '/')
			--p1, --p2;
	}

	if (*p2 == '/') {
		++p2;
	}

	size_t used = 0;
	bool ok = true;
	if (strlen(p1) > 0) {
		int num_parens = countChar(p1, '/') + 1;
		for (int i = 0; ok && i < num_parens; i++) {
			int n = snprintf(path_out + used, MAX_PATH - used, "../");
			ok = n >= 0 && (size_t)n < MAX_PATH - used;
			if (ok)
				used += n;
		}
	}
	if (ok) {
		int n = snprintf(path_out + used, MAX_PATH - used, "%s", p2);
		ok = n >= 0 && (size_t)n < MAX_PATH - used;
	}
	if (!ok)
		path_out[0] = '\0'; // too long for MAX_PATH: report failure, never a cut path

	free(abs_from);
	free(abs_to);
	return ok;
}

bool isFileExtension(const char* dot) {
	// 2-4 letters or digits after the dot, at least one a letter (.p8, .7z, .doom). Anything
	// else is part of the name: the ". 3" in "Super Mario Bros. 3", or the ".11" in "Game 1.11".
	size_t len = strlen(dot);
	if (dot[0] != '.' || len < 3 || len > 5)
		return false;
	bool letter = false;
	for (const char* c = dot + 1; *c; c++) {
		if (!isalnum((unsigned char)*c))
			return false;
		if (isalpha((unsigned char)*c))
			letter = true;
	}
	return letter;
}

void getDisplayName(const char* in_name, char* out_name) { // NOTE: out_name needs to be MAX_PATH length!
	char* tmp;
	char work_name[MAX_PATH];
	strcpy(work_name, in_name);
	strcpy(out_name, in_name);

	// extract just the filename if necessary
	tmp = strrchr(work_name, '/');
	if (tmp)
		strcpy(out_name, tmp + 1);

	// remove extension(s), eg. .p8.png
	while ((tmp = strrchr(out_name, '.')) != NULL && isFileExtension(tmp))
		tmp[0] = '\0';

	// remove trailing parens (round and square)
	strcpy(work_name, out_name);
	while ((tmp = strrchr(out_name, '(')) != NULL || (tmp = strrchr(out_name, '[')) != NULL) {
		if (tmp == out_name)
			break;
		tmp[0] = '\0';
		tmp = out_name;
	}

	// make sure we haven't nuked the entire name
	if (out_name[0] == '\0')
		strcpy(out_name, work_name);

	// remove trailing whitespace
	tmp = out_name + strlen(out_name) - 1;
	while (tmp > out_name && isspace((unsigned char)*tmp))
		tmp--;
	tmp[1] = '\0';
}
void getEmuName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp;
	strcpy(out_name, in_name);
	tmp = out_name;

	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2)
			tmp2[0] = '\0';
		memmove(out_name, tmp, strlen(tmp) + 1);
		tmp = out_name;
	}

	// finally extract pak name from parenths if present
	tmp = strrchr(tmp, '(');
	if (tmp) {
		tmp += 1;
		memmove(out_name, tmp, strlen(tmp) + 1);
		tmp = strchr(out_name, ')');
		if (tmp) // guard: an unbalanced '(' (no matching ')') would make this NULL
			tmp[0] = '\0';
	}
}
// A shipped pak always wins: a same-named SD pak (an old copy, a community
// pak under a shipped tag) is ignored, so every card runs the shipped one.
void getEmuPath(char* emu_name, char* pak_path) {
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
	if (exists(pak_path))
		return;
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", SDCARD_PATH, emu_name);
	if (exists(pak_path))
		return;
	// community paks follow the MinUI convention of a platform subfolder
	// (e.g. Emus/tg5040/MyEmu.pak) and hardcode that path internally
	sprintf(pak_path, "%s/Emus/" PLATFORM "/%s.pak/launch.sh", SDCARD_PATH, emu_name);
}

void normalizeNewline(char* line) {
	int len = strlen(line);
	if (len > 1 && line[len - 1] == '\n' && line[len - 2] == '\r') { // windows!
		line[len - 2] = '\n';
		line[len - 1] = '\0';
	}
}
void trimTrailingNewlines(char* line) {
	int len = strlen(line);
	while (len > 0 && line[len - 1] == '\n') {
		line[len - 1] = '\0'; // trim newline
		len -= 1;
	}
}
void trimSortingMeta(char** str) { // eg. `001) `
	// TODO: this code is suss
	char* safe = *str;
	while (isdigit(**str))
		*str += 1; // ignore leading numbers

	if (*str[0] == ')') { // then match a closing parenthesis
		*str += 1;
	} else { //  or bail, restoring the string to its original value
		*str = safe;
		return;
	}

	while (isblank(**str))
		*str += 1; // ignore leading space
}

///////////////////////////////////////

void mkdir_p(const char* path) {
	mkdir_p_mode(path, 0777);
}

bool exists(char* path) {
	return access(path, F_OK) == 0;
}
void touch(char* path) {
	close(open(path, O_RDWR | O_CREAT, 0777));
}
int toggle(char* path) {
	if (access(path, F_OK) == 0) {
		unlink(path);
		return 0;
	} else {
		touch(path);
		return 1;
	}
}
void putFile(char* path, char* contents) {
	FILE* file = fopen(path, "w");
	if (file) {
		fputs(contents, file);
		fclose(file);
	}
}
int writeFileAtomic(const char* path, const char* data, size_t len) {
	// stage to a sibling tmp then rename so a power cut or ENOSPC mid-write
	// can never leave a truncated file at path; rename also refreshes the
	// mtime that cache-staleness checks rely on. Data is fsync'd before the
	// rename; callers that must also persist the rename itself (a launch-time
	// power cut) sync() afterwards.
	char tmp[PATH_MAX + 8];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return 0; // the temp name would be cut: never write a stray file
	FILE* file = fopen(tmp, "wb");
	if (!file)
		return 0;
	size_t wr = fwrite(data, 1, len, file);
	// a full card can surface only at flush or close; never rename a short file
	int ok = wr == len && fflush(file) == 0 && fsync(fileno(file)) == 0;
	if (fclose(file) != 0)
		ok = 0;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return 0;
	}
	return 1;
}
void getFile(char* path, char* buffer, size_t buffer_size) {
	if (buffer_size == 0)
		return; // no room even for a terminator
	FILE* file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		long ftell_size = ftell(file);
		// ftell can fail (unseekable file / error) — treat as empty
		size_t size = ftell_size < 0 ? 0 : (size_t)ftell_size;
		if (size > buffer_size - 1)
			size = buffer_size - 1;
		rewind(file);
		fread(buffer, sizeof(char), size, file);
		fclose(file);
		buffer[size] = '\0';
	}
}
char* allocFile(char* path) { // caller must free!
	return allocFileCapped(path, LONG_MAX, NULL);
}
bool SimpleMode_readPin(char pin_out[5]) {
	char buf[16] = {0};
	getFile((char*)SIMPLE_MODE_PATH, buf, sizeof(buf));
	int len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
					   buf[len - 1] == ' ' || buf[len - 1] == '\t'))
		buf[--len] = '\0';
	if (len != 4)
		return false;
	for (int i = 0; i < 4; i++)
		if (buf[i] < '0' || buf[i] > '9')
			return false;
	if (pin_out) {
		memcpy(pin_out, buf, 4);
		pin_out[4] = '\0';
	}
	return true;
}
int getInt(char* path) {
	int i = 0;
	if (path == NULL)
		return i;

	FILE* file = fopen(path, "r");
	if (file != NULL) {
		int res = fscanf(file, "%i", &i);
		fclose(file);
		if (res != 1)
			i = 0; // failed to parse int
	}
	return i;
}
void putInt(char* path, int value) {
	char buffer[16]; // fits INT_MIN
	snprintf(buffer, sizeof(buffer), "%d", value);
	putFile(path, buffer);
}

int run_cmd_capture(const char* cmd, char* output, size_t output_len) {
	FILE* fp = popen(cmd, "r");
	if (!fp) {
		return -1;
	}

	if (output && output_len > 0) {
		output[0] = '\0';
		size_t total = 0;
		char buf[256];
		while (fgets(buf, sizeof(buf), fp) && total < output_len - 1) {
			size_t len = strlen(buf);
			if (total + len >= output_len) {
				len = output_len - total - 1;
			}
			memcpy(output + total, buf, len);
			total += len;
		}
		output[total] = '\0';
	}

	int status = pclose(fp);
	return WEXITSTATUS(status);
}

uint64_t getMicroseconds(void) {
	uint64_t ret;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	ret = (uint64_t)tv.tv_sec * 1000000;
	ret += (uint64_t)tv.tv_usec;

	return ret;
}

#define max(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})

#define min(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

int clamp(int x, int lower, int upper) {
	return min(upper, max(x, lower));
}

const char* json_extract_string(const char* json, const char* key, char* out, size_t out_size) {
	if (!json || !key || !out || out_size == 0)
		return NULL;

	// Search for "key":"value" pattern
	char search[128];
	snprintf(search, sizeof(search), "\"%s\":\"", key);

	const char* start = strstr(json, search);
	if (!start) {
		// Try "key": "value" (with space)
		snprintf(search, sizeof(search), "\"%s\": \"", key);
		start = strstr(json, search);
		if (!start)
			return NULL;
	}

	start += strlen(search);
	const char* end = strchr(start, '"');
	if (!end)
		return NULL;

	size_t len = end - start;
	if (len >= out_size)
		len = out_size - 1;

	strncpy(out, start, len);
	out[len] = '\0';

	return out;
}

void ROM_mediaArtPath(const char* rom_path, char* out, size_t out_size) {
	char dir[MAX_PATH];
	char base[MAX_PATH];
	strncpy(dir, rom_path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char* slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	else
		strcpy(dir, ".");
	const char* bn = strrchr(rom_path, '/');
	bn = bn ? bn + 1 : rom_path;
	strncpy(base, bn, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	char* dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	if (snprintf(out, out_size, "%s/.media/%s.png", dir, base) >= (int)out_size)
		out[0] = '\0'; // too long: no path rather than a cut one
}

// ROM_mediaArtPath with the variant subfolder spliced in:
// /Roms/GBA/Game.gba + "screenshot" -> /Roms/GBA/.media/screenshot/Game.png.
// A NULL or empty variant yields the root mix path.
void ROM_mediaArtVariantPath(const char* rom_path, const char* variant,
							 char* out, size_t out_size) {
	ROM_mediaArtPath(rom_path, out, out_size);
	if (!variant || !variant[0])
		return;

	char base_path[MAX_PATH];
	strncpy(base_path, out, sizeof(base_path) - 1);
	base_path[sizeof(base_path) - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return; // no .media component to splice into
	*slash = '\0';
	if (snprintf(out, out_size, "%s/%s/%s", base_path, variant, slash + 1) >= (int)out_size)
		out[0] = '\0'; // too long: no path rather than a cut one that names another file
}

// Game art the companion app (NX Redux Mobile) fetched may be stored as a JPEG under the same name:
// .media/screenshot/Game.jpg where the path builders name Game.png. The extensions tried, after .png.
static const char* art_alt_exts[] = {".jpg", ".jpeg"};

bool ROM_artExists(char* path, size_t size) {
	if (exists(path))
		return true;
	size_t len = strlen(path);
	if (len < 4 || strcasecmp(path + len - 4, ".png") != 0)
		return false;
	char alt[MAX_PATH];
	for (int i = 0; i < (int)(sizeof(art_alt_exts) / sizeof(art_alt_exts[0])); i++) {
		if (snprintf(alt, sizeof(alt), "%.*s%s", (int)(len - 4), path, art_alt_exts[i]) >= (int)sizeof(alt))
			continue;
		if (exists(alt) && strlen(alt) < size) {
			snprintf(path, size, "%s", alt);
			return true;
		}
	}
	return false;
}

// Maps an ArtType (config.h: 0 mix, 1 screenshot, 2 box art) to its variant
// folder. Kept int-typed so utils.c stays free of config.h.
static const char* artVariantFolder(int art_type) {
	switch (art_type) {
	case 1:
		return "screenshot";
	case 2:
		return "boxart";
	default:
		return NULL;
	}
}

// A disc of a folder game (/Roms/PS/Game/Game.m3u, or the .cue or a disc beside
// it) as Home's recents and pins store it: the game's art beside the folder,
// where the scraper and the game list put it (/Roms/PS/.media/<variant>/Game.png;
// NULL variant = the root picture). false when the ROM's folder is not a folder
// game (no folder-named .m3u or .cue in it) or that art is missing; out untouched.
static bool folderGameArtPath(const char* rom_path, const char* variant, char* out, size_t out_size) {
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", rom_path);
	char* slash = strrchr(dir, '/');
	if (!slash || slash == dir)
		return false;
	*slash = '\0';
	const char* name = strrchr(dir, '/');
	if (!name)
		return false;
	char game[MAX_PATH];
	if (snprintf(game, sizeof(game), "%s/%s.m3u", dir, name + 1) >= (int)sizeof(game))
		return false;
	if (!exists(game)) {
		if (snprintf(game, sizeof(game), "%s/%s.cue", dir, name + 1) >= (int)sizeof(game) || !exists(game))
			return false;
	}
	char path[MAX_PATH];
	ROM_mediaArtVariantPath(dir, variant, path, sizeof(path));
	if (!ROM_artExists(path, sizeof(path)))
		return false;
	snprintf(out, out_size, "%s", path);
	return true;
}

// Whether the first len chars of path end in .p8.png, any case.
static bool isCartName(const char* path, size_t len) {
	return len >= 7 && strncasecmp(path + len - 7, ".p8.png", 7) == 0;
}

bool ROM_cartLabelPath(const char* rom_path, char* out, size_t out_size) {
	size_t len = strlen(rom_path);
	if (!isCartName(rom_path, len) || len + sizeof(CART_LABEL_MARK) > out_size)
		return false;
	snprintf(out, out_size, "%s" CART_LABEL_MARK, rom_path);
	return true;
}

bool ROM_cartLabelCart(const char* path, char* cart, size_t cart_size) {
	size_t len = strlen(path), mark = sizeof(CART_LABEL_MARK) - 1;
	if (len <= mark || strcmp(path + len - mark, CART_LABEL_MARK) != 0 || !isCartName(path, len - mark) ||
		len - mark >= cart_size)
		return false;
	snprintf(cart, cart_size, "%.*s", (int)(len - mark), path);
	return true;
}

// The last art fallback: a PICO-8 .p8.png cart (any case) that is on disk is its own cartridge picture, so a cart
// with no scraped or hand-made art still shows one. true = out is the ROM path (a box art or mix slot: the whole
// cart) or, with `label`, its label path (a screenshot slot: the picture printed on the cart, ROM_cartLabelPath); a
// miss leaves out untouched.
static bool cartPictureFallback(const char* rom_path, bool label, char* out, size_t out_size) {
	size_t len = strlen(rom_path);
	if (!isCartName(rom_path, len) || len >= out_size || !exists((char*)rom_path))
		return false;
	if (label)
		return ROM_cartLabelPath(rom_path, out, out_size);
	snprintf(out, out_size, "%s", rom_path);
	return true;
}

void ROM_displayArtPath(const char* rom_path, int art_type, bool fallback_to_mix,
						char* out, size_t out_size) {
	const char* variant = artVariantFolder(art_type);
	if (variant) {
		ROM_mediaArtVariantPath(rom_path, variant, out, out_size);
		if (ROM_artExists(out, out_size) || folderGameArtPath(rom_path, variant, out, out_size))
			return;
		// Caller asked for this variant only: leave the missing path in place
		// so nothing is drawn, rather than substituting a different image
		// (a .p8.png cart's own picture aside: that is the game's only art).
		if (!fallback_to_mix) {
			cartPictureFallback(rom_path, art_type == 1, out, out_size); // 1 = ART_TYPE_SCREENSHOT: its label
			return;
		}
	}
	// No variant requested, or the scraper never wrote one for this game
	// (older libraries only have the mix composite).
	ROM_mediaArtPath(rom_path, out, out_size);
	if (ROM_artExists(out, out_size))
		return;
	// The mix asked for but never made (the scraper no longer composes it):
	// the screenshot instead, when there is one.
	if (!variant) {
		char shot[MAX_PATH];
		ROM_mediaArtVariantPath(rom_path, "screenshot", shot, sizeof(shot));
		if (ROM_artExists(shot, sizeof(shot))) {
			snprintf(out, out_size, "%s", shot);
			return;
		}
	}
	cartPictureFallback(rom_path, art_type == 1, out, out_size);
}

bool ROM_findArt(const char* rom_path, char* out, size_t out_size) {
	ROM_mediaArtPath(rom_path, out, out_size);
	if (ROM_artExists(out, out_size))
		return true;
	// multi-disc folder games: art named after the containing folder, one level up
	char dir[MAX_PATH];
	strncpy(dir, rom_path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char* slash = strrchr(dir, '/');
	if (!slash)
		return cartPictureFallback(rom_path, false, out, out_size);
	*slash = '\0';
	char* parent_slash = strrchr(dir, '/');
	if (!parent_slash || parent_slash == dir)
		return cartPictureFallback(rom_path, false, out, out_size);
	*parent_slash = '\0';
	snprintf(out, out_size, "%s/.media/%s.png", dir, parent_slash + 1);
	return ROM_artExists(out, out_size) || cartPictureFallback(rom_path, false, out, out_size);
}

// ROM_findScreenshot's chain without the cart's label (ROM_hasRealArt tells the two apart).
static bool findScreenshotArt(const char* rom_path, char* out, size_t out_size) {
	ROM_mediaArtVariantPath(rom_path, "screenshot", out, out_size);
	if (ROM_artExists(out, out_size))
		return true;
	char mix[MAX_PATH];
	ROM_mediaArtPath(rom_path, mix, sizeof(mix));
	if (ROM_artExists(mix, sizeof(mix))) {
		snprintf(out, out_size, "%s", mix);
		return true;
	}
	// a folder game's disc: the same two, beside the folder (a miss: out stays the screenshot path)
	return folderGameArtPath(rom_path, "screenshot", out, out_size) ||
		   folderGameArtPath(rom_path, NULL, out, out_size);
}

bool ROM_findScreenshot(const char* rom_path, char* out, size_t out_size) {
	return findScreenshotArt(rom_path, out, out_size) || cartPictureFallback(rom_path, true, out, out_size);
}

bool ROM_hasRealArt(const char* rom_path) {
	char p[MAX_PATH];
	if (findScreenshotArt(rom_path, p, sizeof(p)))
		return true;
	ROM_displayArtPath(rom_path, 2, false, p, sizeof(p)); // 2 = ART_TYPE_BOXART (config.h)
	return strcmp(p, rom_path) != 0 && exists(p);
}

// The fitted List art's folder for a GAME_LIST_ART_* value (config.h; int-typed so utils.c stays free of config.h):
// 1 Mix, 2 2D box art, 3 Wheel, 4 3D box art; NULL for the Screenshot (0) or anything else.
static const char* listArtFolder(int art) {
	switch (art) {
	case 1:
		return "mix";
	case 2:
		return "boxart2d";
	case 3:
		return "wheel";
	case 4:
		return "boxart";
	default:
		return NULL;
	}
}

bool ROM_findListArt(const char* rom_path, int art, char* out, size_t out_size) {
	const char* folder = listArtFolder(art);
	if (folder) {
		ROM_mediaArtVariantPath(rom_path, folder, out, out_size);
		if (ROM_artExists(out, out_size) || folderGameArtPath(rom_path, folder, out, out_size))
			return true;
		// Mix: an older library's mix (and a Port's picture) is the root .media/<name>.png
		if (art == 1) {
			ROM_mediaArtPath(rom_path, out, out_size);
			if (ROM_artExists(out, out_size) || folderGameArtPath(rom_path, NULL, out, out_size))
				return true;
		}
	}
	// the screenshot (false), a .p8.png cart's label with none
	ROM_findScreenshot(rom_path, out, out_size);
	return false;
}

bool M3U_findForRom(const char* rom_path, char* m3u_path, size_t m3u_size) {
	char work[MAX_PATH];
	strncpy(work, rom_path, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';
	char* tmp = strrchr(work, '/');
	if (!tmp)
		return false;
	tmp[0] = '\0';
	char* dir_name = strrchr(work, '/');
	if (!dir_name)
		return false;
	dir_name++;
	snprintf(m3u_path, m3u_size, "%s/%s.m3u", work, dir_name);
	return exists(m3u_path);
}

int M3U_forEachDisc(const char* m3u_path, M3U_DiscFn fn, void* ctx) {
	char base_path[MAX_PATH];
	strncpy(base_path, m3u_path, sizeof(base_path) - 1);
	base_path[sizeof(base_path) - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return 0;
	slash[1] = '\0';

	FILE* file = fopen(m3u_path, "r");
	if (!file)
		return 0;
	int count = 0;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue;
		char disc_path[MAX_PATH];
		if (snprintf(disc_path, sizeof(disc_path), "%s%s", base_path, line) >= (int)sizeof(disc_path) ||
			!exists(disc_path))
			continue;
		bool keep = fn(disc_path, count, ctx);
		count++;
		if (!keep)
			break;
	}
	fclose(file);
	return count;
}

static char pico8_root[MAX_PATH] = SDCARD_PATH;
static int pico8_cached = -1; // -1 unknown, 0 absent, 1 found in pico8_dir
static char pico8_dir[MAX_PATH];

void PICO8_resetRuntimeCache(void) {
	pico8_cached = -1;
	pico8_dir[0] = '\0';
}

void PICO8_setRuntimeRoot(const char* sdcard_root) {
	snprintf(pico8_root, sizeof(pico8_root), "%s", sdcard_root);
	PICO8_resetRuntimeCache();
}

bool PICO8_findRuntime(char* dir_out, size_t size) {
	if (pico8_cached < 0) {
		static const char* bios[] = {"P8", "PICO"};
		pico8_cached = 0;
		for (int i = 0; i < 2 && !pico8_cached; i++) {
			char dir[MAX_PATH], bin[MAX_PATH], dat[MAX_PATH];
			// a root too long for the paths: nothing to find there
			if (snprintf(dir, sizeof(dir), "%s/Bios/%s", pico8_root, bios[i]) >= (int)sizeof(dir) ||
				snprintf(bin, sizeof(bin), "%s/pico8_64", dir) >= (int)sizeof(bin) ||
				snprintf(dat, sizeof(dat), "%s/pico8.dat", dir) >= (int)sizeof(dat))
				continue;
			if (exists(bin) && exists(dat)) {
				snprintf(pico8_dir, sizeof(pico8_dir), "%s", dir);
				pico8_cached = 1;
			}
		}
	}
	if (pico8_cached && dir_out && size)
		snprintf(dir_out, size, "%s", pico8_dir);
	return pico8_cached == 1;
}

void ROM_resumeTag(const char* emu_name, char* out, size_t size) {
	if (strcmp(emu_name, "P8") == 0 && PICO8_findRuntime(NULL, 0))
		snprintf(out, size, "P8-native");
	else
		snprintf(out, size, "%s", emu_name);
}
