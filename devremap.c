/*
 *  convertfs/devremap.c
 *
 *  Copyright (C) 2002 Serguei Tzukanov <tzukanov@narod.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Core of the toolset - block relocation utility.
 *
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "convertfs.h"

#define offsetof(type, member)		((int)&(((type *)0)->member))

struct indexpage {
	struct indexpage *prev;
	struct indexpage *next;
	baddr_t		 new_phys;
	baddr_t		 phys;
	struct chunk	 chunk[0];
};

struct bmove {
	baddr_t		 from;
	baddr_t		 to;
	int		 i;
	int		 ib_offset;
	struct chunk	 *chunk;
	struct indexpage *ip;
};

struct moverec {
	baddr_t from;
	baddr_t to;
};

struct fixrec {
	baddr_t	block;
	int	offset;
	baddr_t	fix;
};

struct info {
	int		 fd;

	void		 *bmove;

	struct indexpage *ip;

	struct indexpage *index;
	uint8_t		 *physmap;
	uint8_t		 *reserved;
	uint8_t		 *reserved2;

	int		 ipsize;
	baddr_t		 mapsize;
	baddr_t		 balloc_cur;

	struct moverec	 *moverec0;
	struct moverec	 *moverec1;
	struct moverec	 *moverec2;
	struct fixrec	 *fixrec;
};


static char *selfname;

static struct superblock super;
static struct info	 info;


/* ULTRA UGLY HACK */
struct indexpage *hack_ip;
struct chunk     *hack_chunk = NULL;
baddr_t          *hack_physp;
baddr_t          hack_virt;
int              hack_i;


static void
die (const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", selfname);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	exit(1);
}

static inline void
bseek (baddr_t block)
{
	assert(lseek64(info.fd, (off64_t)block*super.bsize, SEEK_SET)
	       != (off64_t)-1);
}

static void
bmove (baddr_t src, baddr_t dst)
{
	bseek(src);
	assert(read(info.fd, info.bmove, super.bsize) != -1);
	bseek(dst);
	assert(write(info.fd, info.bmove, super.bsize) != -1);
}

static inline void
bfix (baddr_t block, int offset, baddr_t fix)
{
	assert(lseek64(info.fd, (off64_t)block*super.bsize + offset,
	               SEEK_SET)
	       != (off64_t)-1);
	assert(write(info.fd, &fix, sizeof(fix)) != -1);
}

static void
load_superblock (void)
{
	bseek(0);
	assert(read(info.fd, &super, sizeof(super)) != -1);
	if (super.signature != SB_SIGNATURE)
		die("wrong superblock signature\n");
	info.bmove = malloc(super.bsize);
	assert(info.bmove);
	info.ipsize = sizeof(struct indexpage)
		    + super.bsize - sizeof(baddr_t);
}

static inline void
setbit (uint8_t *base, baddr_t bitnum)
{
assert(bitnum < info.mapsize*8);
	base[bitnum>>3] |= 1 << (bitnum&7);
}

static inline void
clearbit (uint8_t *base, baddr_t bitnum)
{
	base[bitnum>>3] &= ~(1 << (bitnum&7));
}

static inline int
testbit (const uint8_t *base, baddr_t bitnum)
{
	return base[bitnum>>3] & (1 << (bitnum&7));
}

static inline int
is_block_free(baddr_t block)
{
	return (!(testbit(info.physmap, block)
		|| testbit(info.reserved, block)
		|| testbit(info.reserved2, block)));
}

static baddr_t
balloc (void)
{
	for (; info.balloc_cur < super.psize; info.balloc_cur++)
		if (is_block_free(info.balloc_cur)) {
			setbit(info.reserved2, info.balloc_cur);
			return info.balloc_cur++;
		}

	die("cannot allocate temporary block\n");
	return 0;
}

static inline int
ip_offset (const struct indexpage *ip, const struct chunk *chunk)
{
	return ((void *)chunk) - ((void *)ip);
}

static inline struct chunk *
next_chunk (const struct indexpage *ip, struct chunk *chunk)
{
	chunk = (((void *)chunk)
	      + chunk->nblocks*sizeof(baddr_t)
	      + sizeof(*chunk));
	return ((ip_offset(ip, chunk) < (info.ipsize - sizeof(*chunk)))
		&& chunk->nblocks)
		? chunk
		: NULL;
}

static void
init_physmap (struct indexpage *ip)
{
	struct chunk *chunk;
	int i;

	for (chunk = ip->chunk; chunk;) {
		for (i = chunk->nblocks - 1; i >= 0; i--) {
			setbit(info.physmap, chunk->block[i]);
		}
		chunk = next_chunk(ip, chunk);
	}
	setbit(info.physmap, ip->phys);
}

