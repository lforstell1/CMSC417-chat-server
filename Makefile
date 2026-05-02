CC = gcc
CFLAGS = -Wall -Wextra -g
 
all: rserver
 
rserver: server.c server.h
	$(CC) $(CFLAGS) -o rserver server.c
 
clean:
	rm -f rserver