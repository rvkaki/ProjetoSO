#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define INITIAL_BUF_SIZE        64
#define INITIAL_NUM_COMANDS     8
#define TEMP_FILE               "temp.txt"

int temp = -1;
int criticalSection = 0;

// Função que imprime como utilizar o programa e faz exit
void printUsageExit(char *name) {
    printf("Uso: %s exemplo.nb\n", name);
    exit(1);
}

// Função que remove o ficheiro temporário e faz exit
void removeTempExit(int exitStatus) {
    if (temp != -1) {
        close(temp);

        int res = remove(TEMP_FILE);
        if (res == -1) {
            printf("Não foi possível eliminar o ficheiro temporário\n");
            exit(1);
        }
    }
    exit(exitStatus);
}

// Handler para lidar com o Ctrl+C
void sigquitHandler(int x) {
    if (criticalSection)
        return;

    printf("\nSinal %d (Ctrl+C) recebido. A parar a execução\n", x);
    removeTempExit(0);
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
char **getArgs(char *buf, int *numArgs) {
    int i = 0, myNumArgs = 1;
    // Contar o número de argumentos
    while (buf[i] != '\n') {
        if (buf[i] == ' ')
            myNumArgs++;
        i++;
    }

    char **args = malloc((myNumArgs+1) * sizeof(char *));
    int length = i, j = 0;
    for (i = 0; i < myNumArgs; i++) {
        int auxSize = 4, k = 0;
        char *aux = malloc(auxSize * sizeof(char));
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

    args[myNumArgs] = NULL;

    *numArgs = myNumArgs;

    return args;
}

// Função que liberta a memória associada aos argumentos
void freeArgs(char **args, int numArgs) {
    for (int i = 0; i < numArgs; i++)
        free(args[i]);
    
    free(args);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigquitHandler);

    // Verificar se os argumentos estão corretos. Se não estiverem, imprimir
    // o modo de utilização e sair
    if (argc != 2 || !isNBFile(argv[1]))
        printUsageExit(argv[0]);

    // Abrir o notebook
    int notebook = open(argv[1], O_RDWR);
    if (notebook == -1) {
        printf("Não foi possível abrir o notebook\n");
        exit(1);
    }

    // Abrir o ficheiro temporário
    temp = open(TEMP_FILE, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (temp == -1) {
        printf("Não foi possível criar o ficheiro temporário\n");
        exit(1);
    }

    int n, bufSize = INITIAL_BUF_SIZE, curCommand = 0, numOutputs = INITIAL_NUM_COMANDS, *outputs = malloc(numOutputs * sizeof(int));
    char *buf = malloc(bufSize * sizeof(char));

    while ((n = readln(notebook, &buf, &bufSize)) > 0) {
        // Se o texto a seguir for o output do comando de um processamento
        // anterior, ignorá-lo
        if (n == 4 && strncmp(buf, ">>>\n", 4) == 0) {
            while (1) {
                readln(notebook, &buf, &bufSize);
                if (n == 4 && strncmp(buf, "<<<\n", 4) == 0)
                    break;
            }

            continue;
        }

        // Imprimir a linha para o ficheiro temporário, quer seja uma linha de
        // texto ou um comando
        write(temp, buf, n);

        // Se a linha for um comando, interpretá-lo e executá-lo
        if (buf[0] == '$') {
            write(temp, ">>>\n", 4);

            // Guardar a posição do ficheiro temporário onde começa o output
            // do programa a executar
            outputs[curCommand] = lseek(temp, 0, SEEK_CUR);
            if (curCommand == numOutputs-1) {
                outputs = realloc(outputs, 2 * numOutputs * sizeof(int));
                numOutputs *= 2;
            }

            // Verificar se o comando a executar necessita de receber como input
            // o output de um programa anterior e se sim coloca qual em inputNum
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

                    free(num);
                }
                inputNum = curCommand - inputOffset;
            }

            // Criar um pipe para enviar ao comando a executar o output de
            // um programa anterior se ele quiser
            int p[2] = {0, 1};
            if (inputNum != -1) {
                int res = pipe(p);
                if (res == -1) {
                    printf("Não foi possível criar o pipe\n");
                    removeTempExit(1);
                }
            }

            // Obter o array de argumentos para passar ao comando
            int numArgs;
            char **args = getArgs(mybuf, &numArgs);

            // Verificar se o comando contém redirecionamento de input/output
            // e alterar o input/output se tal se verificar
            int i = 0, in = p[0], out = temp;
            char *a = args[i];
            while (a != NULL) {
                if (strcmp(a, ">") == 0) {
                    if (strcmp(args[i+1], TEMP_FILE) == 0 || strcmp(args[i+1], argv[1]) == 0) {
                        printf("Erro a executar o programa: %s\nPor favor, escolha um ficheiro de output diferente\n", args[0]);
                        removeTempExit(1);
                    }

                    out = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
                    if (out == -1) {
                        printf("Erro a executar o programa: %s\nHouve um problema a redirecionar o output\n", args[0]);
                        removeTempExit(1);
                    }

                    free(args[i]);
                    args[i] = NULL;
                    i++;
                } else if (strcmp(a, "<") == 0) {
                    in = open(args[i+1], O_RDONLY);
                    if (in == -1 || inputNum != -1) {
                        printf("Erro a executar o programa: %s\nHouve um problema a redirecionar o input\n", args[0]);
                        removeTempExit(1);
                    }

                    free(args[i]);
                    args[i] = NULL;
                    i++;
                }

                i++;
                a = args[i];
            }

            // Criar um filho para executar o comando
            int x = fork();
            if (x == 0) {
                dup2(in, 0);
                dup2(out, 1);
                if (inputNum != -1) {
                    close(p[0]);
                    close(p[1]);
                }
                close(notebook);
                close(temp);
                if (out != temp)
                    close(out);

                execvp(args[0], args);

                exit(1);
            }

            // Fechar o ficheiro de output
            if (out != temp)
                close(out);

            // Se o comando a executar quiser como input o output de outro
            // comando, enviar-lhe o output
            if (inputNum != -1) {
                close(p[0]);
                lseek(temp, outputs[inputNum], SEEK_SET);
                while (1) {
                    int n = readln(temp, &buf, &bufSize);
                    if (n == 4 && strncmp(buf, "<<<\n", 4) == 0) {
                        break;
                    } else {
                        write(p[1], buf, n);
                    }
                }
                close(p[1]);
                lseek(temp, 0, SEEK_END);
            }

            // Esperar que o comando a executar termine e verificar se o
            // fez com sucesso. Se isso não acontecer, parar o processamento
            // do notebook
            int status;
            wait(&status);
            if (WEXITSTATUS(status) != 0) {
                printf("Erro a executar o programa: %s\n", args[0]);
                removeTempExit(1);
            }

            freeArgs(args, numArgs);

            // Colocar '\n' (se ainda não tiver) antes de imprimir "<<<\n"
            lseek(temp, -1, SEEK_CUR);
            read(temp, buf, 1);
            if (buf[0] != '\n')
                write(temp, "\n", 1);
            write(temp, "<<<\n", 4);

            curCommand++;
        }
    }

    free(outputs);

    // Se ocorrer um erro a ler do notebook, parar o processamento
    if (n == -1) {
        printf("Erro a ler do notebook\n");
        removeTempExit(1);
    }

    // Copiar todo o conteúdo do ficheiro temporário para o notebook original
    criticalSection = 1;

    ftruncate(notebook, 0);
    lseek(notebook, 0, SEEK_SET);
    lseek(temp, 0, SEEK_SET);
    while ((n = readln(temp, &buf, &bufSize)) > 0)
        write(notebook, buf, n);
    
    close(notebook);

    free(buf);
    
    removeTempExit(0);
}
