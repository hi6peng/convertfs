CFLAGS=-g -Wall -O3

all: devclone devremap prepindex ftwmv
	sync

devclone: devclone.c
	$(CC) -o devclone devclone.c $(CFLAGS)

devremap: devremap.c convertfs.h
	$(CC) -o devremap devremap.c $(CFLAGS)

prepindex: prepindex.c convertfs.h
	$(CC) -o prepindex prepindex.c $(CFLAGS)

ftwmv: ftwmv.c
	gcc -o ftwmv ftwmv.c $(CFLAGS)

clean:
	rm -f *.o devclone devremap prepindex ftwmv core
