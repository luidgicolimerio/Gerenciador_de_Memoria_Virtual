// Bibliotecas
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Constantes que descrevem o ambiente simulado
#define ENTRADAS_TP 32      // páginas lógicas por processo
#define QUADROS_PF  16      // quadros de página físicos disponíveis na RAM

// Mantém compatibilidade com nomes anteriores (se necessário em outros arquivos)
#define PAGE_TABLE_ENTRIES ENTRADAS_TP
#define PAGE_FRAME_COUNT   QUADROS_PF

// Bits da palavra de status em cada entrada da Tabela de Páginas
#define BIT_PRESENCA      0x1
#define BIT_REFERENCIADA  0x2
#define BIT_MODIFICADA    0x4

// TODO:Verificar se esse é o formato correto para a tabela de páginas
// Representação genérica de uma entrada da Tabela de Páginas
typedef struct {
    uint32_t quadro_fisico;   // índice do quadro físico que contém a página
    uint8_t  flags;           // combinação de BIT_* acima
    uint64_t ultimo_acesso;   // timestamp/contador usado por LRU/Aging
    uint32_t idade;           // campo extra para NRU/Aging
} entrada_tp_t;

// Cada processo possui uma Tabela de Páginas completa
typedef struct {
    entrada_tp_t entradas[ENTRADAS_TP];
} tabela_pagina_t;

/***************** Protótipos dos algoritmos de substituição ****************/
// Cada função deve devolver o índice do quadro escolhido para substituição
int select_NRU(tabela_pagina_t *tabelas, int qtd_processos);
int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos);
int select_LRU(tabela_pagina_t *tabelas, int qtd_processos);
int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k /* janela de working set */);

/********************* Implementações (stubs por enquanto) ******************/
int select_NRU(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos;
    printf("select_NRU() ainda não implementado.\n");
    return -1;
}

int select_2nCh(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos;
    printf("select_2nCh() ainda não implementado.\n");
    return -1;
}

int select_LRU(tabela_pagina_t *tabelas, int qtd_processos) {
    (void)tabelas; (void)qtd_processos;
    printf("select_LRU() ainda não implementado.\n");
    return -1;
}

int select_WS(tabela_pagina_t *tabelas, int qtd_processos, int k) {
    (void)tabelas; (void)qtd_processos; (void)k;
    printf("select_WS(k=%d) ainda não implementado.\n", k);
    return -1;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <NRU|2nCH|LRU|WS> [k]\n", argv[0]);
        return EXIT_FAILURE;
    }
    // TODO: Verificar se sempre vai precisar passar o k mesmo que não seja WS
    const char *algoritmo = argv[1];
    int k = 3; // valor padrão da janela para WS
    if (argc >= 3) {
        k = atoi(argv[2]);
        if (k <= 0) {
            fprintf(stderr, "k deve ser maior que 0.\n");
            return EXIT_FAILURE;
        }
    }

    // Simula um conjunto de tabelas de páginas — aqui apenas alocamos espaço
    const int QTD_PROCESSOS = 4;
    tabela_pagina_t *tabelas = calloc(QTD_PROCESSOS, sizeof(tabela_pagina_t));
    if (!tabelas) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    int quadro_escolhido = -1;

    if (strcmp(algoritmo, "NRU") == 0) {
        quadro_escolhido = select_NRU(tabelas, QTD_PROCESSOS);
    } else if (strcmp(algoritmo, "2nCH") == 0) {
        quadro_escolhido = select_2nCh(tabelas, QTD_PROCESSOS);
    } else if (strcmp(algoritmo, "LRU") == 0) {
        quadro_escolhido = select_LRU(tabelas, QTD_PROCESSOS);
    } else if (strcmp(algoritmo, "WS") == 0) {
        quadro_escolhido = select_WS(tabelas, QTD_PROCESSOS, k);
    } else {
        fprintf(stderr, "Algoritmo desconhecido: %s\n", algoritmo);
        free(tabelas);
        return EXIT_FAILURE;
    }

    if (quadro_escolhido >= 0)
        printf("Quadro selecionado pelo algoritmo %s: %d\n", algoritmo, quadro_escolhido);

    free(tabelas);
    return EXIT_SUCCESS;
}
