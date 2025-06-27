/*
 * TU: simulates a "telephone unit", which interfaces a client with the PBX.
 */
#include <stdlib.h>
#include <semaphore.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include "pbx.h"
#include "debug.h"

#define BUFFER 8192

typedef struct tu{
    sem_t tu_mutex;
    TU_STATE state;
    int references;
    int ext;
    int fd;
    TU *peer;
}TU;

/*
 * Initialize a TU
 *
 * @param fd  The file descriptor of the underlying network connection.
 * @return  The TU, newly initialized and in the TU_ON_HOOK state, if initialization
 * was successful, otherwise NULL.
 */

TU *tu_init(int fd) {
    TU *tu;
    if (!(tu = calloc(1, sizeof(TU)))){
        fprintf(stderr, "Could not dynamically allocate memory for telephone unit.\n");
        return NULL;
    }
    if (sem_init(&tu->tu_mutex, 0, 1)){
        fprintf(stderr, "Could not successfully create mutex for telephone unit.\n");
        return NULL;
    }
    tu->state = TU_ON_HOOK;
    tu->references = 0;
    tu->peer = NULL;
    tu->ext = -1;
    tu->fd = fd;
    return tu;
}


/*
 * Increment the reference count on a TU.
 *
 * @param tu  The TU whose reference count is to be incremented
 * @param reason  A string describing the reason why the count is being incremented
 * (for debugging purposes).
 */

void tu_ref(TU *tu, char *reason) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
    }

    tu->references += 1;

}


/*
 * Decrement the reference count on a TU, freeing it if the count becomes 0.
 *
 * @param tu  The TU whose reference count is to be decremented
 * @param reason  A string describing the reason why the count is being decremented
 * (for debugging purposes).
 */


void tu_unref(TU *tu, char *reason) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
    }

    tu->references -= 1;
    debug("references, fd %d, = %d", tu->fd, tu->references);
    if (tu->references == 0){
        sem_destroy(&tu->tu_mutex);
        free(tu);
    }
}


/*
 * Get the file descriptor for the network connection underlying a TU.
 * This file descriptor should only be used by a server to read input from
 * the connection.  Output to the connection must only be performed within
 * the PBX functions.
 *
 * @param tu
 * @return the underlying file descriptor, if any, otherwise -1.
 */


int tu_fileno(TU *tu) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu->fd >= 0){
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        return tu->fd;
    }

    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    fprintf(stderr, "Invalid file descriptor detected: %d\n", tu->fd);
    return -1;
}


/*
 * Get the extension number for a TU.
 * This extension number is assigned by the PBX when a TU is registered
 * and it is used to identify a particular TU in calls to tu_dial().
 * The value returned might be the same as the value returned by tu_fileno(),
 * but is not necessarily so.
 *
 * @param tu
 * @return the extension number, if any, otherwise -1.
 */

int tu_extension(TU *tu) {
    debug("TU with fd %d and ext %d", tu->fd, tu->ext);
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu->ext >= 0){
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        return tu->ext;
    }

    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    fprintf(stderr, "Invalid extension detected: %d\n", tu->ext);
    return -1;
}

static int send_to_client(TU *target, TU_STATE state, char *msg){
    char *state_string = tu_state_names[state];
    char buffer[BUFFER];

    int fd = target->fd;
    if (fd < 0){
        fprintf(stderr, "Telephone unit has invalid file descriptor number.\n");
        return -1;
    }

    sigset_t mask, prev;
    sigemptyset(&mask);
    sigfillset(&mask);

    sigprocmask(SIG_BLOCK, &mask, &prev);
    if(state == TU_CONNECTED){
        if (msg == NULL){
            sprintf(buffer, "%s %d\n", state_string, target->peer->ext);
        }else{
            sprintf(buffer, "%s\n", msg);
        }
    }else if(state == TU_ON_HOOK){
        sprintf(buffer, "%s %d\n", state_string, target->ext);
    }else{
        sprintf(buffer, "%s\n", state_string);
    }
    sigprocmask(SIG_SETMASK, &prev, NULL);

    int count = strlen(buffer);
    ssize_t bytes_written = write(fd, buffer, count);

    if(bytes_written < 0){
        debug("error, state: %s, fd %d", buffer, fd);
        return -1;
    }else if(bytes_written != count){
        debug("error");
        fprintf(stderr, "Failed to write correct number of bytes. Exp: %d Got: %lu \n", count, bytes_written);
        return -1;
    }

    return 0;
}

