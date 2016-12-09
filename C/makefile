CC = gcc -O2 -std=gnu99

all: ticklish_util.o ticklish.o ticklish_example

ticklish_example: makefile ticklish_example.o ticklish.o ticklish_util.o
	$(CC) -o ticklish_example ticklish_example.o ticklish.o ticklish_util.o -lpthread -lm -lserialport

ticklish_example.o: makefile ticklish_example.c ticklish_util.h ticklish.h
	$(CC) -c ticklish_example.c

ticklish.o: makefile ticklish_util.h ticklish.h ticklish.c ticklish_util.o
	$(CC) -c ticklish.c

ticklish_util.o: makefile ticklish_util.h ticklish_util.c
	$(CC) -c ticklish_util.c

clean:
	rm *.o
