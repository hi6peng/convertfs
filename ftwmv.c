/*
    ftw - file tree walk
    Copyright (C) 2004  Guus Sliepen <guus@sliepen.eu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>

#define MAX 8192

bool verbose = false;

char *from, *to;
int fromlen, tolen;

void fromto(char *dest, const char *file) {
	strcpy(dest, to);
	strcpy(dest + tolen, file + fromlen);
}

int mvhandler(const char *file, const struct stat *sb, int flag, struct FTW *s) {
	static char dest[MAX];
	int status;

	if(verbose)
		printf("%s\n", file);

	fromto(dest, file);
	
	if(flag == FTW_D) {
		if(mkdir(dest, sb->st_mode) == -1)
			fprintf(stderr, "Could not make directory %s: %s\n", dest, strerror(errno));
		else if(chown(dest, sb->st_uid, sb->st_gid) == -1)
			fprintf(stderr, "Could not set owner of %s: %s\n", dest, strerror(errno));
	} else {
		if(!fork()) {
			execlp("/bin/mv", "mv", file, dest, NULL);
			fprintf(stderr, "Could not exec /bin/mv: %s\n", strerror(errno));
			exit(1);
		}
		wait(&status);
	}

	return 0;
}

int rmdirhandler(const char *file, const struct stat *sb, int flag, struct FTW *s) {
	if(flag != FTW_D && flag != FTW_DP)
		return 0;
	
	if(verbose)
		printf("%s\n", file);

	if(rmdir(file) == 1)
		fprintf(stderr, "Could not remove directory %s: %s\n", file, strerror(errno));

	return 0;
}

static struct option const long_options[] = {
	{"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 1},
        {NULL, 0, NULL, 0}
};

void usage(char *argv0, bool help) {
	fprintf(stderr, "Usage: %s [-v] FROM TO\n", argv0);

	if(help) {
		fprintf(stderr,
			"\n"
			"  -v, --verbose                 list all processed files\n"
			"      --help                    show this help\n"
			"\n"
		);
	} else {
		fprintf(stderr, "Try '%s --help' for more information.\n", argv0);
	}
}

int main(int argc, char **argv) {
	int result, index = 0;
	
	while((result = getopt_long(argc, argv, "v", long_options, &index)) != -1) {
		switch(result) {
			case 'v':
				verbose = true;
				break;
			case 1:
				usage(argv[0], true);
				return 0;
			default:
				usage(argv[0], false);
				return 1;
		}
	}

	if(argc - optind != 2)
		usage(argv[0], false);

	from = argv[optind++];
	to = argv[optind++];

	fromlen = strlen(from);
	tolen = strlen(to);

	return nftw(from, mvhandler, 100, FTW_PHYS) ?: nftw(from, rmdirhandler, 100, FTW_PHYS | FTW_DEPTH);
}
