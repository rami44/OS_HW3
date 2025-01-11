#include "segel.h"

#include "request.h"





/* 

 * Helper: requestError

 * Sends an error response to the client.

 */

static void requestError(int fd,

                         char *cause,

                         char *errnum,

                         char *shortmsg,

                         char *longmsg,

                         struct timeval arrival,

                         struct timeval dispatch,

                         threadStats *t_stats)

{

    char buf[MAXLINE], body[MAXBUF];



    // Create the body of the error message

    sprintf(body, "<html><title>OS-HW3 Error</title>");

    sprintf(body, "%s<body bgcolor=\"fffff\">\r\n", body);

    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);

    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);

    sprintf(body, "%s<hr>OS-HW3 Web Server\r\n", body);



    // Write out the header information for this response

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);

    Rio_writen(fd, buf, strlen(buf));

    printf("%s", buf);



    sprintf(buf, "Content-Type: text/html\r\n");

    Rio_writen(fd, buf, strlen(buf));

    printf("%s", buf);



    sprintf(buf, "Content-Length: %lu\r\n", strlen(body));



    // Stats headers

    sprintf(buf, "%sStat-Req-Arrival:: %lu.%06lu\r\n", buf, arrival.tv_sec, arrival.tv_usec);

    sprintf(buf, "%sStat-Req-Dispatch:: %lu.%06lu\r\n", buf, dispatch.tv_sec, dispatch.tv_usec);

    sprintf(buf, "%sStat-Thread-Id:: %d\r\n", buf, t_stats->id);

    sprintf(buf, "%sStat-Thread-Count:: %d\r\n", buf, t_stats->total_req);

    sprintf(buf, "%sStat-Thread-Static:: %d\r\n", buf, t_stats->stat_req);

    sprintf(buf, "%sStat-Thread-Dynamic:: %d\r\n\r\n", buf, t_stats->dynm_req);



    Rio_writen(fd, buf, strlen(buf));

    printf("%s", buf);



    Rio_writen(fd, body, strlen(body));

    printf("%s", body);

}



/*

 * requestReadhdrs - Reads and discards everything up to an empty text line

 */

static void requestReadhdrs(rio_t *rp)

{

    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);

    while (strcmp(buf, "\r\n")) {

        Rio_readlineb(rp, buf, MAXLINE);

    }

    return;

}



/*

 * requestParseURI - Return 1 if static, 0 if dynamic content

 *                   Also calculates filename (and cgiargs, for dynamic)

 */

// request.c

static int requestParseURI(char *uri, char *filename, char *cgiargs)

{

    char *ptr;



    // Safety check: disallow ..

    if (strstr(uri, "..")) {

        sprintf(filename, "./public/home.html");

        return 1; // treat as static

    }



    // If not containing "cgi", treat as static

    if (!strstr(uri, "cgi")) {

        strcpy(cgiargs, "");

        sprintf(filename, "./public/%s", uri);

        if (uri[strlen(uri) - 1] == '/') {

            strcat(filename, "home.html");

        }

        return 1;

    } 

    else {

        // dynamic

        ptr = index(uri, '?');

        if (ptr) {

            strcpy(cgiargs, ptr + 1);

            *ptr = '\0';

        } else {

            strcpy(cgiargs, "");

        }

        sprintf(filename, "./public/%s", uri);

        return 0;

    }

}



/*

 * requestGetFiletype - Derives file type from filename

 */

static void requestGetFiletype(char *filename, char *filetype)

{

    if (strstr(filename, ".html"))

        strcpy(filetype, "text/html");

    else if (strstr(filename, ".gif"))

        strcpy(filetype, "image/gif");

    else if (strstr(filename, ".jpg"))

        strcpy(filetype, "image/jpeg");

    else

        strcpy(filetype, "text/plain");

}



/*

 * requestServeDynamic - Serves a dynamic request

 */

static void requestServeDynamic(int fd,

                                char *filename,

                                char *cgiargs,

                                struct timeval arrival,

                                struct timeval dispatch,

                                threadStats *t_stats)

{

    char buf[MAXLINE];

    char *emptylist[] = { NULL };



    // The server writes just part of the header;

    // the CGI script finishes writing the HTTP response header.

    sprintf(buf, "HTTP/1.0 200 OK\r\n");

    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);



    // Stats

    sprintf(buf, "%sStat-Req-Arrival:: %lu.%06lu\r\n", buf, arrival.tv_sec, arrival.tv_usec);

    sprintf(buf, "%sStat-Req-Dispatch:: %lu.%06lu\r\n", buf, dispatch.tv_sec, dispatch.tv_usec);

    sprintf(buf, "%sStat-Thread-Id:: %d\r\n", buf, t_stats->id);

    sprintf(buf, "%sStat-Thread-Count:: %d\r\n", buf, t_stats->total_req);

    sprintf(buf, "%sStat-Thread-Static:: %d\r\n", buf, t_stats->stat_req);

    sprintf(buf, "%sStat-Thread-Dynamic:: %d\r\n", buf, t_stats->dynm_req);



    Rio_writen(fd, buf, strlen(buf));



    pid_t pid;

    if ((pid = Fork()) == 0) {

        // Child

        Setenv("QUERY_STRING", cgiargs, 1);

        Dup2(fd, STDOUT_FILENO);   // the CGI writes to fd

        Execve(filename, emptylist, environ);

    }

    WaitPid(pid, NULL, WUNTRACED);

}



