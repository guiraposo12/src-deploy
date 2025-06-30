#include "ppos.h"
#include "ppos-core-globals.h"
#include "pb-ppos_disk.h" 
#include <signal.h>
#include <stdlib.h>
#include "disk-driver.h"

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

void disk_signal_handler(int signum) {
    disk.livre = 1;
    sem_up(&disk.work_semaphore);
}

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

void disk_mgr_body(void* arg) {
    while (1) {
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
    }
}

int disk_block_read (int block, void *buffer) {
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
    task_suspend(NULL, NULL);
    return 0;
}

int disk_block_write (int block, void *buffer) {
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
    task_suspend(NULL, NULL);
    return 0;
}

void disk_set_scheduler(int policy) {
    sem_down(&disk.semaforo);
    disk.scheduling_policy = policy;
    sem_up(&disk.semaforo);
}

void disk_head_travel_reset() {
    sem_down(&disk.semaforo);
    disk_head_travel = 0;
    sem_up(&disk.semaforo);
}

long int disk_head_travel_get() {
    return disk_head_travel;
}

int disk_get_num_blocks() {
    return disk.numBlocks;
}