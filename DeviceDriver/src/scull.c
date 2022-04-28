#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"

/* Quantum command line option */
static int g_quantum;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Qeuery quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
		   "  h          Print this message\n",
	       cmd);
}

typedef int cmd_i;

static cmd_i parse_arguments(int argc, const char **argv)
{
	cmd_i cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
				fprintf(stderr, "%s: Missing quantum\n", argv[0]);
				cmd = -1;
				break;
			}
			g_quantum = atoi(argv[2]);
	case 'R':
	case 'G':
	case 'Q':
	case 'p':
	case 'i':
	case 't':
	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}


void *ioctlThreadCall(void* fd) {
	struct task_info taskinfo;
	ioctl(*((int *)fd), SCULL_IOCIQUANTUM, &taskinfo);
	printf("state %ld, stack %p, cpu %d, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", taskinfo.state, taskinfo.stack, taskinfo.cpu, taskinfo.prio, taskinfo.static_prio, taskinfo.normal_prio, taskinfo.rt_priority, taskinfo.pid, taskinfo.tgid, taskinfo.nvcsw, taskinfo.nivcsw);
	ioctl(*((int *)fd), SCULL_IOCIQUANTUM, &taskinfo);
	printf("state %ld, stack %p, cpu %d, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", taskinfo.state, taskinfo.stack, taskinfo.cpu, taskinfo.prio, taskinfo.static_prio, taskinfo.normal_prio, taskinfo.rt_priority, taskinfo.pid, taskinfo.tgid, taskinfo.nvcsw, taskinfo.nivcsw);
	pthread_exit(NULL);
}

static int do_op(int fd, cmd_i cmd)
{
	int ret, q;
	struct task_info taskinfo;

	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	case 'p':
		g_quantum = 4;
		for(int i = 0; i < g_quantum; i++) {
			if(fork() == 0) {
				ret = ioctl(fd, SCULL_IOCIQUANTUM, &taskinfo);
				printf("state %ld, stack %p, cpu %d, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", taskinfo.state, taskinfo.stack, taskinfo.cpu, taskinfo.prio, taskinfo.static_prio, taskinfo.normal_prio, taskinfo.rt_priority, taskinfo.pid, taskinfo.tgid, taskinfo.nvcsw, taskinfo.nivcsw);
				ret = ioctl(fd, SCULL_IOCIQUANTUM, &taskinfo);
				printf("state %ld, stack %p, cpu %d, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", taskinfo.state, taskinfo.stack, taskinfo.cpu, taskinfo.prio, taskinfo.static_prio, taskinfo.normal_prio, taskinfo.rt_priority, taskinfo.pid, taskinfo.tgid, taskinfo.nvcsw, taskinfo.nivcsw);
				exit(ret);
			}	
		}
		for(int i = 0; i < g_quantum; i++) {
			int current;
			wait(&current);
			if(current != 0)
				ret = -1;
		}
		break;
	case 'i':
	ret = ioctl(fd, SCULL_IOCIQUANTUM, &taskinfo);
		if(ret == 0) {
			printf("state %ld, stack %p, cpu %d, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", taskinfo.state, taskinfo.stack, taskinfo.cpu, taskinfo.prio, taskinfo.static_prio, taskinfo.normal_prio, taskinfo.rt_priority, taskinfo.pid, taskinfo.tgid, taskinfo.nvcsw, taskinfo.nivcsw);
		}
		exit(ret);
		break;

	case 't':
		g_quantum = 4;
		pthread_t* threads = (pthread_t*) malloc(sizeof(pthread_t) * g_quantum);
		if(!threads) {
			fprintf(stderr, "ERROR: Something went wrong with malloc\n");
			ret = 0;
			break;
		}
		for(int i = 0; i < g_quantum; i++) {
			pthread_create(&threads[i], NULL, &ioctlThreadCall, (void *) &fd);
		}
		for(int i = 0; i < g_quantum; i++) {
			pthread_join(threads[i], NULL);
		}
		ret = 0;
		free(threads);
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_i cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
