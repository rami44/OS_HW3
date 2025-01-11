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

// ---------------------------
// VIP Thread Function
// ---------------------------
void *VIPThreadFunction(void *args)
{
    // We receive a pointer to stable memory in threadArr[num].
    threadStats *threadStruct = (threadStats *)args;

    while (1) {
        pthread_mutex_lock(&global_lock);

        // If no VIP requests, wait
        while (getSize(vip_requests) == 0) {
            pthread_cond_wait(&vip_allowed, &global_lock);
        }

        // Mark VIP as busy before dequeueing the VIP request
        vip_is_busy = 1;

        // Dequeue the next VIP request
        Node toWorkWith = removeFront(vip_requests);
        append(running_requests, toWorkWith, threadStruct->id);

        pthread_mutex_unlock(&global_lock);

        // ---- Handle the VIP request ----
        requestHandle(getValue(toWorkWith), toWorkWith, threadStruct);
        Close(getValue(toWorkWith));
        // --------------------------------

        // Cleanup after finishing the request
        pthread_mutex_lock(&global_lock);

        removeByValue(running_requests, getValue(toWorkWith));
        // Mark VIP no longer busy
        vip_is_busy = 0;

        // Signal that a slot freed up
        pthread_cond_broadcast(&write_allowed);

        // If queues are all empty, signal empty_queue
        if ((getSize(running_requests) == 0) &&
            (getSize(waiting_requests) == 0) &&
            (getSize(vip_requests) == 0)) {
            pthread_cond_signal(&empty_queue);
        }

        // Wake up regular threads, in case they’re blocked by vip_is_busy
        pthread_cond_broadcast(&read_allowed);

        pthread_mutex_unlock(&global_lock);
    }

    return NULL;
}

// ---------------------------
// Regular Thread Function
// ---------------------------
void *ThreadFunction(void *args)
{
    threadStats *threadStruct = (threadStats *)args;

    while (1) {
        pthread_mutex_lock(&global_lock);

        // Block if:
        //  1) No regular requests available, OR
        //  2) VIP queue has requests, OR
        //  3) VIP is currently busy
        while ((getSize(waiting_requests) == 0) ||
               (getSize(vip_requests) > 0)      ||
               (vip_is_busy == 1)) {
            if (getSize(vip_requests) > 0) {
                // If VIP queue is non-empty, wait on vip_allowed
                pthread_cond_wait(&vip_allowed, &global_lock);
            } else {
                // Otherwise, just wait on read_allowed
                pthread_cond_wait(&read_allowed, &global_lock);
            }
        }

        // Dequeue the oldest regular request
        Node toWorkWith = removeFront(waiting_requests);
        append(running_requests, toWorkWith, threadStruct->id);

        pthread_mutex_unlock(&global_lock);

        // ---- Handle the regular request ----
        requestHandle(getValue(toWorkWith), toWorkWith, threadStruct);
        Close(getValue(toWorkWith));
        // ------------------------------------

        pthread_mutex_lock(&global_lock);

        removeByValue(running_requests, getValue(toWorkWith));
        pthread_cond_signal(&write_allowed);

        // If everything is empty, signal empty_queue
        if ((getSize(running_requests) == 0) &&
            (getSize(waiting_requests) == 0) &&
            (getSize(vip_requests) == 0)) {
            pthread_cond_signal(&empty_queue);
        }

        pthread_mutex_unlock(&global_lock);
    }

    return NULL;
}

// --------------------------------------------------
// Parse command-line arguments (with scheduling alg)
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
        fprintf(stderr, "Error: Number of threads must be positive.\n");
        exit(1);
    }
    *poolSize = atoi(argv[3]);
    if (*poolSize <= 0) {
        fprintf(stderr, "Error: Queue size must be positive.\n");
        exit(1);
    }

    strcpy(schedAlg, argv[4]);
    // Basic check for known sched alg
    if (strcmp(schedAlg, "block") != 0 &&
        strcmp(schedAlg, "dt")    != 0 &&
        strcmp(schedAlg, "dh")    != 0 &&
        strcmp(schedAlg, "bf")    != 0 &&
        strcmp(schedAlg, "random")!= 0) {
        fprintf(stderr, "Error: Unknown scheduling algorithm: %s\n", schedAlg);
        exit(1);
    }
}