/*
 * Set the extension number for a TU.
 * A notification is set to the client of the TU.
 * This function should be called at most once one any particular TU.
 *
 * @param tu  The TU whose extension is being set.
 */

int tu_set_extension(TU *tu, int ext) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (ext < 0 || ext > PBX_MAX_EXTENSIONS-1){
        fprintf(stderr, "Not a valid extension, must be between 0 and %d.\n", PBX_MAX_EXTENSIONS);
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    tu->ext = ext;
    tu->state = TU_ON_HOOK;
    send_to_client(tu, tu->state, NULL);

    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    return 0;
}


/*
 * Initiate a call from a specified originating TU to a specified target TU.
 *   If the originating TU is not in the TU_DIAL_TONE state, then there is no effect.
 *   If the target TU is the same as the originating TU, then the TU transitions
 *     to the TU_BUSY_SIGNAL state.
 *   If the target TU already has a peer, or the target TU is not in the TU_ON_HOOK
 *     state, then the originating TU transitions to the TU_BUSY_SIGNAL state.
 *   Otherwise, the originating TU and the target TU are recorded as peers of each other
 *     (this causes the reference count of each of them to be incremented),
 *     the target TU transitions to the TU_RINGING state, and the originating TU
 *     transitions to the TU_RING_BACK state.
 *
 * In all cases, a notification of the resulting state of the originating TU is sent to
 * to the associated network client.  If the target TU has changed state, then its client
 * is also notified of its new state.
 *
 * If the caller of this function was unable to determine a target TU to be called,
 * it will pass NULL as the target TU.  In this case, the originating TU will transition
 * to the TU_ERROR state if it was in the TU_DIAL_TONE state, and there will be no
 * effect otherwise.  This situation is handled here, rather than in the caller,
 * because here we have knowledge of the current TU state and we do not want to introduce
 * the possibility of transitions to a TU_ERROR state from arbitrary other states,
 * especially in states where there could be a peer TU that would have to be dealt with.
 *
 * @param tu  The originating TU.
 * @param target  The target TU, or NULL if the caller of this function was unable to
 * identify a TU to be dialed.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state.
 */

