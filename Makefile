processadorNB: processadorNB.o
	gcc -Wall -Wextra -o processadorNB processadorNB.o
	rm *.o

clean:
	rm -r -f *.txt processadorNB
