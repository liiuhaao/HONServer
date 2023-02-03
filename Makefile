CFLAGS=-Wall -pedantic
CC=gcc

all: server

clean :
	rm -f *.o server

server : server.o table.o
	gcc -o $@ $^ -g -Wall -lpthread
	$(CC) $(CFLAGS) -o server server.o table.o -lpthread

server.o: server.h table.h
table.o: table.h
