#include "ppos.h"
#include "ppos-core-globals.h"
#include "ppos-disk-manager.h"
#include <signal.h>
#include <stdlib.h>
#include "disk-driver.h"
#include <string.h>

// ****************************************************************************
// Adicione TUDO O QUE FOR NECESSARIO para realizar o seu trabalho
// Coloque as suas modificações aqui, 
// p.ex. includes, defines variáveis, 
// estruturas e funções
//
// ****************************************************************************

unsigned int _systemTime;
static disk_t disk;
static task_t disk_mgr_task;
static diskrequest_t* current_request; 
static long int disk_head_travel = 0;

void disk_signal_handler();
void disk_mgr_body();

int disk_mgr_init (int *numBlocks, int *blockSize) {
    if (disk_cmd (DISK_CMD_INIT, 0, 0) < 0) {
        perror("Erro ao inicializar o disco");
        return -1;
    }
    
    disk.numBlocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    disk.blockSize = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);

    if (disk.numBlocks <= 0 || disk.blockSize <= 0) {
        perror("Erro ao obter parâmetros do disco");
        return -1;
    }

    *numBlocks = disk.numBlocks;
    *blockSize = disk.blockSize;

    disk.requestQueue = NULL;
    disk.livre = 1;
    disk.head_pos = 0;
    disk.scheduling_policy = DISK_SCHED_FCFS;
    disk_head_travel = 0;

    if (sem_create(&disk.semaforo, 1) != 0) {
        perror("Erro ao criar semáforo do disco");
        return -1;
    }

    if (sem_create(&disk.work_semaphore, 0) != 0) {
        perror("Erro ao criar semáforo de trabalho do disco");
        return -1;
    }

    task_create(&disk_mgr_task, disk_mgr_body, NULL);

    struct sigaction action;
    action.sa_handler = disk_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGUSR1, &action, 0) < 0) {
        perror("Erro ao registrar signal handler para o disco");
        return -1;
    }
    return 0;
}

// Tratador de sinal para o sinal SIGUSR1, enviado pelo disco
void disk_signal_handler(int signum) {
    disk.livre = 1;               
    sem_up(&disk.work_semaphore);  
}

// O escalonador de disco
diskrequest_t* disk_scheduler() {
    diskrequest_t* next_request = NULL;
    if (!disk.requestQueue) return NULL;

    switch (disk.scheduling_policy) {
        case DISK_SCHED_SSTF: {
            diskrequest_t* best_request = (diskrequest_t*)disk.requestQueue;
            int min_distance = abs(best_request->block - disk.head_pos);
            diskrequest_t* current_item = best_request->next;
            while(current_item != (diskrequest_t*)disk.requestQueue) {
                int distance = abs(current_item->block - disk.head_pos);
                if (distance < min_distance) {
                    min_distance = distance;
                    best_request = current_item;
                }
                current_item = current_item->next;
            }
            next_request = best_request;
        } break;
        case DISK_SCHED_CSCAN: {
            diskrequest_t* current_item = (diskrequest_t*)disk.requestQueue;
            diskrequest_t* best_fwd = NULL;
            int min_dist_fwd = -1;
            diskrequest_t* best_wrap = NULL;
            int min_block_wrap = -1;
            do {
                if (current_item->block >= disk.head_pos) {
                    int dist = current_item->block - disk.head_pos;
                    if (min_dist_fwd == -1 || dist < min_dist_fwd) {
                        min_dist_fwd = dist;
                        best_fwd = current_item;
                    }
                }
                if (min_block_wrap == -1 || current_item->block < min_block_wrap) {
                    min_block_wrap = current_item->block;
                    best_wrap = current_item;
                }
                current_item = current_item->next;
            } while (current_item != (diskrequest_t*)disk.requestQueue);

            if (best_fwd) next_request = best_fwd;
            else next_request = best_wrap;
        } break;
        case DISK_SCHED_FCFS:
        default:
            next_request = (diskrequest_t*)disk.requestQueue;
            break;
    }
    return next_request;
}

// Corpo da tarefa gerenciadora de disco
void disk_mgr_body(void* arg) {
    while (1) {
        fflush(stdout);
        sem_down(&disk.work_semaphore);
        sem_down(&disk.semaforo);

        if (current_request && disk.livre) {
            task_resume(current_request->task);
            free(current_request);             
            current_request = NULL;
        }

        if (disk.livre && queue_size((queue_t*)disk.requestQueue) > 0) {
            diskrequest_t* next_req_ptr = disk_scheduler();
            if (next_req_ptr) {
                current_request = (diskrequest_t*)queue_remove((queue_t**)&disk.requestQueue, (queue_t*)next_req_ptr);
                disk.livre = 0;
                fflush(stdout);
                int distance = abs(disk.head_pos - current_request->block);
                disk_head_travel += distance;
                disk.head_pos = current_request->block;
                disk_cmd(current_request->operation, current_request->block, current_request->buffer);
            }
        }

        if (taskMain->state == PPOS_TASK_STATE_TERMINATED && queue_size((queue_t*)disk.requestQueue) == 0) {
            sem_up(&disk.semaforo); 
            task_exit(0);           
        }

        sem_up(&disk.semaforo);
        fflush(stdout);
        task_suspend(NULL, NULL);
    }
}

