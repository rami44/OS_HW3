/*
 * client.c: A very, very primitive HTTP client.
 *
 * Now updated so that an optional fourth argument can be used
 * to specify the method, e.g., REAL or GET.
 *
 * Example usage:
 *   1) ./client <host> <port> <filename>
 *        -> uses default method "GET"
 *   2) ./client <host> <port> <filename> REAL
 *        -> uses "REAL" for VIP requests
 *
 */

#include "segel.h"

/*
 * Send an HTTP request for the specified file and method.
 */
void clientSend(int fd, char *filename, char *method)
{
    char buf[MAXLINE];
    char hostname[MAXLINE];

    Gethostname(hostname, MAXLINE);

    /* Form and send the HTTP request line:
       e.g., "GET /file HTTP/1.1\r\n"
       or    "REAL /file HTTP/1.1\r\n"
    */
    sprintf(buf, "%s %s HTTP/1.1\r\n", method, filename);

    /* Add a Host header and a blank line to end headers */
    sprintf(buf, "%shost: %s\r\n\r\n", buf, hostname);

    /* Send it all to server */
    Rio_writen(fd, buf, strlen(buf));
}
  
/*
 * Read the HTTP response and print it out
 */
void clientPrint(int fd)
{
    rio_t rio;
    char buf[MAXBUF];
    int length = 0;
    ssize_t n;
  
    Rio_readinitb(&rio, fd);

    /* Read and display the HTTP Header */
    n = Rio_readlineb(&rio, buf, MAXBUF);
    while ((n > 0) && strcmp(buf, "\r\n")) {
        printf("Header: %s", buf);

        /* Example: look for Content-Length field */
        if (sscanf(buf, "Content-Length: %d", &length) == 1) {
            printf("Length = %d\n", length);
        }

        n = Rio_readlineb(&rio, buf, MAXBUF);
    }

    /* Read and display the HTTP Body */
    n = Rio_readlineb(&rio, buf, MAXBUF);
    while (n > 0) {
        printf("%s", buf);
        n = Rio_readlineb(&rio, buf, MAXBUF);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <filename> [method]\n", argv[0]);
        exit(1);
    }

    char *host     = argv[1];
    int   port     = atoi(argv[2]);
    char *filename = argv[3];

    // Optional 4th argument: HTTP method (e.g. REAL or GET).
    // If omitted, default to "GET".
    char *method = "GET";
    if (argc >= 5) {
        method = argv[4];
    }

    /* Open a connection to the specified host and port */
    int clientfd = Open_clientfd(host, port);
    if (clientfd < 0) {
        fprintf(stderr, "Error: could not connect to %s:%d\n", host, port);
        exit(1);
    }
  
    /* Send our request and then print the response */
    clientSend(clientfd, filename, method);
    clientPrint(clientfd);
    
    Close(clientfd);
    return 0;
}
