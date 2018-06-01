CC=gcc
CFLAGS=-Wall -Wextra

notebook: processadorNB.o
	gcc -Wall -Wextra -o notebook processadorNB.o

zip:
	zip -9 Grupo_23_LCC.zip processadorNB.c Relatorio_G23.pdf Makefile

clean:
	rm -r -f *.txt *.o notebook