/*

 * requestServeStatic - Serves a static (file) request

 */

static void requestServeStatic(int fd,

                               char *filename,

                               int filesize,

                               struct timeval arrival,

                               struct timeval dispatch,

                               threadStats *t_stats)

{

    int srcfd;

    char *srcp, filetype[MAXLINE], buf[MAXBUF];



    requestGetFiletype(filename, filetype);

    srcfd = Open(filename, O_RDONLY, 0);



    // Memory-map the file

    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);

    Close(srcfd);



    // Write HTTP response header

    sprintf(buf, "HTTP/1.0 200 OK\r\n");

    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);

    sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);

    sprintf(buf, "%sContent-Type: %s\r\n", buf, filetype);



    // Stats

    sprintf(buf, "%sStat-Req-Arrival:: %lu.%06lu\r\n", buf, arrival.tv_sec, arrival.tv_usec);

    sprintf(buf, "%sStat-Req-Dispatch:: %lu.%06lu\r\n", buf, dispatch.tv_sec, dispatch.tv_usec);

    sprintf(buf, "%sStat-Thread-Id:: %d\r\n", buf, t_stats->id);

    sprintf(buf, "%sStat-Thread-Count:: %d\r\n", buf, t_stats->total_req);

    sprintf(buf, "%sStat-Thread-Static:: %d\r\n", buf, t_stats->stat_req);

    sprintf(buf, "%sStat-Thread-Dynamic:: %d\r\n\r\n", buf, t_stats->dynm_req);



    Rio_writen(fd, buf, strlen(buf));



    // Write the file content

    Rio_writen(fd, srcp, filesize);

    Munmap(srcp, filesize);

}



/*

 * getRequestMetaData - returns True/False if REAL-time event

 *    Already used by server.c to classify VIP (REAL) vs GET

 */

int getRequestMetaData(int fd /* placeholder for future usage */)

{

    char buf[MAXLINE], method[MAXLINE];

    int bytesRead = recv(fd, buf, MAXLINE - 1, MSG_PEEK);

    if (bytesRead == -1) {

        perror("recv");

        // On error, treat as VIP or not (arbitrary). We'll say VIP(1).

        return 1;

    }



    sscanf(buf, "%s ", method);

    int isRealTime = !strcasecmp(method, "REAL");  // 1 if REAL, 0 otherwise

    return isRealTime;

}



/*

 * requestHandle - The main entry point to handle a request

 *   from the server threads.  

 *   - We read and parse the HTTP request from 'fd'.

 *   - Decide static/dynamic.

 *   - Serve or error out as needed.

 *   - Update threadStats (like t_stats->total_req, etc.).

 *   - arrival & dispatch times come from the Node's fields.

 */

void requestHandle(int fd, Node node, threadStats *t_stats)

{

    // Extract arrival and dispatch times from the Node

    struct timeval arrival  = getArrivalTime(node);

    struct timeval dispatch = getDispatchTime(node);



    // Increment total requests handled by the thread

    t_stats->total_req++;



    // Set up to parse the request line

    rio_t rio;

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&rio, fd);



    // Read the first line: e.g., "GET /something HTTP/1.1"

    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) {

        // If the request line is empty or there is an error, return early

        return;

    }



    sscanf(buf, "%s %s %s", method, uri, version);



    // Validate the HTTP method

    if (strcasecmp(method, "GET") && strcasecmp(method, "REAL")) {

        requestError(fd, method, "501", "Not Implemented",

                     "OS-HW3 Server does not implement this method",

                     arrival, dispatch, t_stats);

        return;

    }



    // Read and discard the request headers

    requestReadhdrs(&rio);



    // Parse URI to determine if the content is static or dynamic

    char filename[MAXLINE], cgiargs[MAXLINE];

    int is_static = requestParseURI(uri, filename, cgiargs);



    // Check if the requested file exists and has the necessary permissions

    struct stat sbuf;

    if (stat(filename, &sbuf) < 0) {

        requestError(fd, filename, "404", "Not Found",

                     "OS-HW3 Server could not find this file",

                     arrival, dispatch, t_stats);

        return;

    }



    if (is_static) {

        // Static content: file must be regular and have read permissions

        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {

            requestError(fd, filename, "403", "Forbidden",

                         "OS-HW3 Server could not read this file",

                         arrival, dispatch, t_stats);

            return;

        }



        // Increment static request count and serve the file

        t_stats->stat_req++;

        printf("Thread %d: Handling static request. Total static requests: %d\n",

               t_stats->id, t_stats->stat_req);



        requestServeStatic(fd, filename, sbuf.st_size, arrival, dispatch, t_stats);

    } else {

        // Dynamic content: file must be regular and have execute permissions

        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {

            requestError(fd, filename, "403", "Forbidden",

                         "OS-HW3 Server could not run this CGI program",

                         arrival, dispatch, t_stats);

            return;

        }



        // Increment dynamic request count and serve the CGI program

        t_stats->dynm_req++;

        printf("Thread %d: Handling dynamic request. Total dynamic requests: %d\n",

               t_stats->id, t_stats->dynm_req);



        requestServeDynamic(fd, filename, cgiargs, arrival, dispatch, t_stats);

    }

}
