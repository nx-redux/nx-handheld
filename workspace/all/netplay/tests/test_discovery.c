// Host test for NET_receiveDiscoveryResponses (network_common.c) over loopback UDP: every field
// comes from whoever sends to the discovery port, so the parse must hold against bad datagrams.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../network_common.h"

#define MAGIC 0x4E58575Au

static struct sockaddr_in to;

static void send_raw(int fd, const void* buf, size_t len) {
	assert(sendto(fd, buf, len, 0, (struct sockaddr*)&to, sizeof(to)) == (ssize_t)len);
}

static void send_pkt(int fd, uint32_t magic, const char* name, uint16_t port) {
	NET_DiscoveryPacket p;
	memset(&p, 0, sizeof(p));
	p.magic = htonl(magic);
	p.protocol_version = htonl(1);
	p.game_crc = htonl(0xCAFEF00Du);
	p.port = htons(port);
	snprintf(p.game_name, sizeof(p.game_name), "%s", name);
	snprintf(p.link_mode, sizeof(p.link_mode), "wizard");
	send_raw(fd, &p, sizeof(p));
}

// Loopback delivery is immediate, but give the stack a moment anyway.
static void settle(void) {
	usleep(20 * 1000);
}

int main(void) {
	int rx = socket(AF_INET, SOCK_DGRAM, 0);
	assert(rx >= 0);
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(rx, (struct sockaddr*)&to, sizeof(to)) == 0);
	socklen_t len = sizeof(to);
	assert(getsockname(rx, (struct sockaddr*)&to, &len) == 0);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);
	assert(tx >= 0);

	NET_HostInfo hosts[NET_MAX_DISCOVERED_HOSTS];
	memset(hosts, 0, sizeof(hosts));
	int count = 0;

	// nothing queued; bad arguments
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 0);
	assert(NET_receiveDiscoveryResponses(-1, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 0);
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, NULL, &count, NET_MAX_DISCOVERED_HOSTS) == 0);

	// wrong magic is ignored; the right one after it is taken
	send_pkt(tx, 0x12345678u, "Wrong", 1);
	send_pkt(tx, MAGIC, "Tetris (World)", 55440);
	settle();
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 1);
	assert(!strcmp(hosts[0].game_name, "Tetris (World)"));
	assert(!strcmp(hosts[0].host_ip, "127.0.0.1"));
	assert(hosts[0].port == 55440 && hosts[0].game_crc == 0xCAFEF00Du);
	assert(!strcmp(hosts[0].link_mode, "wizard"));

	// a second packet from the same IP is a duplicate (the first one's fields stay)
	send_pkt(tx, MAGIC, "Other", 1);
	settle();
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 1);
	assert(!strcmp(hosts[0].game_name, "Tetris (World)") && hosts[0].port == 55440);

	// a short datagram is ignored (it ends this poll; the next poll takes what follows)
	send_raw(tx, "NXWZ", 4);
	settle();
	count = 0;
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 0);
	send_pkt(tx, MAGIC, "After short", 2);
	settle();
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 1);
	assert(!strcmp(hosts[0].game_name, "After short"));

	// unterminated 64-byte game_name / 32-byte link_mode come back NUL-terminated
	NET_DiscoveryPacket p;
	memset(&p, 'A', sizeof(p));
	p.magic = htonl(MAGIC);
	p.port = htons(7);
	count = 0;
	send_raw(tx, &p, sizeof(p));
	settle();
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 1);
	assert(strlen(hosts[0].game_name) == NET_MAX_GAME_NAME - 1);
	assert(strlen(hosts[0].link_mode) == NET_MAX_LINK_MODE - 1);
	assert(hosts[0].port == 7);

	// max_hosts is honoured: a full list takes nothing new
	memset(hosts, 0, sizeof(hosts));
	snprintf(hosts[0].host_ip, sizeof(hosts[0].host_ip), "10.0.0.9");
	count = 1;
	send_pkt(tx, MAGIC, "No room", 3);
	settle();
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, 1) == 1);
	assert(!strcmp(hosts[0].host_ip, "10.0.0.9") && hosts[1].host_ip[0] == '\0');

	// a longer datagram is read as its first packet's worth (recvfrom truncates): still bounded
	char big[sizeof(NET_DiscoveryPacket) + 8];
	memset(big, 0, sizeof(big));
	*(uint32_t*)big = htonl(MAGIC);
	send_raw(tx, big, sizeof(big));
	settle();
	count = 0;
	memset(hosts, 0, sizeof(hosts));
	assert(NET_receiveDiscoveryResponses(rx, MAGIC, hosts, &count, NET_MAX_DISCOVERED_HOSTS) == 1);
	assert(hosts[0].game_name[0] == '\0' && hosts[0].link_mode[0] == '\0');

	close(tx);
	close(rx);
	printf("test_discovery: all tests passed\n");
	return 0;
}
