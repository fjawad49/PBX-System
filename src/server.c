/*
 * "PBX" server module.
 * Manages interaction with a client telephone unit (TU).
 */
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "debug.h"
#include "pbx.h"
#include "server.h"

/*
 * Thread function for the thread that handles interaction with a client TU.
 * This is called after a network connection has been made via the main server
 * thread and a new thread has been created to handle the connection.
 */
#define BUFFER 8192

volatile sig_atomic_t client_status = 1;

void sigpipe_handler(int sig){
    debug("caught a sigpipe");
    client_status = 0;
}

void *pbx_client_service(void *arg) {
    int connfd = *((int *) arg);
    pthread_detach(pthread_self());
    free(arg);

    TU *teleunit;
    if(!(teleunit = tu_init(connfd))){
        fprintf(stderr, "Failed to successfully create telephone unit. Client ID:%d\n", connfd);
        close(connfd);
        return NULL;
    }

    if (pbx_register(pbx, teleunit, connfd)){
        fprintf(stderr, "Failed to successfully regester telephone unit on PBX.  Client ID:%d\n", connfd);
        close(connfd);
        return NULL;
    }

    char buffer[BUFFER];
    debug("begin client loop");

    struct sigaction handler;
    memset(&handler, 0, sizeof(struct sigaction));
    handler.sa_handler = sigpipe_handler;
    handler.sa_flags = 0;
    sigfillset(&handler.sa_mask);
    if (sigaction(SIGPIPE, &handler, NULL)){
        fprintf(stderr, "Could not successfully implement SIGHUP signal handler.\n");
        exit(EXIT_FAILURE);
    }

    while(client_status){
        ssize_t bytes_read = read(connfd, buffer, BUFFER-1);

        if(bytes_read <= 0){
            debug("EOF read or error");
            tu_hangup(teleunit);
            break;
        }

        buffer[bytes_read] = '\0';
        char temp_buffer[BUFFER];
        strcpy(temp_buffer, buffer);
        debug("value of buffer: %s", buffer);
        char *command = strtok(buffer, " ");
        debug("value of buffer: %s", buffer);

        if (command == NULL){
            continue;
        }
        int command_len = strlen(command);
        int tu_command_len;

        debug("commad_len %d", command_len);
        debug("client_comm: \"%s\"", command);

        tu_command_len = strlen(tu_command_names[TU_PICKUP_CMD]);
        if (command_len-2 == tu_command_len && !strncmp(command, tu_command_names[TU_PICKUP_CMD], tu_command_len)){
            debug("pickup");
            tu_pickup(teleunit);
            continue;
        }

        tu_command_len = strlen(tu_command_names[TU_HANGUP_CMD]);
        if (command_len-2 == tu_command_len && !strncmp(command, tu_command_names[TU_HANGUP_CMD], tu_command_len)){
            debug("hangup");
            tu_hangup(teleunit);
            continue;
        }

        tu_command_len = strlen(tu_command_names[TU_DIAL_CMD]);
        if (command_len == tu_command_len && !strcmp(command, tu_command_names[TU_DIAL_CMD])){
            debug("dial client");
            char *ext = strtok(NULL, " ");
            if (*ext == '\r' && *(ext + 1) == '\n'){
                continue;
            }
            char *endpointer;
            int ext_num = strtol(ext, &endpointer, 10);
            if (ext_num == 0 && *(endpointer - 1) != '0')
                ext_num = -1;

            debug("calling extension: %d", ext_num);
            pbx_dial(pbx, teleunit, ext_num);
            continue;
        }

        tu_command_len = strlen(tu_command_names[TU_CHAT_CMD]);
        if (command[command_len-2] == '\r' && command[command_len - 1] == '\n'){
            command[command_len-2] = '\0';
            command_len -= 2;
        }
        if (command_len == tu_command_len && !strcmp(command, tu_command_names[TU_CHAT_CMD])){
            debug("chat");
            int last_read = 0;
            if (temp_buffer[bytes_read-2] == '\r' && temp_buffer[bytes_read-1] == '\n'){
                debug("first read is last read");
                last_read = 1;
                temp_buffer[bytes_read-2] = '\0';
            }

            char *temp_command = command;
            for (int i = 0; i < command_len; i++){
                *temp_command = toupper(*temp_command);
                temp_command++;
            }
            char *temp = temp_buffer + 4;
            debug("value of temp before: %s", temp);
            while(*temp == ' ' && *temp != '\0'){
                temp++;
            }

            sigset_t mask, prev;
            sigemptyset(&mask);
            sigfillset(&mask);
            sigprocmask(SIG_BLOCK, &mask, &prev);
            sprintf(buffer, "%s %s", command, temp);
            sigprocmask(SIG_SETMASK, &prev, NULL);

            debug("value of temp_buffer: %s", buffer);
            debug("value of command after: %s", command);
            tu_chat(teleunit, buffer);

            while(!last_read){
                bytes_read = read(connfd, buffer, BUFFER-1);
                if (buffer[bytes_read-2] == '\r' && buffer[bytes_read-1] == '\n'){
                    last_read = 1;
                    buffer[bytes_read-2] = '\0';
                }else{
                    buffer[bytes_read] = '\0';
                }
               
                tu_chat(teleunit, buffer);
            }
            continue;
        }

        debug("invalid command");
    }

    if (pbx_unregister(pbx, teleunit)){
        fprintf(stderr, "Failed to unregester telephone unit on PBX.  Client ID:%d\n", connfd);
        close(connfd);
        return NULL;
    }

    close(connfd);
    return NULL;
}