// --------------------------------------------------
// Initialize threads: <num> regular + 1 VIP thread
// --------------------------------------------------
void initializeThreads(int num, threadStats *threadsArr, pthread_t *vipThread)
{
    // Create regular threads [0..num-1]
    for (int i = 0; i < num; i++) {
        threadsArr[i].id        = i;
        threadsArr[i].dynm_req  = 0;
        threadsArr[i].stat_req  = 0;
        threadsArr[i].total_req = 0;

        pthread_create(&threadsArr[i].ourThread, NULL,
                       ThreadFunction, (void *)&threadsArr[i]);
    }

    // Create VIP thread in index [num]
    // so we have stable memory for the VIP stats
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

    // Initialize queues
    vip_requests     = queueConstructor();
    running_requests = queueConstructor();
    waiting_requests = queueConstructor();

    // Initialize synchronization primitives
    pthread_cond_init(&empty_queue, NULL);
    pthread_cond_init(&vip_allowed, NULL);
    pthread_cond_init(&read_allowed, NULL);
    pthread_cond_init(&write_allowed, NULL);
    pthread_mutex_init(&global_lock, NULL);

    // We'll allocate for threadNum+1 so there's space for the VIP stats
    threadStats *threadArr = (threadStats *)malloc(sizeof(threadStats) * (threadNum + 1));
    pthread_t vipThread;

    // Initialize threads (regular + VIP)
    initializeThreads(threadNum, threadArr, &vipThread);

    // Start listening
    listenfd = Open_listenfd(port);
    srand(time(NULL)); // for 'random' dropping if needed

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);

        // Grab arrival time
        struct timeval arrival_time;
        gettimeofday(&arrival_time, NULL);

        pthread_mutex_lock(&global_lock);

        // If getRequestMetaData(...) == 1 => VIP request, else regular
        int isVIP = getRequestMetaData(connfd);

        if (isVIP) {
            // VIP request
            // If total usage is at capacity, we block (per requirement).
            while ((getSize(running_requests) + 
                    getSize(waiting_requests) +
                    getSize(vip_requests)) >= poolSize)
            {
                pthread_cond_wait(&write_allowed, &global_lock);
            }

            appendNewRequest(vip_requests, connfd, arrival_time);
            // Signal VIP thread that a VIP request is available
            pthread_cond_signal(&vip_allowed);

        } else {
            // Regular request
            // Check if we are at capacity
            if ((getSize(running_requests) + getSize(waiting_requests)) == poolSize) {
                // Overload: apply scheduling policy
                if (strcmp(schedAlg, "block") == 0) {
                    while ((getSize(running_requests) + 
                            getSize(waiting_requests)) == poolSize)
                    {
                        pthread_cond_wait(&write_allowed, &global_lock);
                    }
                }
                else if (strcmp(schedAlg, "dt") == 0) {
                    // Drop tail: close new request immediately
                    Close(connfd);
                    pthread_mutex_unlock(&global_lock);
                    continue;
                }
                else if (strcmp(schedAlg, "dh") == 0) {
                    // Drop head: remove the oldest from waiting
                    if (getSize(waiting_requests) > 0) {
                        Node oldest = removeFront(waiting_requests);
                        Close(getValue(oldest));
                    } else {
                        // If waiting queue empty, just close new
                        Close(connfd);
                        pthread_mutex_unlock(&global_lock);
                        continue;
                    }
                }
                else if (strcmp(schedAlg, "bf") == 0) {
                    // block_flush: wait until all are done, then drop new
                    while ((getSize(running_requests) > 0) ||
                           (getSize(waiting_requests) > 0))
                    {
                        pthread_cond_wait(&empty_queue, &global_lock);
                    }
                    // After flush, drop new
                    Close(connfd);
                    pthread_mutex_unlock(&global_lock);
                    continue;
                }
                else if (strcmp(schedAlg, "random") == 0) {
                    // Drop random 50% of waiting
                    int wsize = getSize(waiting_requests);
                    if (wsize == 0) {
                        // No waiting to drop, just close new
                        Close(connfd);
                        pthread_mutex_unlock(&global_lock);
                        continue;
                    }
                    int toDrop = wsize / 2; // 50%
                    for (int i = 0; i < toDrop; i++) {
                        int idx = rand() % getSize(waiting_requests);
                        int oldFd = removeByIndex(waiting_requests, idx);
                        Close(oldFd);
                        if (getSize(waiting_requests) == 0) {
                            break;
                        }
                    }
                }
                // Possibly space is freed, so we can proceed
            }

            // Enqueue the new regular request
            appendNewRequest(waiting_requests, connfd, arrival_time);
            // Signal regular threads
            pthread_cond_signal(&read_allowed);
        }

        pthread_mutex_unlock(&global_lock);
    }

    // (In practice, you'd free memory and join threads on shutdown.)
    // free(threadArr);

    return 0;
}
