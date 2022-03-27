/*
 *  convertfs/prepindex.c
 *
 *  Copyright (C) 2002 Serguei Tzukanov <tzukanov@narod.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Utility to prepare index (list of raw blocks) of filesystem image.
 *
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "convertfs.h"

static char *selfname;

static int indexfd;
static int imagefd;
static baddr_t metasize;

static struct superblock super;

static void
die (const char *format, const char *filename)
{
	fprintf(stderr, "%s: ", selfname);
	fprintf(stderr, format, filename);
	exit(1);
}

static inline int
ib_exceed (void *end, void *ib)
{
	return (end - ib) >= super.bsize;
}

static void
write_indexblock (void *ib)
{
	assert(write(indexfd, ib, super.bsize) != -1);
	memset(ib, 0, super.bsize + sizeof(struct chunk));
	metasize++;
}

static void
prepare_index (void)
{
	struct indexblock *ib;
	struct chunk	 *chunk = NULL; /* to shut up gcc */
	baddr_t		 offset;
	baddr_t		 offset_prev;
	baddr_t		 *block;
	int		 phys;
	int		 washole;

	ib = (struct indexblock *)malloc(super.bsize + sizeof(struct chunk));
	assert(ib);
	memset(ib, 0, super.bsize + sizeof(struct chunk));

	phys = 0;
	assert(ioctl(imagefd, FIBMAP, &phys) != -1);
	super.block0 = phys;

	block = (baddr_t *)ib->chunk;
	offset_prev = 0;
	washole = 1;
	for (offset = 1; offset < super.psize; offset++) {
		phys = offset;
		assert(ioctl(imagefd, FIBMAP, &phys) != -1);
		if (phys == 0) {
			washole = 1;
			continue;
		}
		if (washole) {
			chunk = (struct chunk *)block;
			chunk->offset = offset;
			chunk->nblocks = 0;
			block = chunk->block;
			washole = 0;
		}
		if (ib_exceed(block, ib)) {
			write_indexblock(ib);
			chunk = ib->chunk;
			chunk->offset = offset;
			chunk->nblocks = 0;
			block = chunk->block;
		}
		*block++ = phys;
		chunk->nblocks++;
		offset_prev = offset;
	}
	write_indexblock(ib);
	free(ib);
	close(imagefd);
}

static void
initialize_stuff (const char *imagename, const char *indexname)
{
	struct stat64 statbuf;

	imagefd = open64(imagename, O_RDONLY);
	if (imagefd < 0)
		die("opening '%s': %m\n", imagename);
	assert(ioctl(imagefd, FIGETBSZ, &super.bsize) != -1);
	assert(fstat64(imagefd, &statbuf) != -1);
	super.psize = statbuf.st_size/super.bsize;

	indexfd = creat64(indexname, S_IWUSR|S_IRUSR);
	if (indexfd < 0)
		die("creating '%s': %m\n", indexname);
	return;
}

static void
finalize_index (void)
{
	int next;

	assert(fsync(indexfd) != -1);
	next = NULL_BLOCK;
	for (--metasize; metasize >= 0; metasize--) {
		assert(lseek(indexfd, metasize*super.bsize, SEEK_SET)
		       != (off_t)-1);
		write(indexfd, &next, sizeof(next));
		next = metasize;
		assert(ioctl(indexfd, FIBMAP, &next) != -1);
	}
	assert(fsync(indexfd) != -1);
	close(indexfd);

	super.signature = SB_SIGNATURE;
	super.state = LOG_CLEAN;
	super.index0 = super.indexcur = next;
}

static void
write_superblock (const char *supername)
{
	int superfd;

	superfd = creat(supername, S_IWUSR|S_IRUSR);
	if (superfd < 0)
		die("creating '%s': %m\n", supername);
	assert(write(superfd, &super, sizeof(super)) != -1);
	assert(fsync(superfd) != -1);
	close(superfd);
}

int
main (int argc, char *argv[])
{
	selfname = argv[0];
	if (argc != 4) {
		fprintf(stderr,
			"Usage: %s IMAGE INDEX SUPERBLOCK\n", selfname);
		return 1;
	}
	initialize_stuff(argv[1], argv[2]);
	prepare_index();
	finalize_index();
	write_superblock(argv[3]);

	return 0;
}