static void
load_index (void)
{
	struct indexpage *ip;
	struct indexpage *prev = NULL;
	baddr_t		 phys;

	printf("Loading indexblocks...");
	info.mapsize = (super.psize + 7) >> 3;
	info.reserved = (uint8_t *)malloc(info.mapsize);
	assert(info.reserved);
	info.reserved2 = (uint8_t *)malloc(info.mapsize);
	assert(info.reserved2);
	info.physmap = (uint8_t *)malloc(info.mapsize);
	assert(info.physmap);
	memset(info.physmap, 0, info.mapsize);

	for (phys = super.index0; phys != NULL_BLOCK; ) {
		ip = (struct indexpage *)malloc(info.ipsize);
		assert(ip);
		ip->phys = phys;
		ip->new_phys = phys;
		if (phys == super.indexcur)
			info.ip = ip;
		if (!info.index)
			info.index = ip;
		ip->prev = prev;
		ip->next = NULL;
		if (prev)
			prev->next = ip;
		prev = ip;
		bseek(phys);
		assert(read(info.fd, &phys, sizeof(phys)) != -1);
		assert(read(info.fd, ip->chunk,
			    super.bsize - sizeof(phys)) != -1);
		init_physmap(ip);
	}
	setbit(info.physmap, 0);
	setbit(info.physmap, super.block0);
	printf(" done.\n");
}

#define DECLARE_LOG_BMOVE(N)					\
static void log_bmove##N (baddr_t from, baddr_t to)		\
{								\
	info.moverec##N[super.nmoverec##N].from = from;		\
	info.moverec##N[super.nmoverec##N].to   = to;		\
	super.nmoverec##N++;					\
}

DECLARE_LOG_BMOVE(0)
DECLARE_LOG_BMOVE(1)
DECLARE_LOG_BMOVE(2)

static void
log_bfix (baddr_t block, int offset, baddr_t fix)
{
	int i;

	for (i = super.nfixrec - 1; i >= 0; i--)
		if ((info.fixrec[i].block == block)
		    && (info.fixrec[i].offset == offset)) {
			info.fixrec[i].fix = fix;
			return;
		}
	info.fixrec[super.nfixrec].block  = block;
	info.fixrec[super.nfixrec].offset = offset;
	info.fixrec[super.nfixrec].fix    = fix;
	super.nfixrec++;
}

static void
first_bmove (struct indexpage *ip, struct bmove *bm)
{
	bm->i	      = 0;
	bm->chunk     = ip->chunk;
	bm->to        = bm->chunk->offset;
	bm->from      = bm->chunk->block[0];
	bm->ip        = ip;
	bm->ib_offset = offsetof(struct indexblock, chunk)
		      + offsetof(struct chunk, block);
}

static inline int
ib_offset (const struct indexpage *ip, const baddr_t *blockp)
{
	return offsetof(struct indexblock, chunk) +
		(void *)blockp - (void *)ip->chunk;
}

static int
next_bmove (struct bmove *bm)
{
	bm->i++;
	if (bm->i >= bm->chunk->nblocks) {
		bm->chunk = next_chunk(bm->ip, bm->chunk);
		if (!bm->chunk)
			return 0;
		bm->i         = 0;
		bm->to        = bm->chunk->offset;
		bm->ib_offset = ib_offset(bm->ip, bm->chunk->block);
	} else {
		bm->to++;
		bm->ib_offset += sizeof(baddr_t);
	}
	bm->from = bm->chunk->block[bm->i];

	return 1;
}

static void
handle_cross_block (struct indexpage *ip, struct bmove *bm,
		    baddr_t *physp, baddr_t virt)
{
	baddr_t temp;
	if (is_block_free(virt)) {
		/*
		 * think of it as of optimisation attempt :)
		 */
		clearbit(info.physmap, bm->from);
		setbit(info.reserved2, bm->from);
		setbit(info.physmap, virt);
		log_bmove0(bm->to, virt);
		log_bfix(ip->phys, ib_offset(ip, physp), virt);
		*physp = virt;
	} else {
		temp = balloc();
		log_bmove0(bm->to, temp);
		log_bmove2(temp, bm->from);
		log_bfix(ip->phys, ib_offset(ip, physp), bm->from);
		*physp = bm->from;
	}
}

