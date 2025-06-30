// PingPongOS - PingPong Operating System
// Prof. Carlos A. Maziero, DINF UFPR
// Versão 1.1 -- Julho de 2016

// Teste do task dispatcher e escalonador FCFS

#include <stdio.h>
#include <stdlib.h>
#include "ppos.h"
//#include "ppos-core-globals.h"

task_t Pang, Peng, Ping, Pong, Pung ;
task_t *pPang;

// corpo das threads
void Body (void * arg)
{
   int i ;

   printf ("%s: inicio\n", (char *) arg) ;
   for (i=0; i<5; i++)
   {
      printf ("%s: %d\n", (char *) arg, i) ;
      task_yield ();
   }
   printf ("%s: fim\n", (char *) arg) ;
   task_exit (0) ;
}

int main (int argc, char *argv[])
{
   printf ("main: inicio\n");

   ppos_init () ;

   // Pang = (task_t*)malloc(sizeof(task_t));
   // Peng = (task_t*)malloc(sizeof(task_t));
   // Ping = (task_t*)malloc(sizeof(task_t));
   // Pong = (task_t*)malloc(sizeof(task_t));
   // Pung = (task_t*)malloc(sizeof(task_t));
   pPang = malloc(sizeof(task_t));
   task_create (pPang, Body, "    Pang") ;
   task_create (&Peng, Body, "        Peng") ;
   task_create (&Ping, Body, "            Ping") ;
   task_create (&Pong, Body, "                Pong") ;
   task_create (&Pung, Body, "                    Pung") ;

/*
   printf("\n ========= SCHEDULER FORA");
   task_t *aux = &Pung;
   printf("\n ========= %x \n", aux);
   print_tcb (aux);
   aux = (task_t*)scheduler();
   printf("\n ========= %x \n", aux);
   print_tcb (aux);
   printf("\n ========= SCHEDULER FORA ================");
*/

   task_yield () ;

   printf ("main: fim\n");
   exit (0);
}
