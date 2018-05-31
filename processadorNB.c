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
    if (criticalSection) {
        printf("\nSinal %d (Ctrl+C) recebido, mas vai ser ignorado\n", x);
        return;
    }

    printf("\nSinal %d (Ctrl+C) recebido. A parar a execução\n", x);
    removeTempExit(0);
}

// Handler que não faz nada
void handler() {}

// Função que lê uma linha (ou até nbytes carateres) do input e coloca em buf e, se necessário, aumenta a capacidade de buf
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

// Função que transforma uma string de argumentos separada por espaços num array de argumentos
// Devolve: array de apontadores para os argumentos
char **getArgs(char *buf, int *numArgs) {
    int i = 0, myNumArgs = 1;

    // Contar o número de argumentos
    while (buf[i] != '\n') {
        if (buf[i] == ' ') {
            do {
                i++;
            } while (buf[i] == ' ');

            myNumArgs++;
        } else
            i++;
    }

    char **args = malloc((myNumArgs+1) * sizeof(char *));
    int length = i, j = 0;
    for (i = 0; i < myNumArgs; i++) {
        int auxSize = 8, k = 0;
        char *aux = malloc(auxSize * sizeof(char));
        while (j < length && buf[j] != ' ') {
            aux[k++] = buf[j++];
            if (k == auxSize) {
                aux = realloc(aux, 2 * auxSize * sizeof(char));
                auxSize *= 2;
            }
        }
        aux[k] = '\0';

        // Saltar os espaços entre argumentos
        while (j < length && buf[j] == ' ')
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

// Função que verifica se o comando contém redirecionamento de input/output e altera o input/output se tal se verificar
void redirectInOut(char **args, int *in, int *out, char **argv) {
    int i = 0;
    char *a = args[i];

    while (a != NULL) {
        if (strcmp(a, ">") == 0) {
            if (strcmp(args[i+1], TEMP_FILE) == 0 || strcmp(args[i+1], argv[1]) == 0) {
                printf("Erro a executar o programa: %s\nPor favor, escolha um ficheiro de output diferente\n", args[0]);
                removeTempExit(1);
            }

            *out = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (*out == -1) {
                printf("Erro a executar o programa: %s\nHouve um problema a redirecionar o output\n", args[0]);
                removeTempExit(1);
            }

            free(args[i]);
            args[i] = NULL;
            i++;
        } else if (strcmp(a, "<") == 0) {
            *in = open(args[i+1], O_RDONLY);
            if (*in == -1) {
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
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigquitHandler);
    signal(SIGPIPE, handler);

    // Verificar se existe ficheiro para processar. Se não houver, imprimir
    // o modo de utilização e sair
    if (argc != 2) {
        printf("Uso: %s exemplo.nb\n", argv[0]);
        exit(1);
    }

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
        // Se o texto a seguir for o output do comando de um processamento anterior, ignorá-lo
        if (n == 4 && strncmp(buf, ">>>\n", 4) == 0) {
            while (1) {
                readln(notebook, &buf, &bufSize);
                if (n == 4 && strncmp(buf, "<<<\n", 4) == 0)
                    break;
            }

            continue;
        }

        // Escrever a linha no ficheiro temporário (quer seja uma linha de texto ou uma linha de comando)
        write(temp, buf, n);

        // Se a linha for um comando, interpretá-lo e executá-lo
        if (buf[0] == '$') {
            write(temp, ">>>\n", 4);

            // Guardar a linha atual numa string para usar quando necessário
            char *line = malloc(sizeof(char) * n);
            strncpy(line, buf, n-1);

            // Guardar a posição do ficheiro temporário onde começa o output do programa a executar
            outputs[curCommand] = lseek(temp, 0, SEEK_CUR);
            if (curCommand == numOutputs-1) {
                outputs = realloc(outputs, 2 * numOutputs * sizeof(int));
                numOutputs *= 2;
            }

            // Verificar se o comando a executar necessita de receber como input o output de um programa
            // anterior e se sim coloca qual em inputNum
            char *mybuf = buf+2;
            int inputNum = -1, inputChanged = 0;
            if (buf[1] != ' ') {
                mybuf += 1;
                inputChanged = 1;
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

            // Ignorar os espaços depois de '$n|'
            while (mybuf[0] == ' ')
                mybuf++;

            // Obter o array de argumentos para passar ao comando
            int numArgs;
            char **args = getArgs(mybuf, &numArgs);

            // Verificar se o número de input é válido. Se não for, avisar o utilizador e parar o processamento
            if (inputChanged && (inputNum < 0 || inputNum >= curCommand)) {
                printf("Número de input inválido na linha: %s\n", line);
                removeTempExit(1);
            }

            // Criar um pipe para enviar ao comando a executar o output de um programa anterior se ele quiser
            int p[2] = {0, 1};
            if (inputNum != -1) {
                int aux = 0;
                while (args[aux] != NULL && strcmp(args[aux], "|") != 0) {
                    if (strcmp(args[aux], "<") == 0) {
                        printf("Input anterior e redirecionamento do input simultâneos na linha:\n%s\n", line);
                        removeTempExit(1);
                    }
                    aux++;
                }

                int res = pipe(p);
                if (res == -1) {
                    printf("Não foi possível criar o pipe\n");
                    removeTempExit(1);
                }
            }

            // Criar filhos conforme o número de comandos a executar (caso especial para quando só há um)
            int numPipes = 0;
            for (int i = 0; i < numArgs; i++) {
                if (strcmp(args[i], "|") == 0)
                    numPipes++;
            }

            if (numPipes == 0) {
                int x = fork();
                if (x == 0) {
                    close(temp);
                    temp = open(TEMP_FILE, O_WRONLY | O_APPEND);
                    if (temp == -1)
                        exit(1);

                    int in = p[0], out = temp;
                    redirectInOut(args, &in, &out, argv);

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
                    if (in != p[0])
                        close(in);

                    execvp(args[0], args);

                    exit(1);
                }
            } else {
                int previousReadPipe = -1;
                int i = 0, k = 0;
                while (i < numArgs) {
                    while (k < numArgs) {
                        if (strcmp(args[k], "|") == 0) {
                            args[k] = NULL;
                            break;
                        }
                        k++;
                    }

                    int pAux[2], x;
                    pipe(pAux);
                    x = fork();
                    if (x == 0) {
                        int in, out;

                        if (i == 0) { // Primeiro comando
                            in = p[0];
                            out = pAux[1];
                            redirectInOut(args+i, &in, &out, argv);

                            dup2(in, 0);
                            dup2(out, 1);

                            if (in != p[0])
                                close(in);
                            if (out != pAux[1])
                                close(out);
                        } else if (k != numArgs) { // Comandos intermédios
                            in = previousReadPipe;
                            out = pAux[1];
                            redirectInOut(args+i, &in, &out, argv);

                            dup2(in, 0);
                            dup2(out, 1);

                            if (in != previousReadPipe)
                                close(in);
                            if (out != pAux[1])
                                close(out);
                        } else { // Último comando
                            close(temp);
                            temp = open(TEMP_FILE, O_WRONLY | O_APPEND, 0600);
                            if (temp == -1)
                                exit(1);

                            in = previousReadPipe;
                            out = temp;
                            redirectInOut(args+i, &in, &out, argv);

                            dup2(in, 0);
                            dup2(out, 1);

                            if (in != previousReadPipe)
                                close(in);
                            if (out != temp)
                                close(out);
                        }

                        if (previousReadPipe != -1)
                            close(previousReadPipe);
                        close(pAux[0]);
                        close(pAux[1]);
                        if (inputNum != -1) {
                            close(p[0]);
                            close(p[1]);
                        }
                        close(notebook);
                        close(temp);

                        execvp(args[i], args+i);

                        exit(1);
                    }

                    close(pAux[1]);
                    if (previousReadPipe != -1)
                        close(previousReadPipe);
                    previousReadPipe = pAux[0];

                    k++;
                    i = k;
                }

                close(previousReadPipe);
            }

            // Se o comando a executar quiser como input o output de outro
            // comando, enviar-lhe o output
            if (inputNum != -1) {
                close(p[0]);
                lseek(temp, outputs[inputNum], SEEK_SET);
                while (1) {
                    int n = readln(temp, &buf, &bufSize);
                    if (n == 4 && strncmp(buf, "<<<\n", 4) == 0)
                        break;
                    else {
                        int res = write(p[1], buf, n);
                        if (res == -1)
                            break;
                    }
                }
                close(p[1]);
            }

            // Esperar que os comandos a executar terminem e verificar se o
            // fizeram com sucesso. Se isso não acontecer, parar o processamento
            // do notebook e avisar o utilizador
            int status;
            while (wait(&status) != -1) {
                if (WEXITSTATUS(status) != 0) {
                    printf("Erro a executar a linha: %s\n", line);
                    removeTempExit(1);
                }
            }

            freeArgs(args, numArgs);
            free(line);

            // Colocar '\n' (se ainda não tiver) antes de imprimir "<<<\n"
            lseek(temp, -1, SEEK_END);
            read(temp, buf, 1);
            if (buf[0] != '\n')
                write(temp, "\n", 1);
            write(temp, "<<<\n", 4);

            curCommand++;
        }
    }

    free(outputs);
    free(buf);

    // Se ocorrer um erro a ler do notebook, parar o processamento
    if (n == -1) {
        printf("Erro a ler do notebook\n");
        removeTempExit(1);
    }

    // Alterar o nome do ficheiro temporário para o nome do notebook
    close(notebook);
    close(temp);

    criticalSection = 1;

    int res = rename(TEMP_FILE, argv[1]);
    if (res == -1) {
        printf("Não foi possível alterar o nome do ficheiro temporário para %s\n", argv[1]);
        removeTempExit(1);
    }

    exit(0);
}
