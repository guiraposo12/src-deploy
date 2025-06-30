// PingPongOS - PingPong Operating System
// Prof. Carlos A. Maziero, DINF UFPR
// Versão 1.1 -- Julho de 2016

// Teste das operações de acesso a disco com múltiplas threads
// fazendo operações de leitura e escrita em disco simultâneas.

#include <stdio.h>
#include <stdlib.h>
#include "ppos.h"
#include "ppos-disk-manager.h"

#define NUMTASKS  16
#define DISK_SCHED_FCFS  0
#define DISK_SCHED_SSTF  1
#define DISK_SCHED_CSCAN 2

// tarefas
task_t mover[NUMTASKS] ;	// tarefas movedoras de blocos
int numblocks ;			// numero de blocos no disco
int blocksize ;			// tamanho de cada bloco (bytes)

// corpo das threads "mover"
void moverBody (void * arg)
{
  long myNumber ;
  int i, j, block_orig, block_dest ;
  char *buffer1, *buffer2 ;
  int current_num_blocks = disk_get_num_blocks();
  int blocksPerTask = (current_num_blocks / NUMTASKS / 2);

  buffer1 = malloc (blocksize) ;
  buffer2 = malloc (blocksize) ;

  // define os blocos iniciais
  myNumber = (long) arg ;
  block_orig = myNumber * blocksPerTask ;
  block_dest = current_num_blocks - 1 - (myNumber * blocksPerTask);

  printf ("T%02d movendo %2d blocos entre bloco %3d e bloco %3d\n",
          task_id(), blocksPerTask, block_orig, block_dest) ;

  // move blocksPerTask blocos
  for (i = 0 ; i < blocksPerTask; i++)
  {
    // le o bloco b1 do disco
    printf ("T%02d vai ler bloco %3d\n", task_id(), block_orig) ;
    if (disk_block_read (block_orig, buffer1) == 0)
      printf ("T%02d leu bloco %3d\n", task_id(), block_orig) ;
    else
      printf ("T%02d erro ao ler bloco %3d\n", task_id(), block_orig) ;

    // le o bloco b2 do disco
    printf ("T%02d vai ler bloco %3d\n", task_id(), block_dest) ;
    if (disk_block_read (block_dest, buffer2) == 0)
      printf ("T%02d leu bloco %3d\n", task_id(), block_dest) ;
    else
      printf ("T%02d erro ao ler bloco %3d\n", task_id(), block_dest) ;

    // mostra o conteudo do bloco b1
    printf ("T%02d bloco %3d tem: [", task_id(), block_orig) ;
    for (j = 0; j < blocksize; j++)
      printf ("%c", buffer1[j]) ;
    printf ("]\n") ;

    // mostra o conteudo do bloco b2
    printf ("T%02d bloco %3d tem: [", task_id(), block_dest) ;
    for (j = 0; j < blocksize; j++)
      printf ("%c", buffer2[j]) ;
    printf ("]\n") ;

    // escreve o bloco b1 no disco
    printf ("T%02d vai escrever bloco %3d\n", task_id(), block_dest) ;
    if (disk_block_write (block_dest, buffer1) == 0)
      printf ("T%02d escreveu bloco %3d\n", task_id(), block_dest) ;
    else
      printf ("T%02d erro ao escrever bloco %3d\n", task_id(), block_dest) ;

    // escreve o bloco b2 no disco
    printf ("T%02d vai escrever bloco %3d\n", task_id(), block_orig) ;
    if (disk_block_write (block_orig, buffer2) == 0)
      printf ("T%02d escreveu bloco %3d\n", task_id(), block_orig) ;
    else
      printf ("T%02d erro ao escrever bloco %3d\n", task_id(), block_orig) ;

    // define os proximos blocos
    block_orig++ ;
    block_dest-- ;
  }
  printf ("T%02d terminou\n", task_id()) ;
  free (buffer1) ;
  free (buffer2) ;
  task_exit (0) ;
}

int main (int argc, char *argv[]) {
    long i;
    unsigned int startTime, endTime;

    printf("main: inicio dos testes de escalonamento de disco\n");

    ppos_init();

    // Inicializa o gerente de disco UMA VEZ
    int numblocks, blocksize;
    if (disk_mgr_init(&numblocks, &blocksize) < 0) {
        printf("Erro na abertura do disco\n");
        exit(1);
    }
    printf("Disco contem %d blocos de %d bytes cada\n\n", numblocks, blocksize);

    // ==================================================================
    //                           TESTE COM FCFS
    // ==================================================================
    printf("--- INICIANDO TESTE COM FCFS ---\n");
    disk_set_scheduler(DISK_SCHED_FCFS);
    disk_head_travel_reset();
    startTime = systime();

    // Bloco de código original do pingpong-disco2 para criar e aguardar tarefas
    task_t mover[NUMTASKS];
    for (i = 0; i < NUMTASKS; i++)
        task_create(&mover[i], moverBody, (long*) i);
    for (i = 0; i < NUMTASKS; i++)
        task_join(&mover[i]);
    
    endTime = systime();
    printf("\nResultados FCFS:\n");
    printf("  -> Tempo total: %u ms\n", endTime - startTime);
    printf("  -> Blocos percorridos: %ld\n\n", disk_head_travel_get());

    // ==================================================================
    //                           TESTE COM SSTF
    // ==================================================================
    printf("--- INICIANDO TESTE COM SSTF ---\n");
    // Atenção: O teste do disco2 inverte os blocos. Para um segundo teste justo,
    // seria ideal reiniciar o estado do disco, mas para este projeto, 
    // podemos apenas rodar na sequência e observar a diferença.
    disk_set_scheduler(DISK_SCHED_SSTF);
    disk_head_travel_reset();
    startTime = systime();

    // Roda a mesma carga de trabalho novamente
    for (i = 0; i < NUMTASKS; i++)
        task_create(&mover[i], moverBody, (long*) i);
    for (i = 0; i < NUMTASKS; i++)
        task_join(&mover[i]);

    endTime = systime();
    printf("\nResultados SSTF:\n");
    printf("  -> Tempo total: %u ms\n", endTime - startTime);
    printf("  -> Blocos percorridos: %ld\n\n", disk_head_travel_get());

    // ==================================================================
    //                       TESTE COM CSCAN (similar)
    // ==================================================================
    // ... (repetir a mesma estrutura para CSCAN) ...

    printf("main: fim dos testes\n");
    task_exit(0);
    exit(0);
}
