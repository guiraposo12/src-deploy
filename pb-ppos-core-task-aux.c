#include "ppos.h"
#include "ppos-core-globals.h"

unsigned int _systemTime;

task_t* scheduler() {
    return readyQueue; 
}

unsigned int systime() {
    return _systemTime; 
}