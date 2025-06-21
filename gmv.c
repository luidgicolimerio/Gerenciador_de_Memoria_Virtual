#include "gmv_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define NUM_QUADROS QUADROS_PF
#define BIT_R BIT_REFERENCIADA

/* intervalo para zerar bits R (em acessos) */
#define REF_CLEAR_INTERVAL 20

typedef struct {
    bool ocupado;
    int processo_id;       // índice do processo proprietário
    uint8_t pagina_virtual;
} quadro_t;

static quadro_t memoria_fisica[NUM_QUADROS];
static uint64_t tempo_global = 0;
static int *contador_paginas_sujas_ptr = NULL;   // ponteiro para contador em memória compartilhada
#define INC_PAG_SUJAS() do { if (contador_paginas_sujas_ptr) (*(contador_paginas_sujas_ptr))++; } while(0)


/***************** Protótipos dos algoritmos de substituição ****************/
// Cada função deve devolver o índice do quadro escolhido para substituição
static int select_NRU(tabela_pagina_t *tabelas, int qtd_processos);
static int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos);
static int select_LRU(tabela_pagina_t *tabelas, int qtd_processos);
static int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k /* janela de working set */);

/********************* Implementações simplificadas *************************/
static int rand_quadro(void) { return rand() % NUM_QUADROS; }
static int select_NRU(tabela_pagina_t *tabelas, int qtd_processos) {
    //Resolvido - visto
    int candidatos[4] = {-1, -1, -1, -1};

    /* procura quadro livre imediatamente */
    for (int i = 0; i < NUM_QUADROS; i++) {
        if (!memoria_fisica[i].ocupado)
            return i;
    }

    /* Classifica quadros ocupados nas 4 classes NRU */
    for (int i = 0; i < NUM_QUADROS; i++) {
        quadro_t *q = &memoria_fisica[i]; // q de quadro da memória RAM
        entrada_tp_t *e = &tabelas[q->processo_id].entradas[q->pagina_virtual];
        uint8_t f = e->flags;
        int classe;
        if ((f & BIT_REFERENCIADA) == 0 && (f & BIT_MODIFICADA) == 0)        classe = 0;
        else if ((f & BIT_REFERENCIADA) == 0 && (f & BIT_MODIFICADA))        classe = 1;
        else if ((f & BIT_REFERENCIADA) && (f & BIT_MODIFICADA) == 0)        classe = 2;
        else                                                                 classe = 3;
        if (candidatos[classe] == -1)
            candidatos[classe] = i;
    }

    /* devolve o primeiro candidato de menor classe disponível */
    for (int c = 0; c < 4; c++){
        if (candidatos[c] != -1){
            return candidatos[c];
        }
    }
    /* fallback improvável */
    printf("NRU: fallback improvável\n");
    return rand_quadro();
}
static int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos) {
    // Resolvido - visto
    static int ponteiro = 0;

    for (int tentativas = 0; tentativas < NUM_QUADROS * 2; tentativas++) {
        int idx = ponteiro % NUM_QUADROS;
        quadro_t *q = &memoria_fisica[idx];

        if (!q->ocupado) return idx; // Se o quadro atual não estiver ocupado, retorna o índice do quadro

        entrada_tp_t *e = &tabelas[q->processo_id].entradas[q->pagina_virtual];

        if ((e->flags & BIT_R) == 0){ // Se o bit R não estiver setado, retorna o índice do quadro
            return idx;
        } else {
            e->flags &= ~BIT_R; // Se o bit R estiver setado, limpa o bit R
        }

        ponteiro = (ponteiro + 1) % NUM_QUADROS;
    }
    return ponteiro % NUM_QUADROS; 
}
static int select_LRU(tabela_pagina_t *tabelas, int qtd_processos) {
    // Resolvido - visto

    uint64_t menor_tempo = UINT64_MAX;
    int indice_vitima = -1;

    for (int i = 0; i < NUM_QUADROS; i++) {
        if (!memoria_fisica[i].ocupado) return i;

        quadro_t *q = &memoria_fisica[i];
        entrada_tp_t *e = &tabelas[q->processo_id].entradas[q->pagina_virtual];

        if (e->ultimo_acesso < menor_tempo) {
            menor_tempo = e->ultimo_acesso;
            indice_vitima = i;
        }
    }

    return indice_vitima;
}
static int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k) {
    // Implementação com ponteiro circular para distribuir as vítimas.
    static int ponteiro = 0;

    uint64_t limite = tempo_global - k; // fronteira da janela k

    int indice_fora_ws = -1;     // primeiro quadro fora do WS encontrado na varredura
    int indice_mais_antigo = -1; // fallback LRU caso todos estejam no WS
    uint64_t mais_antigo = UINT64_MAX;

    for (int passo = 0; passo < NUM_QUADROS; ++passo) {
        int i = (ponteiro + passo) % NUM_QUADROS;

        if (!memoria_fisica[i].ocupado) {
            ponteiro = (i + 1) % NUM_QUADROS;
            return i; // quadro livre encontrado
        }

        quadro_t *q = &memoria_fisica[i];
        entrada_tp_t *e = &tabelas[q->processo_id].entradas[q->pagina_virtual];

        if (e->ultimo_acesso < limite && indice_fora_ws == -1) {
            indice_fora_ws = i; // marca o primeiro fora do WS, mas continua a varredura para completar uma volta
        }

        if (e->ultimo_acesso < mais_antigo) {
            mais_antigo = e->ultimo_acesso;
            indice_mais_antigo = i;
        }
    }

    // Atualiza ponteiro para próximo quadro após a vítima escolhida
    int escolhido = (indice_fora_ws != -1) ? indice_fora_ws : indice_mais_antigo;
    ponteiro = (escolhido + 1) % NUM_QUADROS;
    return escolhido;
}

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

