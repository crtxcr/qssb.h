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
	exile_append_syscall_policy(policy,EXILE_SYS(getuid), EXILE_SYSCALL_DENY_KILL_PROCESS, NULL, 0);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);

	uid_t pid = syscall(EXILE_SYS(geteuid));
	pid = syscall(EXILE_SYS(getuid));
	return 0;


}
int test_seccomp_blacklisted()
{
	return test_expected_kill(&do_test_seccomp_blacklisted);
}


static int do_test_seccomp_blacklisted_call_permitted()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYS(getuid),  EXILE_SYSCALL_DENY_KILL_PROCESS, NULL, 0);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);
	//geteuid is not blacklisted, so must succeed
	uid_t pid = syscall(EXILE_SYS(geteuid));
	return 0;
}


int test_seccomp_blacklisted_call_permitted()
{
	return test_successful_exit(&do_test_seccomp_blacklisted_call_permitted);
}

static int do_test_seccomp_x32_kill()
{
	struct exile_policy *policy = exile_init_policy();

	exile_append_syscall_policy(policy, EXILE_SYS(getuid), EXILE_SYSCALL_DENY_KILL_PROCESS, NULL, 0);
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

	exile_append_syscall_policy(policy, EXILE_SYS(getuid), EXILE_SYSCALL_DENY_KILL_PROCESS, NULL, 0);

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

	exile_append_syscall_policy(policy, EXILE_SYS(close),EXILE_SYSCALL_DENY_RET_ERROR,  NULL, 0);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);

	xexile_enable_policy(policy);
	uid_t id = syscall(EXILE_SYS(getuid));

	int fd = syscall(EXILE_SYS(close), 0);
	printf("close() return code: %i, errno: %s\n", fd, strerror(errno));
	return fd == -1 ? 0 : 1;
}



int test_seccomp_errno()
{
	return test_successful_exit(&do_test_seccomp_errno);
}

int test_seccomp_argfilter_allowed()
{
	struct exile_policy *policy = exile_init_policy();

	struct sock_filter argfilter[2] =
	{
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, (offsetof(struct seccomp_data, args[1]))),
		BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, O_WRONLY, 0, EXILE_SYSCALL_EXIT_BPF_NO_MATCH)
	};

	exile_append_syscall_policy(policy, EXILE_SYS(open),EXILE_SYSCALL_DENY_RET_ERROR,  argfilter, 2);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);
	xexile_enable_policy(policy);


	char *t = "/dev/random";
	int ret = (int) syscall(EXILE_SYS(open),t, O_RDONLY);

	if(ret == -1)
	{
		printf("Failed: open was expected to succeed, but returned %i\n", ret);
		return 1;
	}
	return 0;
}

int test_seccomp_argfilter_filtered()
{
	struct exile_policy *policy = exile_init_policy();

	struct sock_filter argfilter[2] =
	{
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, (offsetof(struct seccomp_data, args[1]))),
		BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, O_WRONLY, 0, EXILE_SYSCALL_EXIT_BPF_NO_MATCH)
	};

	exile_append_syscall_policy(policy, EXILE_SYS(open),EXILE_SYSCALL_DENY_RET_ERROR, argfilter, 2);
	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);
	xexile_enable_policy(policy);

	char *t = "/dev/random";
	int ret = (int) syscall(EXILE_SYS(open),t, O_WRONLY);

	if(ret != -1)
	{
		printf("Failed: open was expected to fail, but returned %i\n", ret);
		return 1;
	}
	return 0;
}


int test_seccomp_argfilter_mixed()
{
	struct exile_policy *policy = exile_init_policy();

	struct sock_filter argfilter[2] =
	{
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, (offsetof(struct seccomp_data, args[1]))),
		BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, O_WRONLY, 0, EXILE_SYSCALL_EXIT_BPF_NO_MATCH)
	};

	exile_append_syscall_policy(policy, EXILE_SYS(stat),EXILE_SYSCALL_DENY_RET_ERROR, NULL,0);
	exile_append_syscall_policy(policy, EXILE_SYS(open),EXILE_SYSCALL_DENY_RET_ERROR, argfilter, 2);
	exile_append_syscall_policy(policy, EXILE_SYS(getpid),EXILE_SYSCALL_DENY_RET_ERROR, NULL, 0);

	exile_append_syscall_default_policy(policy, EXILE_SYSCALL_ALLOW);
	xexile_enable_policy(policy);

	struct stat statbuf;
	int s = (int) syscall(EXILE_SYS(stat), "/dev/urandom", &statbuf);
	if(s != -1)
	{
		printf("Failed: stat was expected to fail, but returned %i\n", s);
		return 1;
	}

	pid_t p = (pid_t) syscall(EXILE_SYS(getpid));
	if(p != -1)
	{
		printf("Failed: getpid was expected to fail, but returned %i\n", p);
		return 1;
	}

	char *t = "/dev/random";
	int ret = (int) syscall(EXILE_SYS(open),t, O_WRONLY);
	if(ret != -1)
	{
		printf("Failed: open was expected to fail, but returned %i\n", ret);
		return 1;
	}
	ret = (int) syscall(EXILE_SYS(open), t, O_RDONLY);
	if(ret == -1)
	{
		printf("Failed: open with O_RDONLY was expected to succeed, but returned %i\n", ret);
		return 1;
	}
	return 0;
}


