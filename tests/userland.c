#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include "../module/dmabufexp.h"

#define RUN_TEST(func, fd0, fd1) \
	do { \
		printf("run '" #func "'\n"); \
		if (func(fd0, fd1) < 0) { \
			printf("'" #func "' failed\n"); \
			return EXIT_FAILURE; \
		} \
		printf("'" #func "' passed\n\n"); \
	} while(0)

static const char * const dev0_path = "/dev/dmabufexp-0";
static const char * const dev1_path = "/dev/dmabufexp-1";

static int test_export_and_close(int fd0, int fd1)
{
	int buffd = 0;

	if (ioctl(fd0, DMABUFEXP_IO_EXPORT, &buffd) < 0) {
		printf("failed to export buffer\n");
		return -1;
	}

	printf("got fd %d\n", buffd);

	close(buffd);

	return 0;
}

static int test_export_and_import(int fd0, int fd1)
{
	int buffd = 0;

	printf("getting a buffer from device 0\n");
	if (ioctl(fd0, DMABUFEXP_IO_EXPORT, &buffd) < 0) {
		printf("failed to export buffer from device 0\n");
		return -1;
	}

	printf("importing buffer with fd %d to device 1\n", buffd);
	if (ioctl(fd1, DMABUFEXP_IO_IMPORT, &buffd) < 0) {
		printf("failed to import buffer to device 1\n");
		close(buffd);
		return -1;
	}

	close(buffd);

	return 0;
}

static int test_export_map_write_import(int fd0, int fd1)
{
	int buffd = 0;
	char *data;

	printf("getting a buffer from device 0\n");
	if (ioctl(fd0, DMABUFEXP_IO_EXPORT, &buffd) < 0) {
		printf("failed to export buffer from device 0\n");
		return -1;
	}

	data = mmap(NULL, DMABUFEXP_BUF_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, buffd, 0);
	if (data == (void *)-1) {
		printf("failed to map dmabuf, reason: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	printf("data is: %s\n", data);
	snprintf(data, DMABUFEXP_BUF_SIZE, "I'm evil userspace program");

	munmap (data, DMABUFEXP_BUF_SIZE);

	printf("importing buffer with fd %d to device 1\n", buffd);
	if (ioctl(fd1, DMABUFEXP_IO_IMPORT, &buffd) < 0) {
		printf("failed to import buffer to device 1\n");
		close(buffd);
		return -1;
	}

	close(buffd);
	return 0;
}

static int test_export_dup(int fd0, int fd1)
{
	int buffd = 0;
	int bufdup;

	if (ioctl(fd0, DMABUFEXP_IO_EXPORT, &buffd) < 0) {
		printf("failed to export buffer\n");
		return -1;
	}

	printf("got fd %d\n", buffd);

	bufdup = dup(buffd);

	close(buffd);

	sleep(5);
	close(bufdup);

	return 0;
}

int main(int argc, char *argv[])
{
	int fd0, fd1;
	int index = 1;
	char *data;

	fd0 = open(dev0_path, O_RDWR);
	if (fd0 < 0) {
		printf("failed to open %s\n", dev0_path);
		return EXIT_FAILURE;
	}

	fd1 = open(dev1_path, O_RDWR);
	if (fd1 < 0) {
		printf("failed to open %s\n", dev1_path);
		return EXIT_FAILURE;
	}

	RUN_TEST(test_export_and_close, fd0, fd1);
	RUN_TEST(test_export_and_import, fd0, fd1);
	RUN_TEST(test_export_map_write_import, fd0, fd1);
	RUN_TEST(test_export_dup, fd0, fd1);

	close(fd0);
	close(fd1);

	return 0;
}
