// PingPongOS - PingPong Operating System
// Prof. Carlos A. Maziero, DINF UFPR
// Versão 1.2 -- Julho de 2017

// interface do gerente de disco rígido (block device driver)

#ifndef __DISK_MGR__
#define __DISK_MGR__
#include "ppos-data.h" 

#define DISK_SCHED_FCFS  0
#define DISK_SCHED_SSTF  1
#define DISK_SCHED_CSCAN 2

//#define DEBUG_DISK 1

// estruturas de dados e rotinas de inicializacao e acesso
// a um dispositivo de entrada/saida orientado a blocos,
// tipicamente um disco rigido.

// structura de dados que representa um pedido de leitura/escrita ao disco
typedef struct diskrequest_t {
    struct diskrequest_t* next;
    struct diskrequest_t* prev;

    task_t* task;
    unsigned char operation; // DISK_REQUEST_READ ou DISK_REQUEST_WRITE
    int block;
    void* buffer;
} diskrequest_t;

// estrutura que representa um disco no sistema operacional
// structura de dados que representa o disco para o SO
typedef struct {
    int numBlocks;
    int blockSize;
    semaphore_t semaforo;
    unsigned char livre;
    diskrequest_t* requestQueue;
    semaphore_t work_semaphore;
    int head_pos;          
    int scheduling_policy; 
} disk_t;

// inicializacao do gerente de disco
// retorna -1 em erro ou 0 em sucesso
// numBlocks: tamanho do disco, em blocos
// blockSize: tamanho de cada bloco do disco, em bytes
int disk_mgr_init (int *numBlocks, int *blockSize) ;

// leitura de um bloco, do disco para o buffer
int disk_block_read (int block, void *buffer) ;

// escrita de um bloco, do buffer para o disco
int disk_block_write (int block, void *buffer) ;

// escalonador de requisições do disco
diskrequest_t* disk_scheduler();

void disk_set_scheduler(int policy);      
void disk_head_travel_reset();          
long int disk_head_travel_get();   
int disk_get_num_blocks();     

#endif