static int
find_cross_block (struct bmove *bm)
{
	struct indexpage *ip;
	struct chunk	 *chunk;
	baddr_t		 *physp;
	baddr_t		 virt;
	int		 i;

/*        printf("find_cross_block\n"); */

        if(hack_chunk != NULL)
        {
            if ((*hack_physp == bm->to) && (*hack_physp != hack_virt))
            {

                ip = hack_ip;
                chunk = hack_chunk;
                physp = hack_physp;
                virt = hack_virt;
                i = hack_i;

                goto VERY_UGLY_HACK;
            }
        }

	i = bm->i + 1;
	chunk = bm->chunk;
	for (ip = bm->ip; ip; ip = ip->next, chunk = ip->chunk)
		for (; chunk; chunk = next_chunk(ip, chunk), i = 0) {

/*  printf("chunk: %p  bm->to: %lx\n", chunk, (long int)bm->to); */

			virt = chunk->offset + i;
			physp = &chunk->block[i];
			for (; i < chunk->nblocks; i++, virt++, physp++) {
                                VERY_UGLY_HACK:
 
				if ((*physp == bm->to) && (*physp != virt)) {

                                        hack_ip = ip;
                                        hack_chunk = chunk;
                                        hack_physp = physp;
                                        hack_virt = virt;
                                        hack_i = i;

                                        hack_i++;
                                        hack_virt++;
                                        hack_physp++;

                                        if(hack_i >= chunk->nblocks)
                                        {
                                            hack_chunk = next_chunk(hack_ip, hack_chunk);
                                            hack_i = 0;

                                            if(hack_chunk)
                                            {
                                                hack_virt = hack_chunk->offset + hack_i;
                                                hack_physp = &hack_chunk->block[hack_i];
                                            }
                                        }

					handle_cross_block(ip, bm, physp, virt);
					return 1;
				}
                       }
		}
	return 0;
}

static void
handle_cross_indexpage (baddr_t phys, baddr_t new_phys)
{
	struct indexpage *ip;

	for (ip = info.index; ip; ip = ip->next)
		if (phys == ip->phys) {
			ip->new_phys = new_phys;
			return;
		}
	die("should not get here: %d\n", phys);
}

static void
init_reserved (void)
{
	struct chunk *chunk;
	int          i;

	memset(info.reserved, 0, info.mapsize);
	memset(info.reserved2, 0, info.mapsize);
	for (chunk = info.ip->chunk; chunk;) {
		for (i = chunk->nblocks - 1; i >= 0; i--)
			setbit(info.reserved, chunk->offset + i);
		chunk = next_chunk(info.ip, chunk);
	}
	info.balloc_cur = 0;
}

static void
prepare_log (void)
{
	struct indexpage *ip;
	struct bmove	 bm;
	baddr_t		 from;

	printf("Relocating block group at %d...", info.ip->chunk[0].offset);
	super.nmoverec0 = 0;
	super.nmoverec1 = 0;
	super.nmoverec2 = 0;
	super.nfixrec   = 0;
	init_reserved();

	first_bmove(info.ip, &bm);
	do {
		if (bm.to == bm.from)
			continue;
		if (testbit(info.reserved, bm.from)) {
			from = balloc();
			clearbit(info.physmap, bm.from);
			setbit(info.reserved2, bm.from);
			setbit(info.physmap, from);
			log_bmove0(bm.from, from);
			bm.chunk->block[bm.i] = from;
		}
	} while (next_bmove(&bm));

	first_bmove(info.ip, &bm);
	do {
		if (bm.to == bm.from)
			continue;
		bm.chunk->block[bm.i] = bm.to;
		log_bmove1(bm.from, bm.to);
		log_bfix(bm.ip->phys, bm.ib_offset, bm.to);
		if (testbit(info.physmap, bm.to)) {
			baddr_t temp;
			if (find_cross_block(&bm))
				continue;
			temp = balloc();
			log_bmove0(bm.to, temp);
			log_bmove2(temp, bm.from);
			if (bm.to == super.block0)
				super.block0 = bm.from;
			else
				handle_cross_indexpage(bm.to, bm.from);
		} else {
			setbit(info.physmap, bm.to);
			clearbit(info.physmap, bm.from);
			setbit(info.reserved2, bm.from);
		}
	} while (next_bmove(&bm));

	for (ip = info.index->next; ip; ip = ip->next)
		if (ip->phys != ip->new_phys)
			log_bfix(ip->prev->phys,
				 offsetof(struct indexblock, next),
				 ip->new_phys);

	for (ip = info.index; ip; ip = ip->next)
		ip->phys = ip->new_phys;

	info.ip = info.ip->next;
	super.indexcur = info.ip ? info.ip->phys : NULL_BLOCK;
	super.index0 = info.index->phys;
}

static void
init_log (void)
{
	int bpmb = super.bsize/sizeof(baddr_t);

	info.moverec0 = (struct moverec *)malloc(10*bpmb*sizeof(struct moverec));
	assert(info.moverec0);
	info.moverec1 = (struct moverec *)malloc(10*bpmb*sizeof(struct moverec));
	assert(info.moverec1);
	info.moverec2 = (struct moverec *)malloc(10*bpmb*sizeof(struct moverec));
	assert(info.moverec2);
	info.fixrec  = (struct fixrec *)malloc(10*bpmb*sizeof(struct fixrec));
	assert(info.fixrec);
}

