#ifndef PROC_SPAWN_H
#define PROC_SPAWN_H

#include <stdbool.h>
#include <sys/types.h>

// Fork and exec argv[0] (an absolute path; no shell, no PATH search) with
// argv as its arguments. The child's stdout and stderr go to /dev/null, and
// its stdin is `stdin_fd` (closed in the child after the dup) or /dev/null
// when stdin_fd < 0. `new_session` detaches the child with setsid() first.
// Descriptors are otherwise inherited, so mark any the child must not keep
// (a pipe's write end) FD_CLOEXEC. An exec failure exits the child with 127.
// Returns the child's pid, or -1 when fork fails; waiting (and killing) is
// left to the caller.
pid_t spawn_quiet(char* const argv[], int stdin_fd, bool new_session);

// spawn_quiet with stdin on /dev/null, then wait: true when the child exited 0.
bool spawn_quiet_wait(char* const argv[], bool new_session);

#endif // PROC_SPAWN_H
