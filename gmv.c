#include "gmv_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// (Constantes e estruturas agora estão em gmv_proto.h)

/***************** Protótipos dos algoritmos de substituição ****************/
// Cada função deve devolver o índice do quadro escolhido para substituição
static int select_NRU(tabela_pagina_t *tabelas, int qtd_processos);
static int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos);
static int select_LRU(tabela_pagina_t *tabelas, int qtd_processos);
static int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k /* janela de working set */);

/********************* Implementações simplificadas *************************/
static int rand_quadro(void) { return rand() % QUADROS_PF; }
static int select_NRU(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos; return rand_quadro(); }
static int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos; return rand_quadro(); }
static int select_LRU(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos; return rand_quadro(); }
static int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k) {
    (void)tabelas; (void)qtd_processos; (void)k; return rand_quadro(); }

/********************* Servidor GMV via FIFO *********************************/
static const char *FIFO_DIR = "./FIFOs";
static const char *FIFO_REQ = "./FIFOs/gmv_req";
static pid_t pid_map[QTDE_FILHOS] = {0};

/* Retorna índice 0..QTDE_FILHOS-1 para o pid, -1 se excesso */
static int pid_to_index(pid_t pid) {
    for (int i = 0; i < QTDE_FILHOS; ++i) if (pid_map[i] == pid) return i;
    for (int i = 0; i < QTDE_FILHOS; ++i) if (pid_map[i] == 0) { pid_map[i] = pid; return i; }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <NRU|2nCH|LRU|WS> [k]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *algoritmo = argv[1];
    int k = (argc >= 3) ? atoi(argv[2]) : 3;
    srand(time(NULL));

    /* garante diretório de FIFOs */
    mkdir(FIFO_DIR, 0777);

    /* cria FIFO de requisições se não existir */
    mkfifo(FIFO_REQ, 0666);
    int fd_req = open(FIFO_REQ, O_RDONLY);
    if (fd_req < 0) { 
        perror("open req fifo"); 
        return EXIT_FAILURE; 
    }

    tabela_pagina_t tabelas[QTDE_FILHOS] = {0};

    printf("GMV iniciado usando algoritmo %s\n", algoritmo);

    while (1) {
        req_t req;
        ssize_t r = read(fd_req, &req, sizeof(req));
        if (r == 0) { /* EOF – reabre para novo writer */
            close(fd_req);
            fd_req = open(FIFO_REQ, O_RDONLY);
            continue;
        }
        if (r != sizeof(req)) {
            perror("read req fifo");
            continue; // leitura incompleta
        }

        int idx = pid_to_index(req.pid);
        if (idx < 0) {
            fprintf(stderr, "Processos excedem limite de %d\n", QTDE_FILHOS);
            continue;
        }

        entrada_tp_t *entry = &tabelas[idx].entradas[req.pagina];
        int page_fault = 0;
        if (!(entry->flags & BIT_PRESENCA)) {
            /* página não presente */
            int quadro;
            if (strcmp(algoritmo, "NRU") == 0)
                quadro = select_NRU(tabelas, QTDE_FILHOS);
            else if (strcmp(algoritmo, "2nCH") == 0)
                quadro = select_2nCh(tabelas, QTDE_FILHOS);
            else if (strcmp(algoritmo, "LRU") == 0)
                quadro = select_LRU(tabelas, QTDE_FILHOS);
            else
                quadro = select_WS(tabelas, QTDE_FILHOS, k);
            entry->quadro_fisico = quadro;
            entry->flags = BIT_PRESENCA;
            page_fault = 1;
        }
        entry->flags |= BIT_REFERENCIADA;
        if (req.operacao == 'W') entry->flags |= BIT_MODIFICADA;
        entry->ultimo_acesso = (uint64_t)time(NULL);

        /* Envia resposta */
        char fifo_resp[64];
        snprintf(fifo_resp, sizeof(fifo_resp), "./FIFOs/gmv_resp_%d", req.pid);
        mkfifo(fifo_resp, 0666);
        int fd_resp = open(fifo_resp, O_WRONLY);
        if (fd_resp < 0) { perror("open resp fifo"); continue; }
        resp_t resp = { .quadro = entry->quadro_fisico, .page_fault = page_fault };
        write(fd_resp, &resp, sizeof(resp));
        close(fd_resp);
    }
}
