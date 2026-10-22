// Host-compiled unit test for taskset.c's CPU list / hex mask parsing and
// its argument checks (the paths that return before any affinity call).
// taskset.c is included whole with main renamed. On macOS the glibc
// cpu_set_t API comes from the clangd shim in scripts/clangd/include.
// Build & run (from workspace/all/taskset):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I../../../scripts/clangd/include \
//      tests/test_taskset.c -o /tmp/t && /tmp/t
#define main taskset_main
#include "../taskset.c"
#undef main

#include <assert.h>

#ifdef __APPLE__
// Declared by the shim, never reached by these tests
int sched_setaffinity(int pid, size_t cpusetsize, const cpu_set_t* mask) {
	(void)pid;
	(void)cpusetsize;
	(void)mask;
	errno = ENOSYS;
	return -1;
}
#endif

static int count(const cpu_set_t* set) {
	int n = 0;
	for (int i = 0; i < CPU_SETSIZE; i++)
		if (CPU_ISSET(i, set))
			n++;
	return n;
}

static void test_cpu_list(void) {
	cpu_set_t set;
	assert(parse_cpu_list("0-3", &set) == 0);
	assert(count(&set) == 4 && CPU_ISSET(0, &set) && CPU_ISSET(3, &set));
	assert(parse_cpu_list("1,3", &set) == 0);
	assert(count(&set) == 2 && CPU_ISSET(1, &set) && CPU_ISSET(3, &set));
	assert(parse_cpu_list("4-7,0", &set) == 0);
	assert(count(&set) == 5 && CPU_ISSET(0, &set) && CPU_ISSET(7, &set));
	assert(parse_cpu_list("2-2", &set) == 0 && count(&set) == 1);
	assert(parse_cpu_list("5,", &set) == 0 && count(&set) == 1); // trailing comma

	assert(parse_cpu_list("4-2", &set) == -1); // reversed range
	assert(parse_cpu_list("x", &set) == -1);
	assert(parse_cpu_list("1-", &set) == -1);
	assert(parse_cpu_list("1,,2", &set) == -1);
	assert(parse_cpu_list("3x", &set) == -1); // garbage suffix
	assert(parse_cpu_list("-1", &set) == -1); // wraps to a huge number

	char big[32];
	snprintf(big, sizeof(big), "%d", CPU_SETSIZE);
	assert(parse_cpu_list(big, &set) == -1);
	snprintf(big, sizeof(big), "0-%d", CPU_SETSIZE);
	assert(parse_cpu_list(big, &set) == -1);
	snprintf(big, sizeof(big), "%d", CPU_SETSIZE - 1);
	assert(parse_cpu_list(big, &set) == 0 && CPU_ISSET(CPU_SETSIZE - 1, &set));
}

static void test_hex_mask(void) {
	cpu_set_t set;
	assert(parse_hex_mask("0x30", &set) == 0);
	assert(count(&set) == 2 && CPU_ISSET(4, &set) && CPU_ISSET(5, &set));
	assert(parse_hex_mask("F", &set) == 0 && count(&set) == 4);
	assert(parse_hex_mask("0XfF", &set) == 0 && count(&set) == 8);
	assert(parse_hex_mask("30g", &set) == -1); // garbage suffix
	assert(parse_hex_mask("0x3 ", &set) == -1);
	assert(parse_hex_mask("8000000000000000", &set) == 0);
	assert(count(&set) == 1 && CPU_ISSET(63, &set));
}

static int run(int argc, const char* const* argv) {
	return taskset_main(argc, (char**)argv);
}

static void test_args(void) {
	const char* none[] = {"taskset"};
	assert(run(1, none) == 1);
	const char* bad_opt[] = {"taskset", "-z", "1", "true"};
	assert(run(4, bad_opt) == 1);
	const char* no_mask[] = {"taskset", "-c"};
	assert(run(2, no_mask) == 1);
	const char* bad_list[] = {"taskset", "-c", "4-2", "true"};
	assert(run(4, bad_list) == 1);
	const char* bad_mask[] = {"taskset", "zz", "true"};
	assert(run(3, bad_mask) == 1);
	const char* no_pid[] = {"taskset", "-p", "0x1"};
	assert(run(3, no_pid) == 1);
	const char* no_cmd[] = {"taskset", "0x1"};
	assert(run(2, no_cmd) == 1);
}

int main(void) {
	test_cpu_list();
	test_hex_mask();
	test_args();
	printf("test_taskset: all tests passed\n");
	return 0;
}
