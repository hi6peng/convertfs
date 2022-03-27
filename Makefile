CFLAGS=-g -Wall -O3

all: devclone devremap prepindex
	sync

devclone: devclone.c
	$(CC) -o devclone devclone.c $(CFLAGS)

devremap: devremap.c convertfs.h
	$(CC) -o devremap devremap.c $(CFLAGS)

prepindex: prepindex.c convertfs.h
	$(CC) -o prepindex prepindex.c $(CFLAGS)

clean:
	rm -f *.o devclone devremap prepindex core
