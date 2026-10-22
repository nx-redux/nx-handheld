// Host test for NET_sendAll / NET_recvAll (network_common.c): bounded,
// progress-timed socket I/O that netplay's packet and savestate paths use.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../network_common.h"

static long ms_since(struct timeval* t0) {
	struct timeval t1;
	gettimeofday(&t1, NULL);
	return (t1.tv_sec - t0->tv_sec) * 1000 + (t1.tv_usec - t0->tv_usec) / 1000;
}

// A connected TCP pair over loopback, like the real link (macOS AF_UNIX
// socketpairs ignore MSG_DONTWAIT on a big send).
static void tcp_pair(int sv[2]) {
	int l = socket(AF_INET, SOCK_STREAM, 0);
	assert(l >= 0);
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(l, (struct sockaddr*)&a, sizeof(a)) == 0 && listen(l, 1) == 0);
	socklen_t len = sizeof(a);
	assert(getsockname(l, (struct sockaddr*)&a, &len) == 0);
	sv[0] = socket(AF_INET, SOCK_STREAM, 0);
	assert(connect(sv[0], (struct sockaddr*)&a, sizeof(a)) == 0);
	sv[1] = accept(l, NULL, NULL);
	assert(sv[1] >= 0);
	close(l);
}

int main(void) {
	int sv[2];
	char buf[64];
	size_t got = 99;

	// whole packet
	tcp_pair(sv);
	assert(NET_sendAll(sv[0], "abcdefg", 7, 100) == 1);
	assert(NET_recvAll(sv[1], buf, 7, 100, &got) == 1 && got == 7 && !memcmp(buf, "abcdefg", 7));

	// nothing arrives: timeout with nothing read (an idle link, not an error)
	struct timeval t0;
	gettimeofday(&t0, NULL);
	assert(NET_recvAll(sv[1], buf, 7, 50, &got) == 0 && got == 0);
	assert(ms_since(&t0) >= 40);

	// cut-off packet (3 of 7 header bytes, then silence): timeout, got = 3
	assert(NET_sendAll(sv[0], "abc", 3, 100) == 1);
	assert(NET_recvAll(sv[1], buf, 7, 50, &got) == 0 && got == 3);

	// peer closes mid-packet: -1
	assert(NET_sendAll(sv[0], "xy", 2, 100) == 1);
	close(sv[0]);
	assert(NET_recvAll(sv[1], buf, 7, 1000, &got) == -1 && got == 2);
	close(sv[1]);

	// a reader that never reads: the send gives up after the timeout.
	// O_NONBLOCK only for the host run: macOS blocks a big send despite
	// MSG_DONTWAIT, Linux (the device) honours the flag on a blocking socket.
	tcp_pair(sv);
	fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL) | O_NONBLOCK);
	static char big[4 << 20];
	gettimeofday(&t0, NULL);
	assert(NET_sendAll(sv[0], big, sizeof(big), 100) == 0);
	long took = ms_since(&t0);
	assert(took >= 90 && took < 2000);
	close(sv[0]);
	close(sv[1]);

	assert(NET_recvAll(-1, buf, 1, 10, &got) == -1);
	printf("test_net_io: all assertions passed\n");
	return 0;
}
