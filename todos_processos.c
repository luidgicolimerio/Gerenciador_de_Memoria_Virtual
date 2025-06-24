// Bibliotecas necessárias
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "gmv_proto.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

// Constantes
#define QTDE_FILHOS 4
#define QTDE_ACESSOS 100
#define QUANTUM_SEGUNDOS 1      
// Número de rodadas será recebido por parâmetro de linha de comando
static int RODADAS_TOTAIS = 100;

//Variaveis globais
char paginas_filhos[QTDE_FILHOS][QTDE_ACESSOS][6]; // Ex: "23 W\0"
int *contador_compartilhado = NULL;
int *contador_pag_sujas_shared = NULL; // novo ponteiro para páginas sujas

// Protótipos
static void rotina_filho(int id);
static void gerar_acessos_vetor();
static void imprimir_amostra();
/* protótipo para permitir chamada antes da definição */
static void exibir_relatorio_final(const char *algoritmo, int k_param, int rodadas,
                                   int total_pf, int dirty_pages);
static void salvar_acessos_arquivos();

int main(int argc, char *argv[]) {
    /* Parametros: argv[1] = rodadas, opcional */
    const char *algoritmo_nome = "(desconhecido)";

    if (argc >= 2) {
        RODADAS_TOTAIS = atoi(argv[1]);
        if (RODADAS_TOTAIS <= 0) RODADAS_TOTAIS = 1;
    }
    if (argc >= 3) {
        algoritmo_nome = argv[2]; // apenas para relatorio
    }

    gerar_acessos_vetor();
    salvar_acessos_arquivos();
    imprimir_amostra();

    // Cria o segmento de memória compartilhada para o contador de page faults
    int shmid = shmget(IPC_PRIVATE, sizeof(int),IPC_CREAT | S_IRUSR | S_IWUSR);
    if (shmid == -1) { perror("shmget"); exit(1); }
    contador_compartilhado = shmat(shmid, NULL, 0);
    if (contador_compartilhado == (void *)-1) { perror("shmat"); exit(1); }
    *contador_compartilhado = 0;          /* zera antes dos forks */
    
    /* anexa ao contador de páginas sujas criado pelo GMV */
    key_t shm_key_dp = ftok("/tmp", 'D');
    int shmid_dp = shmget(shm_key_dp, sizeof(int), 0666);
    if (shmid_dp == -1) { perror("shmget dirty"); exit(1); }
    contador_pag_sujas_shared = shmat(shmid_dp, NULL, 0);
    if (contador_pag_sujas_shared == (void *)-1) { perror("shmat dirty"); exit(1); }

    /* garante diretório de FIFOs */
    mkdir("./FIFOs", 0777);
    /* cria FIFO de requisições se ainda não existir */
    mkfifo("./FIFOs/gmv_req", 0666);

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
    for (int rodada = 0; rodada < RODADAS_TOTAIS*QTDE_FILHOS; rodada++) {
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
    contador_page_faults = *contador_compartilhado;

    /* Solicita ao GMV que grave as tabelas finais e finalize */
    FILE *pidf = fopen("gmv.pid", "r");
    if (pidf) {
        pid_t gpid; if (fscanf(pidf, "%d", &gpid) == 1) {
            kill(gpid, SIGUSR1);
            sleep(1); /* aguarda escrita de tables.txt */
        }
        fclose(pidf);
    }

    exibir_relatorio_final(algoritmo_nome, 0 /*k param placeholder*/, RODADAS_TOTAIS,
                           contador_page_faults, *contador_pag_sujas_shared);

    puts("\nTodosProcessos finalizado.");

    // Remove o segmento de memória compartilhada
    shmdt(contador_compartilhado);      /* desanexa */
    shmctl(shmid, IPC_RMID, NULL);      /* remove o segmento */
    shmdt(contador_pag_sujas_shared);
    return 0;
}

// Rotina principal de cada filho
static void rotina_filho(int id) {
    /* Abre FIFO de requisições para escrita */
    int fd_req = open("./FIFOs/gmv_req", O_WRONLY);
    if (fd_req < 0) {
        perror("open req fifo (filho)");
        _exit(EXIT_FAILURE);
    }
    /* Cria e abre FIFO de resposta exclusivo */
    char fifo_resp[64];
    snprintf(fifo_resp, sizeof(fifo_resp), "./FIFOs/gmv_resp_%d", getpid());
    mkfifo(fifo_resp, 0666);
    int fd_resp = open(fifo_resp, O_RDWR); // RDWR evita bloqueio
    if (fd_resp < 0) {
        perror("open resp fifo (filho)");
        _exit(EXIT_FAILURE);
    }

    /* abre arquivo com sequencia de acessos do processo */
    char nome_arq[32];
    snprintf(nome_arq, sizeof(nome_arq), "acessos_P%d", id + 1);
    FILE *fp_acessos = fopen(nome_arq, "r");
    if (!fp_acessos) { perror("open acessos file"); _exit(EXIT_FAILURE);}    

    char linha[16];
    int i = 0;
    while (i < QTDE_ACESSOS && fgets(linha, sizeof(linha), fp_acessos)) {
        int pagina; char operacao;
        if (sscanf(linha, "%d %c", &pagina, &operacao) != 2) continue;

        printf("Filho P%d – PID %d trabalhando | Acesso %s", id+1, getpid(), linha);
        fflush(stdout);

        /* monta requisição e envia ao GMV */
        req_t req = { .pid = getpid(), .pagina = (uint8_t)pagina, .operacao = operacao };
        write(fd_req, &req, sizeof(req));

        /* lê resposta */
        resp_t resp;
        if (read(fd_resp, &resp, sizeof(resp)) == sizeof(resp)) {
            printf("    -> quadro %d (page_fault=%d)\n", resp.quadro, resp.page_fault);
        }
        
        if (resp.page_fault == 1) {
            __sync_fetch_and_add(contador_compartilhado, 1);

            usleep(200000); // 200ms para simular o tempo de execução do GMV
        }
        fflush(stdout);

        ++i;
        sleep(1);
    }

    fclose(fp_acessos);
    close(fd_req);
    close(fd_resp);
    shmdt(contador_compartilhado);
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

/* Estrutura de utilidade para imprimir relatório final */
static void exibir_relatorio_final(const char *algoritmo, int k_param, int rodadas,
                                   int total_pf, int dirty_pages) {
    printf("\n======== Estatísticas =========\n");
    printf("Algoritmo.................: %s", algoritmo);
    if (strcmp(algoritmo, "WS") == 0) {
        printf(" (k=%d)", k_param);
    }
    printf("\nRodadas executadas........: %d\n", rodadas);
    printf("Page-faults..............: %d\n", total_pf);
    printf("Páginas sujas gravadas...: %d\n", dirty_pages);

#if 0
    /* Caso deseje exibir a sequência completa de page-faults, implemente aqui */
#endif

    /* Tabelas de página finais */
    FILE *tlog = fopen("tables.txt", "r");
    if (tlog) {
        char buf[256];
        printf("\n======== Tabelas de Página =========\n");
        while (fgets(buf, sizeof(buf), tlog)) {
            fputs(buf, stdout);
        }
        fclose(tlog);
    }
}

/* grava arquivos acessos_PX */
static void salvar_acessos_arquivos() {
    for (int i = 0; i < QTDE_FILHOS; ++i) {
        char nome[32];
        snprintf(nome, sizeof(nome), "acessos_P%d", i + 1);
        FILE *f = fopen(nome, "w");
        if (!f) { perror("fopen acessos"); continue; }
        for (int j = 0; j < QTDE_ACESSOS; ++j) {
            fprintf(f, "%s\n", paginas_filhos[i][j]);
        }
        fclose(f);
    }
}