static void limpa_bits_referencia(tabela_pagina_t *tabelas, int qtd_proc) {
    for (int p = 0; p < qtd_proc; ++p)
        for (int i = 0; i < ENTRADAS_TP; ++i)
            tabelas[p].entradas[i].flags &= ~BIT_REFERENCIADA;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <NRU|2nCH|LRU|WS> [k]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *algoritmo = argv[1];
    int k = (argc >= 3) ? atoi(argv[2]) : 3;
    srand(time(NULL));

    /* configura memória compartilhada para contador de páginas sujas */
    key_t shm_key_dp = ftok("/tmp", 'D');
    if (shm_key_dp == -1) { perror("ftok"); return EXIT_FAILURE; }
    int shm_id_dp = shmget(shm_key_dp, sizeof(int), IPC_CREAT | 0666);
    if (shm_id_dp == -1) { perror("shmget"); return EXIT_FAILURE; }
    contador_paginas_sujas_ptr = shmat(shm_id_dp, NULL, 0);
    if (contador_paginas_sujas_ptr == (void *)-1) { perror("shmat"); return EXIT_FAILURE; }
    *contador_paginas_sujas_ptr = 0;


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

        tempo_global++;
        if (tempo_global % REF_CLEAR_INTERVAL == 0)
            limpa_bits_referencia(tabelas, QTDE_FILHOS);

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
            /* se o quadro já estiver ocupado, limpa mapeamento antigo */
            if (memoria_fisica[quadro].ocupado) {
                entrada_tp_t *vict = &tabelas[memoria_fisica[quadro].processo_id]
                                              .entradas[memoria_fisica[quadro].pagina_virtual];
                vict->flags &= ~BIT_PRESENCA;
                if (vict->flags & BIT_MODIFICADA){
                    INC_PAG_SUJAS();
                }
            }
            memoria_fisica[quadro].ocupado = true;
            memoria_fisica[quadro].processo_id = idx;
            memoria_fisica[quadro].pagina_virtual = req.pagina;
            entry->quadro_fisico = quadro;
            entry->flags = BIT_PRESENCA;
            page_fault = 1;
        }
        entry->flags |= BIT_REFERENCIADA;
        if (req.operacao == 'W') entry->flags |= BIT_MODIFICADA;
        entry->ultimo_acesso = tempo_global;

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
