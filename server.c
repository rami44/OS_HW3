#include "segel.h"
#include "request.h"

#define MAX_POLICY 7

// Queues for VIP and Regular requests
List vip_requests = NULL;
List running_requests = NULL;
List waiting_requests = NULL;

// Synchronization primitives
pthread_cond_t empty_queue;
pthread_cond_t vip_allowed;
pthread_cond_t read_allowed;
pthread_cond_t write_allowed;
pthread_mutex_t global_lock;

// A flag indicating the VIP thread is actively working.
// While vip_is_busy = 1, no regular thread is allowed to start a request.
static int vip_is_busy = 0;

// --------------------------------------------------
// VIP Thread Function
// --------------------------------------------------
void *VIPThreadFunction(void *args)
{
    threadStats *threadStruct = (threadStats *)args;

    while (1) {
        pthread_mutex_lock(&global_lock);

        // Wait if no VIP requests
        while (getSize(vip_requests) == 0) {
            pthread_cond_wait(&vip_allowed, &global_lock);
        }
        vip_is_busy = 1;

        // Take next VIP request
        Node toWorkWith = removeFront(vip_requests);
        append(running_requests, toWorkWith, threadStruct->id);

        pthread_mutex_unlock(&global_lock);

        // Handle request
        requestHandle(getValue(toWorkWith), toWorkWith, threadStruct);
        Close(getValue(toWorkWith));

        // Cleanup
        pthread_mutex_lock(&global_lock);
        removeByValue(running_requests, getValue(toWorkWith));
        vip_is_busy = 0;

        // Freed a slot
        pthread_cond_broadcast(&write_allowed);

        // If all empty, signal empty_queue
        if ( (getSize(running_requests) == 0) &&
             (getSize(waiting_requests) == 0) &&
             (getSize(vip_requests) == 0) ) {
            pthread_cond_signal(&empty_queue);
        }
        // Wake regular threads
        pthread_cond_broadcast(&read_allowed);

        pthread_mutex_unlock(&global_lock);
    }
    return NULL;
}

// --------------------------------------------------
// Regular Thread Function
// --------------------------------------------------
void *ThreadFunction(void *args)
{
    threadStats *threadStruct = (threadStats *)args;

    while (1) {
        pthread_mutex_lock(&global_lock);

        // Wait if:
        // 1) No regular requests
        // 2) VIP queue non-empty
        // 3) VIP busy
        while ( (getSize(waiting_requests) == 0) ||
                (getSize(vip_requests) > 0) ||
                (vip_is_busy == 1) )
        {
            if (getSize(vip_requests) > 0) {
                pthread_cond_wait(&vip_allowed, &global_lock);
            } else {
                pthread_cond_wait(&read_allowed, &global_lock);
            }
        }

        // Dequeue oldest regular request
        Node toWorkWith = removeFront(waiting_requests);
        append(running_requests, toWorkWith, threadStruct->id);

        pthread_mutex_unlock(&global_lock);

        // Handle request
        requestHandle(getValue(toWorkWith), toWorkWith, threadStruct);
        Close(getValue(toWorkWith));

        // Cleanup
        pthread_mutex_lock(&global_lock);
        removeByValue(running_requests, getValue(toWorkWith));
        pthread_cond_signal(&write_allowed);

        if ( (getSize(running_requests) == 0) &&
             (getSize(waiting_requests) == 0) &&
             (getSize(vip_requests) == 0) )
        {
            pthread_cond_signal(&empty_queue);
        }
        pthread_mutex_unlock(&global_lock);
    }
    return NULL;
}

// --------------------------------------------------
// Parse command-line arguments
// --------------------------------------------------
void getArguments(int *port, int *threadsNum, int *poolSize,
                  char *schedAlg, int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <portnum> <threads> <queue_size> <schedalg>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threadsNum = atoi(argv[2]);
    if (*threadsNum <= 0) {
        fprintf(stderr, "Error: #threads must be positive.\n");
        exit(1);
    }
    *poolSize = atoi(argv[3]);
    if (*poolSize <= 0) {
        fprintf(stderr, "Error: queue size must be positive.\n");
        exit(1);
    }

    strcpy(schedAlg, argv[4]);
    if (strcmp(schedAlg, "block") != 0 &&
        strcmp(schedAlg, "dt")    != 0 &&
        strcmp(schedAlg, "dh")    != 0 &&
        strcmp(schedAlg, "bf")    != 0 &&
        strcmp(schedAlg, "random")!= 0)
    {
        fprintf(stderr, "Error: Unknown scheduling algorithm: %s\n", schedAlg);
        exit(1);
    }
}