static void
commit (int log)
{
	assert(fsync(info.fd) != -1);
	bseek(0);
	super.state = log;
	assert(write(info.fd, &super, sizeof(super)) != -1);
	assert(fsync(info.fd) != -1);
}

static void
read_log_part (baddr_t *blocks, void *data, size_t n)
{
	int i;

	for (i = n/super.bsize; i > 0; i--) {
		bseek(*blocks++);
		assert(read(info.fd, data, super.bsize) != -1);
		data += super.bsize;
	}
	if (n % super.bsize) {
		bseek(*blocks);
		assert(read(info.fd, data, n % super.bsize) != -1);
	}
}

static void
write_log_part (baddr_t *blocks, void *data, size_t n)
{
	int i;

	for (i = n/super.bsize; i > 0; i--) {
		*blocks = balloc();
		bseek(*blocks++);
		assert(write(info.fd, data, super.bsize) != -1);
		data += super.bsize;
	}
	if (n % super.bsize) {
		*blocks = balloc();
		bseek(*blocks);
		assert(write(info.fd, data, n % super.bsize) != -1);
	}
}

static void
write_log (void)
{
	write_log_part(super.moverec0, info.moverec0,
		       super.nmoverec0*sizeof(struct moverec));
	write_log_part(super.moverec1, info.moverec1,
		       super.nmoverec1*sizeof(struct moverec));
	write_log_part(super.moverec2, info.moverec2,
		       super.nmoverec2*sizeof(struct moverec));
	write_log_part(super.fixrec, info.fixrec,
		       super.nfixrec*sizeof(struct fixrec));
	commit(LOG_STAGE0_DONE);
}

static void
commit_log_stage1 (void)
{
	int i;

	for (i = super.nfixrec - 1; i >= 0; i--)
		bfix(info.fixrec[i].block, info.fixrec[i].offset,
		     info.fixrec[i].fix);
	for (i = 0; i < super.nmoverec0; i++)
		bmove(info.moverec0[i].from, info.moverec0[i].to);
	commit(LOG_STAGE1_DONE);
}

static void
commit_log_stage2 (void)
{
	int i;

	for (i = 0; i < super.nmoverec1; i++)
		bmove(info.moverec1[i].from, info.moverec1[i].to);
	commit(LOG_STAGE2_DONE);
}

static void
commit_log_stage3 (void)
{
	int i;

	for (i = 0; i < super.nmoverec2; i++)
		bmove(info.moverec2[i].from, info.moverec2[i].to);
	commit(LOG_STAGE3_DONE);
}

static void
commit_log (void)
{
	commit_log_stage1();
	commit_log_stage2();
	commit_log_stage3();
	printf(" done.\n");
}

static void
load_log (void)
{
	read_log_part(super.moverec0, info.moverec0,
		      super.nmoverec0*sizeof(struct moverec));
	read_log_part(super.moverec1, info.moverec1,
		      super.nmoverec1*sizeof(struct moverec));
	read_log_part(super.moverec2, info.moverec2,
		      super.nmoverec2*sizeof(struct moverec));
	read_log_part(super.fixrec, info.fixrec,
		      super.nfixrec*sizeof(struct fixrec));
}

static void
replay_log (void)
{
	if (super.state == LOG_CLEAN)
		return;

	printf("Replaying log...\n");
	load_log();
	switch (super.state) {
	case LOG_STAGE0_DONE:
		commit_log_stage1();
	case LOG_STAGE1_DONE:
		commit_log_stage2();
	case LOG_STAGE2_DONE:
		commit_log_stage3();
	}
}

int
main (int argc, char *argv[])
{
        time_t t;

	selfname = argv[0];
	if (argc != 2) {
		fprintf(stderr, "Usage: %s DEVICE\n", selfname);
		return 1;
	}
	info.fd = open64(argv[1], O_RDWR);
	if (info.fd < 0)
		die("opening '%s': %m\n", argv[1]);

	setvbuf(stdout, NULL, _IONBF, 0);
	load_superblock();
	init_log();
	replay_log();
	load_index();
	while (info.ip) {
                t = time(NULL);

		prepare_log();
		write_log();
		commit_log();

                printf("...that took %d seconds.\n", (int)(time(NULL)-t));
	}
	printf("And now the block0...\n");
	bmove(super.block0, 0);
	assert(fsync(info.fd) != -1);
	close(info.fd);

	return 0;
}
