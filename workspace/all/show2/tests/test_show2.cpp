// Host-compiled unit test for show2.cpp: color/int argument parsing and the
// daemon FIFO reader (a command split across two reads is applied once, the
// last line without a newline still counts). show2.cpp is included whole with
// main renamed and its private members opened up; SDL is linked but never
// initialised. Uses the real FIFO path /tmp/show2.fifo, so don't run it while
// a host show2 daemon is up.
// Build & run (from workspace/all/show2; no -fsanitize=address: Homebrew's
// sdl2-compat fails to load SDL3 under ASan and blocks on an error dialog):
//   c++ -std=c++17 -Wall -Werror -g $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include \
//      tests/test_show2.cpp $(sdl2-config --libs) -lSDL2_image -lSDL2_ttf -lpthread -o /tmp/t && /tmp/t
#include <atomic>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define private public
#define main show2_main
#include "../show2.cpp"
#undef main
#undef private

static void test_parse_color() {
	assert(ShowApp::parseColor("0x123456") == 0x123456);
	assert(ShowApp::parseColor("0XABCDEF") == 0xABCDEF);
	assert(ShowApp::parseColor("#ffffff") == 0xFFFFFF);
	assert(ShowApp::parseColor("ff8800") == 0xFF8800);
	assert(ShowApp::parseColor("") == 0);
	assert(ShowApp::parseColor("0x") == 0);
	assert(ShowApp::parseColor("#") == 0);
	assert(ShowApp::parseColor("0xzz") == 0);
	assert(ShowApp::parseColor("0x12 ") == 0);					// trailing garbage
	assert(ShowApp::parseColor("0xFFFFFFFFFFFFFFFFFFFF") == 0); // out of range
}

static void test_parse_int() {
	assert(ShowApp::parseInt("50", 7) == 50);
	assert(ShowApp::parseInt("-1", 7) == -1);
	assert(ShowApp::parseInt("", 7) == 7);
	assert(ShowApp::parseInt("abc", 7) == 7);
	assert(ShowApp::parseInt("12abc", 7) == 7);
	assert(ShowApp::parseInt("99999999999999999999", 7) == 7);
	assert(ShowApp::parseInt("2147483648", 7) == 7); // > INT_MAX
	assert(ShowApp::parseInt("2147483647", 7) == INT_MAX);
}

static void* fifo_thread(void* arg) {
	static_cast<ShowApp*>(arg)->fifoThread();
	return nullptr;
}

struct Snapshot {
	std::string text;
	int progress, text_y, progress_y;
};

static Snapshot snap(ShowApp& app) {
	pthread_mutex_lock(&app.mutex);
	Snapshot s = {app.current_text, app.current_progress, app.current_text_y_pct, app.current_progress_y_pct};
	pthread_mutex_unlock(&app.mutex);
	return s;
}

static void put(int fd, const char* s) {
	ssize_t n = write(fd, s, strlen(s));
	assert(n == (ssize_t)strlen(s));
	usleep(250 * 1000); // the reader polls every 100 ms
}

static void test_fifo_lines() {
	Config cfg;
	cfg.mode = DisplayMode::Daemon;
	cfg.text = "start";
	ShowApp app(cfg);

	unlink(FIFO_PATH);
	assert(mkfifo(FIFO_PATH, 0666) == 0);
	pthread_t thread;
	pthread_create(&thread, nullptr, fifo_thread, &app);
	int fd = open(FIFO_PATH, O_WRONLY); // waits for the reader
	assert(fd >= 0);

	put(fd, "TEXT:hel");
	assert(snap(app).text == "start"); // half a command: not applied yet
	put(fd, "lo world\nTEXTY:4");
	assert(snap(app).text == "hello world" && snap(app).text_y == 80);
	put(fd, "2\nPROGRESS:abc\nPROGRESSY:95\nUNKNOWN:1\n");
	Snapshot s = snap(app);
	assert(s.text_y == 42 && s.progress == 0 && s.progress_y == 95);
	put(fd, "TEXT:a\nTEXT:b\nPROGRESS:7"); // last line has no newline
	assert(snap(app).text == "b" && snap(app).progress == 0);
#ifdef __linux__
	close(fd);
	usleep(300 * 1000);
	assert(snap(app).progress == 7); // applied when the writer closes

	// a new writer connection works the same
	fd = open(FIFO_PATH, O_WRONLY);
	assert(fd >= 0);
	put(fd, "TEXT:again\n");
	assert(snap(app).text == "again");
#else
	// macOS poll() never reports a FIFO writer's close (no POLLHUP), so the
	// reader stays on this connection: end the line instead
	put(fd, "\n");
	assert(snap(app).progress == 7);
#endif
	put(fd, "QUIT\n");
	close(fd);
	pthread_join(thread, nullptr);
	assert(!app.running);
	unlink(FIFO_PATH);
}

int main() {
	signal(SIGPIPE, SIG_IGN);
	test_parse_color();
	test_parse_int();
	test_fifo_lines();
	std::cout << "test_show2: all tests passed" << std::endl;
	return 0;
}
