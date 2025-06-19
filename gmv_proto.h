/* gmv_proto.h – Estruturas e constantes comuns entre GMV e processos filhos */
#ifndef GMV_PROTO_H
#define GMV_PROTO_H

#include <stdint.h>
#include <unistd.h>

/**************** Limites do sistema ****************/
#define ENTRADAS_TP 32      // páginas lógicas por processo
#define QUADROS_PF  16      // quadros físicos de página
#define QTDE_FILHOS 4       // processos no simulador

/**************** Bits de estado da página **********/
#define BIT_PRESENCA      0x1
#define BIT_REFERENCIADA  0x2
#define BIT_MODIFICADA    0x4

/**************** Estruturas da tabela de páginas ****/
typedef struct {
    uint32_t quadro_fisico;   // índice do quadro físico que contém a página
    uint8_t  flags;           // combinação de BIT_*
    uint64_t ultimo_acesso;   // timestamp ou contador
    uint32_t idade;           // usado por algoritmos de aging
} entrada_tp_t;

typedef struct {
    entrada_tp_t entradas[ENTRADAS_TP];
} tabela_pagina_t;

/**************** Protocolo FIFO ********************/
/* Pedido que um processo envia ao GMV */
typedef struct {
    pid_t   pid;      // pid do solicitante
    uint8_t pagina;   // número da página (0-31)
    char    operacao; // 'R' ou 'W'
} req_t;

/* Resposta que o GMV devolve */
typedef struct {
    int quadro;       // quadro físico fornecido (0-15)
    int page_fault;   // 0 = hit, 1 = page fault tratado
} resp_t;

#endif /* GMV_PROTO_H */ 