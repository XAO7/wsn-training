.PHONY: clear all

all: client.exe
	./client

client.exe: client.c
	gcc -o client.exe .\client.c -lws2_32

clear:
	rm .\client.exe