CC=gcc
CFLAGS=-Wall -Wextra

processadorNB: processadorNB.o
	gcc -Wall -Wextra -o processadorNB processadorNB.o

zip:
	zip -9 processadorNB.zip processadorNB.c exemplo.nb Makefile

clean:
	rm -r -f *.txt *.o processadorNB
