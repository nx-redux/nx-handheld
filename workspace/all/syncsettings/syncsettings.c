#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "msettings.h"

// Detect USB DAC by reading /proc/asound/cards (non-blocking)
static int detect_usbdac(void) {
	FILE* f = fopen("/proc/asound/cards", "r");
	if (!f)
		return 0;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "USB-Audio") || strstr(line, "USB Audio")) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

// Detect Bluetooth by reading .asoundrc for bluealsa config
static int detect_bluetooth(void) {
	// Use USERDATA_PATH (where audiomon writes .asoundrc) instead of HOME,
	// because standalone emulators may override HOME to their own config dir.
	const char* userdata = getenv("USERDATA_PATH");
	if (!userdata)
		userdata = getenv("HOME");
	if (!userdata)
		return 0;
	char path[512];
	snprintf(path, sizeof(path), "%s/.asoundrc", userdata);
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	char buf[256];
	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "bluealsa")) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

// The sink audiomon routed to, from the state it publishes in
// /tmp/nx_audio_sink. Returns -1 when the file is absent or unreadable.
static int published_sink(void) {
	FILE* f = fopen("/tmp/nx_audio_sink", "r");
	if (!f)
		return -1;
	char line[128];
	int sink = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "sink=", 5) != 0)
			continue;
		if (strncmp(line + 5, "bluetooth", 9) == 0)
			sink = AUDIO_SINK_BLUETOOTH;
		else if (strncmp(line + 5, "usb", 3) == 0)
			sink = AUDIO_SINK_USBDAC;
		else if (strncmp(line + 5, "default", 7) == 0)
			sink = AUDIO_SINK_DEFAULT;
		break;
	}
	fclose(f);
	return sink;
}

int main(int argc, char* argv[]) {
	InitSettings();

	// Sync audio sink so SetRawVolume() uses the correct mixer path: the route
	// audiomon chose (a USB card it did not route to is not the sink), else
	// detected from the cards and .asoundrc
	int sink = published_sink();
	if (sink >= 0)
		SetAudioSink(sink);
	else if (detect_bluetooth())
		SetAudioSink(AUDIO_SINK_BLUETOOTH);
	else if (detect_usbdac())
		SetAudioSink(AUDIO_SINK_USBDAC);
	else
		SetAudioSink(AUDIO_SINK_DEFAULT);

	sleep(1);
	SetVolume(GetVolume());
	SetBrightness(GetBrightness());
	return 0;
}
