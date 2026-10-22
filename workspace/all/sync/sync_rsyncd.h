// rsync's daemon, shared by Device Sync (sync.c) and the netplay wizard's save
// sharing (netplay-wizard/wizard_sync.c). Each passes its own config and
// pidfile, so the two never touch each other's files.
#ifndef SYNC_RSYNCD_H
#define SYNC_RSYNCD_H

// <SHARED_BIN_PATH>/rsync, the one rsync both use (client and daemon).
const char* sync_rsyncd_bin(void);

// Runs `rsync --daemon --config=<conf_path>` and gives it half a second to
// write pid_path (rsync writes it only once it has bound its port). 0 when it
// did; -1 when the daemon didn't start or didn't bind.
int sync_rsyncd_start(const char* conf_path, const char* pid_path);

// Stops the daemon named in pid_path, then any rsync left (one that died
// before writing its pidfile leaves no other handle), and removes pid_path and
// conf_path. Idempotent.
void sync_rsyncd_stop(const char* pid_path, const char* conf_path);

#endif
