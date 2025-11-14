CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -g -pedantic

mysh: mysh.c
	$(CC) $(CFLAGS) -o mysh mysh.c

clean:
	rm -f mysh

