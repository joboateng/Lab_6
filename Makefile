CC = gcc
CFLAGS = -Wall -Wextra -Wno-logical-op-parentheses -Wno-unused-variable -Wno-unused-parameter -Wno-tautological-pointer-compare

all: oss user

oss: oss.o
	$(CC) $(CFLAGS) -o oss oss.o

user: user.o
	$(CC) $(CFLAGS) -o user user.o

oss.o: oss.c
	$(CC) $(CFLAGS) -c oss.c

user.o: user.c
	$(CC) $(CFLAGS) -c user.c

clean:
	rm -f oss user *.o

