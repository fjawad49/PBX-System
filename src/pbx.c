/*
 * PBX: simulates a Private Branch Exchange.
 */
#include <stdlib.h>
#include <semaphore.h>
#include  <sys/socket.h>

#include "pbx.h"
#include "debug.h"

typedef struct pbx{
    int num_entries;
    sem_t entries_mutex;
    sem_t pbx_mutex;
    TU **pbx_entries;
} PBX;

/*
 * Initialize a new PBX.
 *
 * @return the newly initialized PBX, or NULL if initialization fails.
 */

PBX *pbx_init() {
    debug("making pbx");
    if (!(pbx = calloc(1, sizeof(PBX)))){
        fprintf(stderr, "Could not dynamically allocate memory for pbx.\n");
        return NULL;
    }
    if (!(pbx->pbx_entries = calloc(PBX_MAX_EXTENSIONS, sizeof(TU *)))){
        fprintf(stderr, "Could not dynamically allocate memory for pbx entries.\n");
        return NULL;
    }
    if (sem_init(&pbx->pbx_mutex, 0, 1)){
        fprintf(stderr, "Could not successfully create mutex for pbx.\n");
        return NULL;
    }
    if (sem_init(&pbx->entries_mutex, 0, 1)){
        fprintf(stderr, "Could not successfully create mutex for pbx.\n");
        return NULL;
    }
    pbx->num_entries = 0;
    return pbx;
}

/*
 * Shut down a pbx, shutting down all network connections, waiting for all server
 * threads to terminate, and freeing all associated resources.
 * If there are any registered extensions, the associated network connections are
 * shut down, which will cause the server threads to terminate.
 * Once all the server threads have terminated, any remaining resources associated
 * with the PBX are freed.  The PBX object itself is freed, and should not be used again.
 *
 * @param pbx  The PBX to be shut down.
 */


void pbx_shutdown(PBX *pbx) {
    if (sem_wait(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }
    TU **entries = pbx->pbx_entries;
    for (int i = 0; i < PBX_MAX_EXTENSIONS; i++){
        if (entries[i] != NULL){
            if (shutdown(tu_fileno(entries[i]), SHUT_RDWR)){
                fprintf(stderr, "Error shutting down socket descriptor.  ID:%d\n", tu_fileno(entries[i]));
            }
        }
    }
    if (sem_post(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }

    int all_clients_closed = 0;
    while(!all_clients_closed){
        if (sem_wait(&pbx->entries_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }

        if (pbx->num_entries == 0){
            all_clients_closed = 1;
        }

        if (sem_post(&pbx->entries_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
    }

    sem_destroy(&pbx->pbx_mutex);
    sem_destroy(&pbx->entries_mutex);
    free(pbx->pbx_entries);
    free(pbx);
}

/*
 * Register a telephone unit with a PBX at a specified extension number.
 * This amounts to "plugging a telephone unit into the PBX".
 * The TU is initialized to the TU_ON_HOOK state.
 * The reference count of the TU is increased and the PBX retains this reference
 *for as long as the TU remains registered.
 * A notification of the assigned extension number is sent to the underlying network
 * client.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU to be registered.
 * @param ext  The extension number on which the TU is to be registered.
 * @return 0 if registration succeeds, otherwise -1.
 */


int pbx_register(PBX *pbx, TU *tu, int ext) {
    if(pbx == NULL){
        fprintf(stderr, "Not a valid pbx.\n");
        return -1;
    }

    if (ext < 0 || ext > PBX_MAX_EXTENSIONS-1){
        fprintf(stderr, "Not a valid extension, must be between 0 and %d.\n", PBX_MAX_EXTENSIONS);
        return -1;
    }

    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }
    if (sem_wait(&pbx->entries_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }

    debug("Registering at ext: %d", ext);
    TU **entries = pbx->pbx_entries;
    if (entries[ext] != NULL){
        fprintf(stderr, "There is already a telephone unit registered at extension %d.\n", ext);
        if (sem_post(&pbx->entries_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        if (sem_post(&pbx->pbx_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        return -1;
    }

    entries[ext] = tu;
    tu_set_extension(tu, ext);
    pbx->num_entries += 1;
    tu_ref(tu, "Registered telephone unit on pbx.");

    if (sem_post(&pbx->entries_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }
    if (sem_post(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
    }

    return 0;
}


/*
 * Unregister a TU from a PBX.
 * This amounts to "unplugging a telephone unit from the PBX".
 * The TU is disassociated from its extension number.
 * Then a hangup operation is performed on the TU to cancel any
 * call that might be in progress.
 * Finally, the reference held by the PBX to the TU is released.
 *
 * @param pbx  The PBX.
 * @param tu  The TU to be unregistered.
 * @return 0 if unregistration succeeds, otherwise -1.
 */


int pbx_unregister(PBX *pbx, TU *tu) {
    debug("Unregistering tu");
    if(pbx == NULL){
        fprintf(stderr, "Not a valid pbx.\n");
        return -1;
    }

    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }
    if (sem_wait(&pbx->entries_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }

    TU **entries = pbx->pbx_entries;
    int ext = tu_extension(tu);

    if (ext == -1){
        fprintf(stderr, "There is no valid extension for this telephone unit%d.\n", ext);
        if (sem_post(&pbx->entries_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        if (sem_post(&pbx->pbx_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        return -1;
    }

    if (entries[ext] == NULL){
        fprintf(stderr, "There is no telephone unit registered at extension %d.\n", ext);
        if (sem_post(&pbx->entries_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        if (sem_post(&pbx->pbx_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        return -1;
    }

    entries[ext] = NULL;
    pbx->num_entries -= 1;
    tu_unref(tu, "Unregistered telephone unit on pbx.");

    if (sem_post(&pbx->entries_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }
    if (sem_post(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }

    return 0;
}


/*
 * Use the PBX to initiate a call from a specified TU to a specified extension.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU that is initiating the call.
 * @param ext  The extension number to be called.
 * @return 0 if dialing succeeds, otherwise -1.
 *
 */


int pbx_dial(PBX *pbx, TU *tu, int ext) {
    if(pbx == NULL){
        fprintf(stderr, "Not a valid pbx.\n");
        return -1;
    }

    if (ext > PBX_MAX_EXTENSIONS-1){
        return -1;
    }

    if(tu == NULL){
        fprintf(stderr, "Not a valid telephone unit.\n");
        return -1;
    }

    if (sem_wait(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }

    debug("Find recipient %d", ext);
    TU *recipient = NULL;
    if (ext != -1 && pbx->pbx_entries[ext]){
        recipient = pbx->pbx_entries[ext];
    }

    if (tu_dial(tu, recipient)){
        if (sem_post(&pbx->pbx_mutex)){
            fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        }
        return -1;
    }

    if (sem_post(&pbx->pbx_mutex)){
        fprintf(stderr, "Could not successfully handle mutex for pbx.\n");
        return -1;
    }

    return 0;
}

