.PHONY: clear all

all: server.exe
	./server

server.exe: server.c
	gcc -o server.exe .\server.c -lws2_32

clear:
	rm .\server.exe