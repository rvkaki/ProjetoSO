#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define INITIAL_BUF_SIZE    1
#define TEMP_FILE           "temp.txt"

int temp = -1;
int criticalSection = 0;

// Função que imprime como utilizar o programa e faz exit
void printUsageExit(char *name) {
    printf("Uso: %s exemplo.nb\n", name);
    exit(1);
}

// Função que remove o ficheiro temporário e faz exit
void removeTempExit() {
    if (temp != -1) {
        close(temp);

        int res = remove(TEMP_FILE);
        if (res == -1) {
            printf("Não foi possível eliminar o ficheiro temporário\n");
            exit(1);
        }
    }
    exit(0);
}

// Handler para lidar com o Ctrl+C
void sigquitHandler(int x) {
    if (criticalSection)
        return;

    printf("\nSinal %d (Ctrl+C) recebido. A parar a execução\n", x);
    removeTempExit();
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
            mybuf = realloc(mybuf, 2 * numbytes * sizeof(char));
            numbytes *= 2;
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
                aux = realloc(aux, 2 * auxSize * sizeof(char));
                auxSize *= 2;
            }
        }
        aux[k] = '\0';

        j++;

        args[i] = aux;
    }

    args[numArgs] = NULL;

    return args;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigquitHandler);

    if (argc != 2 || !isNBFile(argv[1]))
        printUsageExit(argv[0]);

    int notebook = open(argv[1], O_RDWR);
    if (notebook == -1) {
        printf("Não foi possível abrir o notebook\n");
        exit(1);
    }

    temp = open(TEMP_FILE, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (temp == -1) {
        printf("Não foi possível criar o ficheiro temporário\n");
        exit(1);
    }

    int n, bufSize = INITIAL_BUF_SIZE, curCommand = 0, numOutputs = 4, *outputs = malloc(numOutputs * sizeof(int));
    char *buf = malloc(bufSize * sizeof(char));

    while ((n = readln(notebook, &buf, &bufSize)) > 0) {
        if (n == 4 && strncmp(buf, ">>>\n", 4) == 0) {
            while (1) {
                readln(notebook, &buf, &bufSize);
                if (n == 4 && strncmp(buf, "<<<\n", 4) == 0)
                    break;
            }

            continue;
        }
        write(temp, buf, n);

        if (buf[0] == '$') {
            write(temp, ">>>\n", 4);
            outputs[curCommand] = lseek(temp, 0, SEEK_CUR);
            if (curCommand == numOutputs-1) {
                outputs = realloc(outputs, 2 * numOutputs * sizeof(int));
                numOutputs *= 2;
            }

            char *mybuf = buf+2;
            int inputNum = -1;
            if (buf[1] != ' ') {
                mybuf += 1;
                int inputOffset = 1;
                if (buf[1] != '|') {
                    int i = 0, sizeNum = 2;
                    char *num = malloc(sizeNum * sizeof(char));
                    while (buf[i+1] != '|') {
                        num[i] = buf[i+1];
                        i++;
                        if (i == sizeNum) {
                            num = realloc(num, 2 * sizeNum * sizeof(char));
                            sizeNum *= 2;
                        }
                    }
                    num[i] = '\0';
                    mybuf += i;
                    inputOffset = atoi(num);
                }
                inputNum = curCommand - inputOffset;
            }

            int p[2] = {0, 1};
            if (inputNum != -1)
                pipe(p);

            char **args = getArgs(mybuf);

            int x = fork();
            if (x == 0) {
                dup2(p[0], 0);
                dup2(temp, 1);
                if (inputNum != -1) {
                    close(p[0]);
                    close(p[1]);
                }
                close(notebook);
                close(temp);

                execvp(args[0], args);

                exit(1);
            }

            if (inputNum != -1) {
                close(p[0]);
                lseek(temp, outputs[inputNum], SEEK_SET);
                while (1) {
                    char *pos;
                    int n = read(temp, buf, bufSize);
                    if ((pos = strstr(buf, "<<<")) != NULL) {
                        write(p[1], buf, pos-buf);
                        break;
                    } else {
                        write(p[1], buf, n);
                    }
                }
                close(p[1]);
                lseek(temp, 0, SEEK_END);
            }

            int status;
            wait(&status);
            if (WEXITSTATUS(status) == EXIT_FAILURE) {
                printf("Erro a executar o programa: %s\n", args[0]);
                removeTempExit();
            }

            write(temp, "<<<\n", 4);

            curCommand++;
        }
    }

    if (n == -1) {
        printf("Erro a ler do notebook\n");
        removeTempExit();
    }

    criticalSection = 1;

    ftruncate(notebook, 0);
    lseek(notebook, 0, SEEK_SET);
    lseek(temp, 0, SEEK_SET);
    while ((n = readln(temp, &buf, &bufSize)) > 0)
        write(notebook, buf, n);
    
    close(notebook);
    
    removeTempExit();
}
