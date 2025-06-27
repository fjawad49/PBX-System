#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"
#include "helper.h"

static void terminate(int status);
volatile sig_atomic_t server_status = 1;

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
void server_handler(int sig){
    server_status = 0;
}

int main(int argc, char* argv[]){
    char *port = "3000";
    int port_num;

    if (argc != 1 && argc != 3){
        fprintf(stderr, "Invalid number of command-line arguments.  Got: %d, Exp: 1 or 3\n", argc);
        exit(EXIT_FAILURE);
    }
    if(argc == 3){
        if (strcmp("-p", argv[1])){
            fprintf(stderr, "Invalid flag detected.  Got: %s, Exp: -p\n", argv[1]);
            exit(EXIT_FAILURE);
        }

        char *endpointer;
        port_num = strtol(argv[2], &endpointer, 10);
        if (*endpointer != '\0' || port_num < 1024){
            fprintf(stderr, "Invalid port argument or number (must be >= 1024): Got: %s\n", argv[2]);
            exit(EXIT_FAILURE);
        }
        port = argv[2];
    }

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    struct sigaction handler;
    memset(&handler, 0, sizeof(struct sigaction));
    handler.sa_handler =  server_handler;
    handler.sa_flags = 0;
    sigfillset(&handler.sa_mask);
    if (sigaction(SIGHUP, &handler, NULL)){
        fprintf(stderr, "Could not successfully implement SIGHUP signal handler.\n");
        exit(EXIT_FAILURE);
    }

    // Code adapted from Professor Lee LEC21-Concurrency Lecture slide 44.
    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tpid;

    listenfd = open_listenfd(port);
    debug("listenfd: %d, port: %s", listenfd, port);
    if (listenfd < 0){
        fprintf(stderr, "Could not successfully create listening file descriptor: %d\n", listenfd);
        exit(EXIT_FAILURE);
    }

    while(server_status){
        clientlen = sizeof(struct sockaddr_storage);
        connfd = malloc(sizeof(int));
        if(server_status == 0){
            free(connfd);
            terminate(EXIT_SUCCESS);
            break;
        }
        *connfd = accept(listenfd, (struct sockaddr *) &clientaddr, &clientlen);
        debug("connfd: %d", *connfd);
        debug("server status: %d", server_status);
        if(server_status == 0){
            free(connfd);
            terminate(EXIT_SUCCESS);
            break;
        }
        if(*connfd < 0){
            free(connfd);
            fprintf(stderr, "Could not successfully accept client request\n");
            continue;
        }
        if (pthread_create(&tpid, NULL, pbx_client_service, connfd)){
            free(connfd);
            fprintf(stderr, "Could not successfully create client thread\n");
            continue;
        }
    }
}

/*
 * Function called to cleanly shut down the server.
 */

static void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    exit(status);
}
