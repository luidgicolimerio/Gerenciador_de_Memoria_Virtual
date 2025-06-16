// Bibliotecas necessárias
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// Constantes
#define QTDE_FILHOS 4
#define QTDE_ACESSOS 100
#define QUANTUM_SEGUNDOS 1      
#define RODADAS_TOTAIS 10
#define EVER ;;

//Variaveis globais
char paginas_filhos[QTDE_FILHOS][QTDE_ACESSOS][6]; // Ex: "23 W\0"

// Protótipos
static void rotina_filho(int id);
static void gerar_acessos_vetor();
static void imprimir_amostra();

int main(void) {
    gerar_acessos_vetor();
    imprimir_amostra();

    pid_t pids_filhos[QTDE_FILHOS];

    // Cria todos os filhos
    for (int i = 0; i < QTDE_FILHOS; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            // Código executado apenas pelo filho i
            rotina_filho(i); // Nunca retorna
            _exit(EXIT_SUCCESS);
        }
        // Código do pai continua aqui
        pids_filhos[i] = pid;
        // Inicia cada filho parado para controle explícito do escalonador
        if (kill(pid, SIGSTOP) == -1) {
            perror("kill(SIGSTOP)");
            exit(EXIT_FAILURE);
        }
    }
    printf("Todos os filhos foram criados e parados\n");
    // Loop de escalonamento Round-Robin
    for (int rodada = 0; rodada < RODADAS_TOTAIS; rodada++) {
        int indice = rodada % QTDE_FILHOS;
        // Continua o filho selecionado
        if (kill(pids_filhos[indice], SIGCONT) == -1) {
            perror("kill(SIGCONT)");
            continue;
        }
        sleep(QUANTUM_SEGUNDOS);

        if (kill(pids_filhos[indice], SIGSTOP) == -1) {
            perror("kill(SIGSTOP)");
        }
    }

    // Aguarda término
    for (int i = 0; i < QTDE_FILHOS; ++i) {
        kill(pids_filhos[i], SIGKILL);
        waitpid(pids_filhos[i], NULL, 0);
    }

    puts("TodosProcessos finalizado.");
    return 0;
}

// Rotina principal de cada filho
static void rotina_filho(int id) {
    int i = 0;

    for (EVER) {

        if (id == 0) {
            printf("Filho P1 – PID %d trabalhando\n", getpid());
            printf("Acesso: %s\n", paginas_filhos[id][i]);
            // TODO: Verificar com o professor
            // Como vamos chamar o GMV para ver se a página está disponível?
            // FIFO?
            // Memória compartilhada?
            // Sinal?
            i++;
            sleep(1);
        } else if (id == 1) {
            printf("Filho P2 – PID %d trabalhando\n", getpid());
            printf("Acesso: %s\n", paginas_filhos[id][i]);
            i++;
            sleep(1);
        } else if (id == 2) {
            printf("Filho P3 – PID %d trabalhando\n", getpid());
            printf("Acesso: %s\n", paginas_filhos[id][i]);
            i++;
            sleep(1);
        } else {
            printf("Filho P4 – PID %d trabalhando\n", getpid());
            printf("Acesso: %s\n", paginas_filhos[id][i]);
            i++;
            sleep(1);
        }
        fflush(stdout);
    }
}

static void gerar_acessos_vetor() {
    srand(time(NULL));

    for (int i = 0; i < QTDE_FILHOS; i++) {
        for (int j = 0; j < QTDE_ACESSOS; j++) {
            int pagina = rand() % 32;
            char tipo = (rand() % 2 == 0) ? 'R' : 'W';
            snprintf(paginas_filhos[i][j], 6, "%02d %c", pagina, tipo);
        }
    }
}

static void imprimir_amostra() {
    for (int i = 0; i < QTDE_FILHOS; i++) {
        printf("P%d:\n", i+1);
        for (int j = 0; j < 5; j++) {
            printf("  %s\n", paginas_filhos[i][j]);
        }
        printf("...\n");
    }
}
