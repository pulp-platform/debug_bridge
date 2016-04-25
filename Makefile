CC=gcc


all: debug_bridge

debug_bridge: main.c
	$(CC) -std=c99 -o $@ $^
