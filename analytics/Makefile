#Simple Makefile to build the localiser


CFLAGS += -g -O0
CFLAGS += -Wall
CFLAGS += -I/usr/local/include/json-c
LDFLAGS+= -L/usr/local/lib
LDLIBS+= -lzmq -lsqlite3 -lpthread -ljson-c -lIP2Location -lcurl

CC = gcc

all:
	${CC} $(CFLAGS) -g main.c -o analytics $(LDFLAGS) $(LDLIBS)
