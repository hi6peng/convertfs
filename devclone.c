/*
 *  convertfs/devclone.c
 *
 *  Copyright (C) 2002 Serguei Tzukanov <tzukanov@narod.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Utility to make clone of the block device (sparse file of the same size).
 *
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

static char *selfname;

static void
die (const char *format, const char *filename)
{
	fprintf(stderr, "%s: ", selfname);
	fprintf(stderr, format, filename);
	exit(1);
}

int
main (int argc, char *argv[])
{
	long devsize;
	int  sectsize = 512; /* FIXME */
	int  fd;
	int  bsize;

	selfname = argv[0];
	if (argc != 3) {
		printf("Usage: %s DEVICE IMAGE\n", selfname);
		return 1;
	}
	fd = open64(argv[1], O_RDONLY);
	if ((fd < 0) || (ioctl(fd, BLKGETSIZE, &devsize) < 0))
		die("opening '%s': %m\n", argv[1]);
	close(fd);

	fd = creat64(argv[2], S_IWUSR|S_IRUSR);
	if (fd < 0)
		die("creating '%s': %m\n", argv[2]);
	assert(ioctl(fd, FIGETBSZ, &bsize) != -1);
	assert(ftruncate64(fd, (off64_t)devsize*sectsize & ~(bsize - 1)) != -1);
	close(fd);

	return 0;
}
