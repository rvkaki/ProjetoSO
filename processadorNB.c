#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#define INITIAL_BUF_SIZE    1
#define TEMP_FILE           "temp.txt"

// Função que imprime como utilizar o programa e faz exit
void printUsageExit(char *name) {
    printf("Uso: %s exemplo.nb\n", name);
    exit(1);
}

// Função que determina se um ficheiro é do tipo .nb
// Devolve: 1 se for
//          0 caso contrário
int isNBFile(char *fileName) {
    int length = strlen(fileName);
    if (length < 4)
        return 0;
    
    return strcmp(fileName + length - 3, ".nb") == 0 ? 1 : 0;
}

// Função que lê uma linha (ou até nbytes carateres) do input e coloca em buf
// e, se necessário, aumenta a capacidade de buf
// Devolve: -1 se falhar
//          número de carateres lidos caso contrário
int readln(int input, char **buf, int *nbytes) {
    int numbytes = *nbytes;
    char *mybuf = *buf;

	int n, count = 0;
	char c = '\0';

	while (c != '\n' && (n = read(input, &c, 1)) > 0) {
		mybuf[count++] = c;
        if (count == numbytes) {
            mybuf = realloc(mybuf, numbytes * 2);
            numbytes = numbytes * 2;
        }
    }

    if (count > 0 && c != '\n')
        mybuf[count++] = '\n';

    *nbytes = numbytes;
    *buf = mybuf;

    if (n == -1)
		return -1;

	return count;
}

// Função que tranforma uma string de argumentos separada por espaços num
// array de argumentos
// Devolve: array de apontadores para os argumentos
char **getArgs(char *buf) {
    int i = 0, numArgs = 1;
    // Contar o número de argumentos
    while (buf[i] != '\n') {
        if (buf[i] == ' ')
            numArgs++;
        i++;
    }

    char **args = malloc((numArgs+1) * sizeof(char *));
    int length = i, j = 0;
    for (i = 0; i < numArgs; i++) {
        int auxSize = 4, k = 0;
        char *aux = malloc(auxSize);
        while (j < length && buf[j] != ' ') {
            aux[k++] = buf[j++];
            if (k == auxSize) {
                aux = realloc(aux, auxSize * 2);
                auxSize = auxSize * 2;
            }
        }
        aux[k] = '\0';

        j++;

        args[i] = aux;
    }

    args[numArgs] = NULL;

    return args;
}

void main(int argc, char *argv[]) {
    if (argc != 2)
        printUsageExit(argv[0]);

    if (!isNBFile(argv[1]))
        printUsageExit(argv[0]);

    int notebook = open(argv[1], O_RDWR);
    if (notebook == -1) {
        printf("Não foi possível abrir o notebook\n");
        exit(1);
    }

    int temp = open(TEMP_FILE, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (temp == -1) {
        printf("Não foi possível criar o ficheiro temporário\n");
        exit(1);
    }

    int n, buf_size = INITIAL_BUF_SIZE;
    char *buf = malloc(buf_size);

    while ((n = readln(notebook, &buf, &buf_size)) > 0) {
        write(temp, buf, n);

        if (buf[0] == '$') {
            write(temp, ">>>\n", 4);

            if (n >= 4 && buf[1] == '|') {
                // Falta implementar
            } else if (n >= 3) {
                char **args = getArgs(buf+2);

                int x = fork();
                if (x == 0) {
                    dup2(temp, 1);

                    execvp(args[0], args);

                    exit(1);
                }

                int status;
                wait(&status);
                if (WEXITSTATUS(status) == EXIT_FAILURE) {
                    printf("Erro a executar o programa: %s\n", args[0]);
                    exit(1);
                }
            }

            write(temp, "<<<\n", 4);
        }
    }

    if (n == -1) {
        printf("Erro a ler do notebook\n");
        exit(1);
    }

    close(notebook);

    /*
    int res = remove(TEMP_FILE);
    if (res == -1) {
        printf("Não foi possível eliminar o ficheiro temporário\n");
        exit(1);
    }
    */
}
