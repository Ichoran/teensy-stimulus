CC = gcc -O2

all: ticklish_util.o ticklish.o

ticklish.o: makefile ticklish_util.h ticklish.h ticklish.c ticklish_util.o
	$(CC) -c ticklish.c

ticklish_util.o: makefile ticklish_util.h
	$(CC) -c ticklish_util.c

clean:
	rm *.o
