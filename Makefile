CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -g

mysh: mysh.c
	$(CC) $(CFLAGS) -o mysh mysh.c

clean:
	rm -f mysh
