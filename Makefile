.PHONY: all clean

all: mytar.c mytar

clean:
	rm -f mytar *.o

mytar: mytar.c
	cc -Wall -Wextra -o mytar mytar.c
