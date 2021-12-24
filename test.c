#include "exile.h"
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

int xexile_enable_policy(struct exile_policy *policy)
{
	int ret = exile_enable_policy(policy);
	if(ret != 0)
	{
		fprintf(stderr, "exile_enable_policy() failed: %i\n", ret);
		exit(EXIT_FAILURE);
	}
	return 0;
}

int test_default_main()
{
	struct exile_policy *policy = exile_init_policy();
	return xexile_enable_policy(policy);
}

static int test_expected_kill(int (*f)())
{
	pid_t pid = fork();
	if(pid == 0)
	{
		return f();
	}
	int status = 0;
	waitpid(pid, &status, 0);

	if(WIFSIGNALED(status))
	{
		int c = WTERMSIG(status);
		if(c == SIGSYS)
		{
			printf("Got expected signal\n");
			return 0;
		}
		printf("Unexpected status code: %i\n", c);
		return 1;
	}
	else
	{
		int c = WEXITSTATUS(status);
		printf("Process was not killed, test fails. Status code of exit: %i\n", c);
		return 1;
	}
	return 0;
}


static int test_successful_exit(int (*f)())
{
	pid_t pid = fork();
	if(pid == 0)
	{
		return f();
	}
	int status = 0;
	waitpid(pid, &status, 0);

	if(WIFSIGNALED(status))
	{
		int c = WTERMSIG(status);
		printf("Received signal, which was not expected. Signal was: %i\n", c);
		return 1;
	}
	else
	{
		int c = WEXITSTATUS(status);
		if(c != 0)
		{
			printf("Process failed to exit properly. Status code is: %i\n", c);
		}
		return c;
	}
	printf("Process exited sucessfully as expected");
	return 0;
}


static int do_test_seccomp_blacklisted()
{
	struct exile_policy *policy = exile_init_policy();
	exile_append_syscall_policy(policy, EXILE_SYSCALL_DENY_KILL_PROCESS, EXILE_SYS(getuid));
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);

	uid_t pid = geteuid();
	pid = getuid();
	return 0;


}
int test_seccomp_blacklisted()
{
	return test_expected_kill(&do_test_seccomp_blacklisted);
}


static int do_test_seccomp_blacklisted_call_permitted()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYSCALL_DENY_KILL_PROCESS, EXILE_SYS(getuid));
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);
	//geteuid is not blacklisted, so must succeed
	uid_t pid = geteuid();
	return 0;
}


int test_seccomp_blacklisted_call_permitted()
{
	return test_successful_exit(&do_test_seccomp_blacklisted_call_permitted);
}

static int do_test_seccomp_x32_kill()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYSCALL_DENY_KILL_PROCESS, EXILE_SYS(getuid));
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);

	/* Attempt to bypass by falling back to x32 should be blocked */
	syscall(EXILE_SYS(getuid)+__X32_SYSCALL_BIT);

	return 0;
}

int test_seccomp_x32_kill()
{
	return test_expected_kill(&do_test_seccomp_x32_kill);
}

/* Tests whether seccomp rules end with a policy matching all syscalls */
int test_seccomp_require_last_matchall()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYSCALL_DENY_KILL_PROCESS, EXILE_SYS(getuid));

	int status = exile_enable_policy(policy);
	if(status == 0)
	{
		printf("Failed. Should not have been enabled!");
		return 1;
	}
	return 0;
}

static int do_test_seccomp_errno()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYSCALL_DENY_RET_ERROR, EXILE_SYS(close));
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);
	uid_t id = getuid();

	int fd = close(0);
	printf("close() return code: %i, errno: %s\n", fd, strerror(errno));
	return fd == -1 ? 0 : 1;
}



int test_seccomp_errno()
{
	return test_successful_exit(&do_test_seccomp_errno);
}

