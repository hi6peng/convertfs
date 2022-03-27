#ifndef __CONVERTFS_H
#define __CONVERTFS_H

/* must be signed */
typedef int32_t baddr_t;

enum {
	LOG_STAGE3_DONE = 0,
	LOG_CLEAN	= 0,
	LOG_STAGE0_DONE,
	LOG_STAGE1_DONE,
	LOG_STAGE2_DONE,
};

/* must be no more than sector size */
struct superblock {
	int32_t signature;
	int32_t state;
	int32_t bsize;
	baddr_t	psize;
	baddr_t	index0;
	baddr_t indexcur;
	baddr_t block0;
	baddr_t moverec0[10];
	baddr_t moverec1[10];
	baddr_t moverec2[10];
	baddr_t fixrec[10];
	int32_t nmoverec0;
	int32_t nmoverec1;
	int32_t nmoverec2;
	int32_t nfixrec;
};

struct chunk {
	baddr_t	offset;
	int32_t	nblocks;
	baddr_t	block[0];
};

struct indexblock {
	baddr_t      next;
	struct chunk chunk[0];
};

#define NULL_BLOCK	((baddr_t)-1)
#define SB_SIGNATURE	0x39485761	/* intended to be "fsCV" */

#endif /* __CONVERTFS_H */
