.PHONY: all clean

all: mytar.c mytar

clean:
	rm -f mytar *.o

mytar: mytar.c
	gcc -Wall -Wextra -std=c99 -o mytar mytar.c