// API de leitura de bloco do disco
int disk_block_read (int block, void *buffer) {
    fflush(stdout);
    diskrequest_t* request = malloc(sizeof(diskrequest_t));
    if (!request) return -1;
    request->operation = DISK_CMD_READ;
    request->block = block;
    request->buffer = buffer;
    request->task = taskExec;
    sem_down(&disk.semaforo);
    queue_append((queue_t**)&disk.requestQueue, (queue_t*)request);
    sem_up(&disk.semaforo);
    sem_up(&disk.work_semaphore);
    task_suspend(taskExec, NULL);
    return 0;
}

// API de escrita de bloco no disco
int disk_block_write (int block, void *buffer) {
    fflush(stdout);
    diskrequest_t* request = malloc(sizeof(diskrequest_t));
    if (!request) return -1;
    request->operation = DISK_CMD_WRITE;
    request->block = block;
    request->buffer = buffer;
    request->task = taskExec;
    sem_down(&disk.semaforo);
    queue_append((queue_t**)&disk.requestQueue, (queue_t*)request);
    sem_up(&disk.semaforo);
    sem_up(&disk.work_semaphore);
    task_suspend(taskExec, NULL);
    return 0;
}

// Define a política de escalonamento a ser usada
void disk_set_scheduler(int policy) {
    sem_down(&disk.semaforo);
    disk.scheduling_policy = policy;
    sem_up(&disk.semaforo);
}

// Zera o contador de distância percorrida
void disk_head_travel_reset() {
    sem_down(&disk.semaforo);
    disk_head_travel = 0;
    sem_up(&disk.semaforo);
}

// Retorna o total de distância percorrida
long int disk_head_travel_get() {
    return disk_head_travel;
}

task_t* scheduler() {
    return readyQueue; 
}

// Implementação mínima de systime()
unsigned int systime() {
    return _systemTime; 
}

int disk_get_num_blocks() {
    return disk.numBlocks;
}

void before_ppos_init () {
    char* policy_str = getenv("PPOS_SCHEDULER");

    if (policy_str) { 
        if (strcmp(policy_str, "SSTF") == 0) {
            disk_set_scheduler(DISK_SCHED_SSTF);
        } else if (strcmp(policy_str, "CSCAN") == 0) {
            disk_set_scheduler(DISK_SCHED_CSCAN);
        }
    }
#ifdef DEBUG
    printf("\ninit - BEFORE");
#endif
}

void after_ppos_init () {
    // put your customization here
#ifdef DEBUG
    printf("\ninit - AFTER");
#endif
    
}

void before_task_create (task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_create - BEFORE - [%d]", task->id);
#endif
}

void after_task_create (task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_create - AFTER - [%d]", task->id);
#endif
    
}

void before_task_exit () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_exit - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_exit () {
    if (taskExec->id == 0) {
        long int final_travel = disk_head_travel_get();
        unsigned int final_time = systime();

        char* policy_name;
        switch(disk.scheduling_policy) {
            case DISK_SCHED_SSTF:
                policy_name = "SSTF";
                break;
            case DISK_SCHED_CSCAN:
                policy_name = "CSCAN";
                break;
            default:
                policy_name = "FCFS";
                break;
        }
        printf("  Relatorio de Desempenho do Disco:\n");
        printf("  Politica Executada: %s\n", policy_name);
        printf("  -> Tempo total de execucao: %u ms\n", final_time);
        printf("  -> Distancia total percorrida: %ld blocos\n", final_travel);
    }
}

void before_task_switch ( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_switch - BEFORE - [%d -> %d]", taskExec->id, task->id);
#endif
    
}

void after_task_switch ( task_t *task ) {
    _systemTime++;
#ifdef DEBUG
    printf("\ntask_switch - AFTER - [%d -> %d]", taskExec->id, task->id);
#endif
}

void before_task_yield () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_yield - BEFORE - [%d]", taskExec->id);
#endif
}
void after_task_yield () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_yield - AFTER - [%d]", taskExec->id);
#endif
}


void before_task_suspend( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_suspend - BEFORE - [%d]", task->id);
#endif
}

void after_task_suspend( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_suspend - AFTER - [%d]", task->id);
#endif
}

void before_task_resume(task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_resume - BEFORE - [%d]", task->id);
#endif
}

void after_task_resume(task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_resume - AFTER - [%d]", task->id);
#endif
}

void before_task_sleep () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_sleep - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_sleep () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_sleep - AFTER - [%d]", taskExec->id);
#endif
}

int before_task_join (task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_task_join (task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}


int before_sem_create (semaphore_t *s, int value) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_create (semaphore_t *s, int value) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_down (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_down - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_down (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_down - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_up (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_up - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_up (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_up - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_destroy (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_destroy (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_create (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_create (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_lock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_lock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_lock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_lock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_unlock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_unlock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_unlock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_unlock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_destroy (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_destroy (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_create (barrier_t *b, int N) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_create (barrier_t *b, int N) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_join (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_join (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_destroy (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_destroy (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_create (mqueue_t *queue, int max, int size) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_create (mqueue_t *queue, int max, int size) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_send (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_send - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_send (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_send - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_recv (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_recv - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_recv (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_recv - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_destroy (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_destroy (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_msgs (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_msgs - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_msgs (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_msgs - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}
