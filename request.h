#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "queue.h"
#include <sys/time.h>
#include <pthread.h>

typedef struct threadStats {
    pthread_t ourThread;
    int id;
    int stat_req;
    int dynm_req;
    int total_req;
    
} threadStats;

// Called by your threads to handle a request
void requestHandle(int fd, Node node, threadStats *t_stats);

Node skip_request(threadStats* thread);

#endif
