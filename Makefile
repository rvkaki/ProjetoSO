CC=gcc
CFLAGS=-Wall -Wextra

processadorNB: processadorNB.o
	gcc -Wall -Wextra -o processadorNB processadorNB.o

clean:
	rm -r -f *.txt *.o processadorNB