static int test_seccomp_group()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_group_syscall_policy(policy, EXILE_SYSCALL_DENY_RET_ERROR, EXILE_SYSCGROUP_SOCKET);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);

	int s = socket(AF_INET,SOCK_STREAM,0);
	if(s != -1)
	{
		printf("Failed: socket was expected to return error\n");
		return 1;
	}
	return 0;
}

#if HAVE_LANDLOCK == 1
int test_landlock()
{
	struct exile_policy *policy = exile_init_policy();
	exile_append_path_policy(policy, EXILE_FS_ALLOW_ALL_READ, "/proc/self/fd");
	xexile_enable_policy(policy);

	int fd = open("/", O_RDONLY | O_CLOEXEC);
	if(fd < 0)
	{
		return 0;
	}
	return 1;
}

int test_landlock_deny_write()
{
	struct exile_policy *policy = exile_init_policy();
	exile_append_path_policy(policy, EXILE_FS_ALLOW_ALL_READ, "/tmp/");
	xexile_enable_policy(policy);

	int fd = open("/tmp/a", O_WRONLY | O_CLOEXEC);
	if(fd < 0)
	{
		return 0;
	}
	return 1;
}
#else
int test_landlock()
{
	return 2;
}

int test_landlock_deny_write()
{
	return 2;
}
#endif

int test_nofs()
{
	struct exile_policy *policy = exile_init_policy();
	policy->no_fs = 1;
	xexile_enable_policy(policy);

	int s = socket(AF_INET,SOCK_STREAM,0);
	if(s == -1)
	{
		fprintf(stderr, "Failed to open socket but this was not requested by policy\n");
		return 1;
	}

	/* Expect seccomp to take care of this */
	if(open("/test", O_CREAT | O_WRONLY) >= 0)
	{
		fprintf(stderr, "Failed: We do not expect write access\n");
		return 1;
	}

	return 0;
}


int test_no_new_fds()
{
	struct exile_policy *policy = exile_init_policy();
	policy->no_new_fds = 1;
	xexile_enable_policy(policy);

	if(open("/tmp/test", O_CREAT | O_WRONLY) >= 0)
	{
		fprintf(stderr, "Failed: Could open new file descriptor\n");
		return -1;
	}

	int s = socket(AF_INET,SOCK_STREAM,0);
	if(s >= 0)
	{
		fprintf(stderr, "Failed: socket got opened but policy denied\n");
		return -1;
	}

	return 0;

}

struct dispatcher
{
	char *name;
	int (*f)();
};

struct dispatcher dispatchers[] = {
	{ "default", &test_default_main },
	{ "seccomp-blacklisted", &test_seccomp_blacklisted},
	{ "seccomp-blacklisted-permitted", &test_seccomp_blacklisted_call_permitted},
	{ "seccomp-x32-kill", &test_seccomp_x32_kill},
	{ "seccomp-require-last-matchall", &test_seccomp_require_last_matchall},
	{ "seccomp-errno", &test_seccomp_errno},
	{ "seccomp-group", &test_seccomp_group},
	{ "landlock", &test_landlock},
	{ "landlock-deny-write", &test_landlock_deny_write },
	{ "no_fs", &test_nofs},
	{ "no_new_fds", &test_no_new_fds}
};

int main(int argc, char *argv[])
{
	if(argc < 2)
	{
		fprintf(stderr, "Usage: %s [testname]\n", argv[0]);
		return EXIT_FAILURE;
	}
	char *test = argv[1];
	if(strcmp(test, "--dumptests") == 0)
	{
		for(unsigned int i = 0; i < sizeof(dispatchers)/sizeof(dispatchers[0]); i++)
		{
			printf("%s\n", dispatchers[i].name);
		}
		return EXIT_SUCCESS;
	}

	for(unsigned int i = 0; i < sizeof(dispatchers)/sizeof(dispatchers[0]); i++)
	{
		struct dispatcher *current = &dispatchers[i];
		if(strcmp(current->name, test) == 0)
		{
			return current->f();
		}
	}
	fprintf(stderr, "Unknown test\n");
	return EXIT_FAILURE;
}
