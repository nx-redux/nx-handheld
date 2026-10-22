#include "proc_spawn.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t spawn_quiet(char* const argv[], int stdin_fd, bool new_session) {
	pid_t pid = fork();
	if (pid != 0)
		return pid; // parent (or -1: fork failed)

	// Child: only async-signal-safe calls until exec.
	if (new_session)
		setsid();
	if (stdin_fd >= 0 && stdin_fd != STDIN_FILENO) {
		dup2(stdin_fd, STDIN_FILENO);
		close(stdin_fd);
	}
	int null_fd = open("/dev/null", O_RDWR);
	if (null_fd >= 0) {
		if (stdin_fd < 0)
			dup2(null_fd, STDIN_FILENO);
		dup2(null_fd, STDOUT_FILENO);
		dup2(null_fd, STDERR_FILENO);
		if (null_fd > STDERR_FILENO)
			close(null_fd);
	}
	execv(argv[0], argv);
	_exit(127);
}

bool spawn_quiet_wait(char* const argv[], bool new_session) {
	pid_t pid = spawn_quiet(argv, -1, new_session);
	if (pid < 0)
		return false;
	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
