CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -pedantic-errors
OPTIMIZATION = -O2
LIBS = -pthread

all: client server
.PHONY: all client server

client:
	gcc $(CFLAGS) $(OPTIMIZATION) client/*.c $(LIBS) -o client/client.exe
	rm -f client/*.txt client/*.jpeg client/*.jpg

server:
	gcc $(CFLAGS) $(OPTIMIZATION) server/*.c $(LIBS) -o server/server.exe

clean:
	rm client/client.exe server/server.exe client/*.txt client/*.jpeg