// --------------------------------------------------
// Initialize threads: <num> regular + 1 VIP
// --------------------------------------------------
void initializeThreads(int num, threadStats *threadsArr, pthread_t *vipThread)
{
    for (int i = 0; i < num; i++) {
        threadsArr[i].id        = i;
        threadsArr[i].dynm_req  = 0;
        threadsArr[i].stat_req  = 0;
        threadsArr[i].total_req = 0;

        pthread_create(&threadsArr[i].ourThread, NULL,
                       ThreadFunction, (void *)&threadsArr[i]);
    }

    // VIP in [num]
    threadsArr[num].id        = num;
    threadsArr[num].dynm_req  = 0;
    threadsArr[num].stat_req  = 0;
    threadsArr[num].total_req = 0;

    pthread_create(vipThread, NULL, VIPThreadFunction, (void *)&threadsArr[num]);
}

// --------------------------------------------------
// main()
// --------------------------------------------------
int main(int argc, char *argv[])
{
    int listenfd, connfd, clientlen;
    struct sockaddr_in clientaddr;
    int port, threadNum, poolSize;
    char schedAlg[MAX_POLICY];

    getArguments(&port, &threadNum, &poolSize, schedAlg, argc, argv);

    // init queues
    vip_requests     = queueConstructor();
    running_requests = queueConstructor();
    waiting_requests = queueConstructor();

    // init sync
    pthread_cond_init(&empty_queue, NULL);
    pthread_cond_init(&vip_allowed, NULL);
    pthread_cond_init(&read_allowed, NULL);
    pthread_cond_init(&write_allowed, NULL);
    pthread_mutex_init(&global_lock, NULL);

    // thread array
    threadStats *threadArr = (threadStats *)malloc(sizeof(threadStats)*(threadNum+1));
    pthread_t vipThread;

    // create threads
    initializeThreads(threadNum, threadArr, &vipThread);

    listenfd = Open_listenfd(port);
    srand(time(NULL)); // for random dropping

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);

        // arrival time
        struct timeval arrival_time;
        gettimeofday(&arrival_time, NULL);

        pthread_mutex_lock(&global_lock);

        int isVIP = getRequestMetaData(connfd);
        if (isVIP) {
            // VIP
            while ( (getSize(running_requests) +
                     getSize(waiting_requests) +
                     getSize(vip_requests)) >= poolSize )
            {
                pthread_cond_wait(&write_allowed, &global_lock);
            }
            appendNewRequest(vip_requests, connfd, arrival_time);
            pthread_cond_signal(&vip_allowed);
        } else {
            // Regular
            if ( (getSize(running_requests) + getSize(waiting_requests)) == poolSize ) {
                // Overloaded => apply schedAlg
                if (strcmp(schedAlg, "block") == 0) {
                    while ((getSize(running_requests) + getSize(waiting_requests)) == poolSize) {
                        pthread_cond_wait(&write_allowed, &global_lock);
                    }
                }
                else if (strcmp(schedAlg, "dt") == 0) {
                    // drop tail => close new
                    Close(connfd);
                    pthread_mutex_unlock(&global_lock);
                    continue;
                }
                else if (strcmp(schedAlg, "dh") == 0) {
                    // drop head => remove oldest from waiting
                    if (getSize(waiting_requests) > 0) {
                        Node oldest = removeFront(waiting_requests);
                        Close(getValue(oldest));
                    } else {
                        Close(connfd);
                        pthread_mutex_unlock(&global_lock);
                        continue;
                    }
                }
                else if (strcmp(schedAlg, "bf") == 0) {
                    // block_flush => wait all done, then drop new
                    while ( (getSize(running_requests) > 0) ||
                            (getSize(waiting_requests) > 0) )
                    {
                        pthread_cond_wait(&empty_queue, &global_lock);
                    }
                    Close(connfd);
                    pthread_mutex_unlock(&global_lock);
                    continue;
                }
                else if (strcmp(schedAlg, "random") == 0) {
                    // Drop ~50% of waiting requests at random
                    int wsize = getSize(waiting_requests);
                    if (wsize == 0) {
                        // no waiting => close new
                        Close(connfd);
                        pthread_mutex_unlock(&global_lock);
                        continue;
                    }
                    // half => round up
                    int toDrop = (wsize + 1)/2;
                    for (int i = 0; i < toDrop; i++) {
                        int idx = rand() % getSize(waiting_requests);
                        int oldFd = removeByIndex(waiting_requests, idx);
                        Close(oldFd);
                        if (getSize(waiting_requests) == 0) {
                            break;
                        }
                    }
                }
            }
            // now we can accept the new request
            appendNewRequest(waiting_requests, connfd, arrival_time);
            pthread_cond_signal(&read_allowed);
        }

        pthread_mutex_unlock(&global_lock);
    }
    return 0;
}
