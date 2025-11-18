CC     = gcc
FLAGS = -g -std=c99 -Wall -Wvla -Werror -fsanitize=address,undefined

all: mysh

mysh: mysh.c
		$(CC) $(FLAGS) -o $@ $^

clean:
	rm -f *.o mysh

