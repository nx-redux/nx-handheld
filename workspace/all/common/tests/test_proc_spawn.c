// Host test for proc_spawn.c (spawn_quiet / spawn_quiet_wait).
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../proc_spawn.h"

static int exit_code(pid_t pid) {
	int status = 0;
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status));
	return WEXITSTATUS(status);
}

int main(void) {
	// Exit status passes through; no shell is involved in argv[0].
	char* ok[] = {"/bin/sh", "-c", "exit 0", NULL};
	char* fail[] = {"/bin/sh", "-c", "exit 3", NULL};
	assert(spawn_quiet_wait(ok, false));
	assert(!spawn_quiet_wait(fail, true));
	assert(exit_code(spawn_quiet(fail, -1, false)) == 3);

	// A missing binary: the child exits 127.
	char* missing[] = {"/nonexistent/nx_spawn_test", NULL};
	assert(exit_code(spawn_quiet(missing, -1, false)) == 127);
	assert(!spawn_quiet_wait(missing, false));

	// stdin from /dev/null: read hits EOF at once.
	char* eof[] = {"/bin/sh", "-c", "if read x; then exit 1; fi; exit 0", NULL};
	assert(spawn_quiet_wait(eof, false));

	// stdin from a pipe (write end FD_CLOEXEC so the child sees EOF after it).
	int fds[2];
	assert(pipe(fds) == 0);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	char* cat_check[] = {"/bin/sh", "-c", "read x; [ \"$x\" = hello ] || exit 1; if read y; then exit 2; fi; exit 0",
						 NULL};
	pid_t pid = spawn_quiet(cat_check, fds[0], false);
	assert(pid > 0);
	close(fds[0]);
	assert(write(fds[1], "hello\n", 6) == 6);
	close(fds[1]);
	assert(exit_code(pid) == 0);

	// stdout/stderr go to /dev/null: a child writing to them still succeeds,
	// and nothing reaches our (redirected) stdout.
	fflush(stdout);
	int saved = dup(STDOUT_FILENO);
	int out_pipe[2];
	assert(pipe(out_pipe) == 0);
	dup2(out_pipe[1], STDOUT_FILENO);
	close(out_pipe[1]);
	char* noisy[] = {"/bin/sh", "-c", "echo out; echo err >&2", NULL};
	assert(spawn_quiet_wait(noisy, false));
	dup2(saved, STDOUT_FILENO);
	close(saved);
	char buf[8];
	fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
	assert(read(out_pipe[0], buf, sizeof(buf)) <= 0);
	close(out_pipe[0]);

	// new_session: the child leads its own session (so its own process group).
	char* leader[] = {"/bin/sh", "-c", "[ \"$(ps -o pgid= -p $$ | tr -d ' ')\" = \"$$\" ]", NULL};
	assert(spawn_quiet_wait(leader, true));
	assert(!spawn_quiet_wait(leader, false));

	printf("test_proc_spawn: OK\n");
	return 0;
}