int tu_dial(TU *tu, TU *target) {
    if(tu == NULL && target == NULL){
        fprintf(stderr, "Not valid telephone units.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    debug("dialing tu %d", tu->ext);

    if(tu->state != TU_DIAL_TONE){
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    if(tu->state == TU_DIAL_TONE && tu != NULL && target == NULL){
        debug("target null");
        tu->state = TU_ERROR;
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    if(tu == target){
        debug("busy signal");
        tu->state = TU_BUSY_SIGNAL;
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu < target){
        if (sem_wait(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_wait(&target->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
    }else{
        if (sem_wait(&target->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_wait(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
    }

    debug("dialing target %d", tu->ext);

    if(tu->peer != NULL || target->state != TU_ON_HOOK){
        debug("busy signal");
        tu->state = TU_BUSY_SIGNAL;
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&target->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    debug("starting transmission");

    tu->peer = target;
    target->peer = tu;
    tu->state = TU_RING_BACK;
    target->state = TU_RINGING;
    tu_ref(tu, "Telephone unit peer established");
    tu_ref(target, "Telephone unit peer established");

    int ret1 = send_to_client(tu, tu->state, NULL);
    int ret2 = send_to_client(target, target->state, NULL);
    if (sem_post(&target->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    if (ret1 || ret2)
        return -1;

    return 0;
}

/*
 * Take a TU receiver off-hook (i.e. pick up the handset).
 *   If the TU is in neither the TU_ON_HOOK state nor the TU_RINGING state,
 *     then there is no effect.
 *   If the TU is in the TU_ON_HOOK state, it goes to the TU_DIAL_TONE state.
 *   If the TU was in the TU_RINGING state, it goes to the TU_CONNECTED state,
 *     reflecting an answered call.  In this case, the calling TU simultaneously
 *     also transitions to the TU_CONNECTED state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The TU that is to be picked up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */

int tu_pickup(TU *tu) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu->state != TU_ON_HOOK && tu->state != TU_RINGING){
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    if(tu->state == TU_ON_HOOK){
        tu->state = TU_DIAL_TONE;
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    if(tu->state == TU_RINGING){
        if (sem_wait(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        tu->state = TU_CONNECTED;
        tu->peer->state = TU_CONNECTED;
        int ret1 = send_to_client(tu, tu->state, NULL);
        int ret2 = send_to_client(tu->peer, tu->peer->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_post(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret1 || ret2)
            return -1;
        else
            return 0;
    }

    int ret = send_to_client(tu, tu->state, NULL);
    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    if (ret)
        return -1;
    else
        return 0;
}


/*
 * Hang up a TU (i.e. replace the handset on the switchhook).
 *
 *   If the TU is in the TU_CONNECTED or TU_RINGING state, then it goes to the
 *     TU_ON_HOOK state.  In addition, in this case the peer TU (the one to which
 *     the call is currently connected) simultaneously transitions to the TU_DIAL_TONE
 *     state.
 *   If the TU was in the TU_RING_BACK state, then it goes to the TU_ON_HOOK state.
 *     In addition, in this case the calling TU (which is in the TU_RINGING state)
 *     simultaneously transitions to the TU_ON_HOOK state.
 *   If the TU was in the TU_DIAL_TONE, TU_BUSY_SIGNAL, or TU_ERROR state,
 *     then it goes to the TU_ON_HOOK state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The tu that is to be hung up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */

int tu_hangup(TU *tu) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu->state == TU_CONNECTED || tu->state == TU_RINGING){
        if (sem_wait(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        tu->state = TU_ON_HOOK;
        tu->peer->state = TU_DIAL_TONE;
        tu_unref(tu, "Telephone unit peer removed");
        tu_unref(tu->peer, "Telephone unit peer removed");

        int ret1 = send_to_client(tu, tu->state, NULL);
        int ret2 = send_to_client(tu->peer, tu->peer->state, NULL);
        tu->peer->peer = NULL;
        if (sem_post(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        tu->peer = NULL;
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret1 || ret2)
            return -1;
        else
            return 0;
    }

    if(tu->state == TU_RING_BACK){
        if (sem_wait(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        tu->state = TU_ON_HOOK;
        tu->peer->state = TU_ON_HOOK;
        int ret1 = send_to_client(tu, tu->state, NULL);
        int ret2 = send_to_client(tu->peer, tu->peer->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_post(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret1 || ret2)
            return -1;
        else
            return 0;
    }

    if(tu->state == TU_DIAL_TONE || tu->state == TU_BUSY_SIGNAL || tu->state == TU_ERROR){
        tu->state = TU_ON_HOOK;
        int ret = send_to_client(tu, tu->state, NULL);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret)
            return -1;
        else
            return 0;
    }

    int ret = send_to_client(tu, tu->state, NULL);
    if (sem_post(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    if (ret)
        return -1;
    else
        return 0;
}


/*
 * "Chat" over a connection.
 *
 * If the state of the TU is not TU_CONNECTED, then nothing is sent and -1 is returned.
 * Otherwise, the specified message is sent via the network connection to the peer TU.
 * In all cases, the states of the TUs are left unchanged and a notification containing
 * the current state is sent to the TU sending the chat.
 *
 * @param tu  The tu sending the chat.
 * @param msg  The message to be sent.
 * @return 0  If the chat was successfully sent, -1 if there is no call in progress
 * or some other error occurs.
 */

int tu_chat(TU *tu, char *msg) {
    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }

    if(tu->state != TU_CONNECTED){
        return -1;
    }else{
        if (sem_wait(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        int ret1 = send_to_client(tu, tu->state, NULL);
        int ret2 = send_to_client(tu->peer, tu->peer->state, msg);
        if (sem_post(&tu->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (sem_post(&tu->peer->tu_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
            return -1;
        }
        if (ret1 || ret2)
            return -1;
        else
            return 0;
    }

    if (sem_wait(&tu->tu_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for telephone unit.\n");
        return -1;
    }
    return -1;
}
