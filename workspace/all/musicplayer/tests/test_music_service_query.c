// Host test for MusicService_isPlaying (the launcher's one-shot "is music
// playing" query): a forked fake owner answers one SNAPSHOT per state.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "music_service_client.h"

static int failures;
static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static int listen_on(const char* path) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
	unlink(path);
	if (fd < 0 || bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0 || listen(fd, 1) != 0)
		return -1;
	return fd;
}

// Fork an owner that answers one SNAPSHOT request with this state, then ask.
static int query_with_state(int listen_fd, int state) {
	pid_t owner = fork();
	if (owner == 0) {
		int client = accept(listen_fd, NULL, NULL);
		MusicFrameHeader header;
		if (client < 0 || recv(client, &header, sizeof(header), MSG_WAITALL) != (ssize_t)sizeof(header))
			_exit(1);
		MusicResponseWire response;
		memset(&response, 0, sizeof(response));
		response.status = MUSIC_STATUS_OK;
		response.snapshot.loaded = 1;
		response.snapshot.state = state;
		header.payload_length = sizeof(response);
		send(client, &header, sizeof(header), 0);
		send(client, &response, sizeof(response), 0);
		close(client);
		_exit(0);
	}
	int playing = MusicService_isPlaying();
	waitpid(owner, NULL, 0);
	return playing;
}

int main(void) {
	char socket_path[96];
	snprintf(socket_path, sizeof(socket_path), "/tmp/nx_music_query_test_%ld.sock", (long)getpid());
	setenv(MUSIC_SERVICE_SOCKET_ENV, socket_path, 1);

	unlink(socket_path);
	check(!MusicService_isPlaying(), "no owner: nothing plays");

	int listen_fd = listen_on(socket_path);
	check(listen_fd >= 0, "fake owner listens");
	if (listen_fd >= 0) {
		check(query_with_state(listen_fd, MUSIC_STATE_PLAYING), "playing owner reports playing");
		check(!query_with_state(listen_fd, MUSIC_STATE_PAUSED), "paused owner is not playing");
		check(!query_with_state(listen_fd, MUSIC_STATE_STOPPED), "stopped owner is not playing");
		close(listen_fd);
	}
	unlink(socket_path);

	if (!failures)
		printf("test_music_service_query: all passed\n");
	return failures ? 1 : 0;
}