int do_test_seccomp_pledge_socket()
{
	struct exile_policy *policy = exile_init_policy();
	policy->pledge_promises = EXILE_SYSCALL_PLEDGE_STDIO | EXILE_SYSCALL_PLEDGE_INET | EXILE_SYSCALL_PLEDGE_DENY_ERROR;
	xexile_enable_policy(policy);

	int s = socket(AF_INET, SOCK_STREAM, 0);
	if(s == -1)
	{
		printf("Failed: socket was expected to succeed, but returned %i\n", s);
		return 1;
	}
	s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if(s != -1)
	{
		printf("Failed: socket was expected to fail, but returned %i\n", s);
		return 1;
	}
	return 0;
}

int do_test_seccomp_pledge_open()
{
	struct exile_policy *policy = exile_init_policy();
	policy->pledge_promises = EXILE_SYSCALL_PLEDGE_STDIO | EXILE_SYSCALL_PLEDGE_RPATH | EXILE_SYSCALL_PLEDGE_DENY_ERROR;
	xexile_enable_policy(policy);

	int ret = open("/dev/urandom", O_WRONLY  | O_APPEND);
	if(ret != -1)
	{
		printf("Failed: open was expected to fail, but returned %i\n", ret);
		return 1;
	}
	ret = open("/dev/urandom", O_RDWR);
	if(ret != -1)
	{
		printf("Failed: open O_RDWR was expected to fail, but returned %i\n", ret);
		return 1;
	}
	ret = open("/dev/urandom", O_RDONLY);
	if(ret == -1)
	{
		printf("Failed: open was expected to succceed, but returned %i\n", ret);
		return 1;
	}
	return 0;
}

int test_seccomp_pledge()
{
	int ret = test_successful_exit(&do_test_seccomp_pledge_open);
	if(ret != 0)
	{
		printf("Failed: do_test_seccomp_pledge_open()\n");
		return 1;
	}
	ret = test_successful_exit(&do_test_seccomp_pledge_socket);
	if(ret != 0)
	{
		printf("Failed: do_test_seccomp_pledge_socket()\n");
		return 1;
	}
	return 0;
}

int test_seccomp_exile_pledge_multiple()
{

	int ret = exile_pledge(EXILE_SYSCALL_PLEDGE_STDIO | EXILE_SYSCALL_PLEDGE_UNIX | EXILE_SYSCALL_PLEDGE_SECCOMP_INSTALL | EXILE_SYSCALL_PLEDGE_DENY_ERROR);
	if(ret != 0)
	{
		printf("Failed: exile_pledge() call 1 failed\n");
		return 1;
	}
	int s = socket(AF_UNIX, SOCK_STREAM, 0);
	if(s == -1)
	{
		printf("Failed: socket was expected to succeed, but returned %i\n", s);
		return 1;
	}

	/* Let's take away unix sockets, so it should not be possible anymore */
	ret = exile_pledge(EXILE_SYSCALL_PLEDGE_STDIO | EXILE_SYSCALL_PLEDGE_SECCOMP_INSTALL | EXILE_SYSCALL_PLEDGE_DENY_ERROR);
	if(ret != 0)
	{
		printf("Failed: exile_pledge() call 2 failed\n");
		return 1;
	}
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if(s != -1)
	{
		printf("Failed: socket was expected to fail, but returned %i\n", s);
		return 1;
	}

	/* Let's try to regain unix sockets again */
	ret = exile_pledge(EXILE_SYSCALL_PLEDGE_STDIO | EXILE_SYSCALL_PLEDGE_UNIX | EXILE_SYSCALL_PLEDGE_SECCOMP_INSTALL | EXILE_SYSCALL_PLEDGE_DENY_ERROR);
	if(ret != 0)
	{
		printf("Failed: exile_pledge() call 3 failed\n");
		return 1;
	}
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if(s != -1)
	{
		printf("Failed: socket was still expected to fail, but returned %i\n", s);
		return 1;
	}

	return 0;
}


#if HAVE_LANDLOCK == 1
int test_landlock()
{
	if(!exile_landlock_is_available())
	{
		printf("landlock not available, so cannot test\n");
		return 1;
	}
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
	{ "seccomp-argfilter-allowed", &test_seccomp_argfilter_allowed},
	{ "seccomp-argfilter-filtered", &test_seccomp_argfilter_filtered},
	{ "seccomp-argfilter-mixed", &test_seccomp_argfilter_mixed},
	{ "seccomp-pledge", &test_seccomp_pledge},
	{ "seccomp-pledge-exile_pledge-multi", &test_seccomp_exile_pledge_multiple},
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
