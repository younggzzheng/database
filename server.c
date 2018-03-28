#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "./db.h"
#include "./comm.h"
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#ifdef __APPLE__
#include "pthread_OSX.h"
#endif

// wrapper method for unlocking mutexes
void cleanup_pthread_mutex_unlock(void *arg) {
    pthread_mutex_unlock((pthread_mutex_t *) arg);
}

int accepting_clients = 1;
/* 
 * Use the variables in this struct to synchronize your main thread with client
 * threads. Note that all client threads must have terminated before you clean
 * up the database. 
 */
typedef struct server_control {
    pthread_mutex_t server_mutex;
    pthread_cond_t server_cond;
    int num_client_threads;
} server_control_t;

/*
 * Controls when the clients in the client thread list should be stopped and
 * let go.
 */
typedef struct client_control {
    pthread_mutex_t go_mutex;
    pthread_cond_t go;
    int stopped;
} client_control_t;

/*
 * The encapsulation of a client thread, i.e., the thread that handles
 * commands from clients.
 */
typedef struct client {
    pthread_t thread;
    FILE *cxstr;  // File stream for input and output
    // For client list
    struct client *prev;
    struct client *next;
} client_t;

/*
 * The encapsulation of a thread that handles signals sent to the server.
 * When SIGINT is sent to the server all client threads should be destroyed.
 */
typedef struct sig_handler {
    sigset_t set;
    pthread_t thread;
} sig_handler_t;

client_t *thread_list_head;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void *run_client(void *arg);
void *monitor_signal(void *arg);
void thread_cleanup(void *arg);

client_control_t *c_control;
server_control_t *s_control;

// Called by client threads to wait until progress is permitted
void client_control_wait() {
    // Block the calling thread until the main thread calls
    // client_control_release()
    pthread_mutex_lock(&c_control->go_mutex);
    pthread_cleanup_push(&cleanup_pthread_mutex_unlock, (void *) &c_control->go_mutex);
    while (c_control->stopped == 1) {
        pthread_cond_wait(&c_control->go, &c_control->go_mutex);
    }
    pthread_cleanup_pop(1);
}
// Called by main thread to stop client threads
void client_control_stop() {
    c_control->stopped = 1;
}

// Called by main thread to resume client threads
// Allows clients that are blocked within client_control_wait()
void client_control_release() {
    c_control->stopped = 0;
    pthread_cond_broadcast(&c_control->go);
}

// Called by listener (in comm.c) to create a new client thread
void client_constructor(FILE *cxstr) {
    // You should create a new client_t struct here and initialize ALL
    // of its fields. Remember that these initializations should be 
    // error-checked.
    // 
    // Step 1: Allocate memory for a new client and set its connection stream
    // to the input argument.
    // For client list
    client_t *client = malloc(sizeof(client_t));
    client -> cxstr = cxstr;
    client -> prev = NULL;
    client -> next = NULL;
    // Step 2: Create the new client thread running the run_client routine.
    int err;

    if ((err = pthread_create(&client->thread, 0, run_client, (void *) client)))
        handle_error_en(err, "pthread_create");
    if ((err = pthread_detach(client->thread)))
        handle_error_en(err, "pthread_detach");
}
// Free all resources associated with a client.
// Whatever was malloc'd in client_constructor should 
// be freed here!
void client_destructor(client_t *client) {
    comm_shutdown(client->cxstr); // closing the file.
    free(client);
}

// Code executed by a client thread
void *run_client(void *arg) {
    client_t *client = arg;
    // Step 1: Make sure that the server is still accepting clients.
    //         NOTE: you can use a global variable to keep track if server is still accepting clients
    if (accepting_clients != 1) {
        client_destructor(client); 
        return NULL; 
    }

    // Step 2: Add client to the client list and push thread_cleanup to remove
    //       it if the thread is canceled.
    // LOCK A MUTEX HERE
    pthread_mutex_lock(&thread_list_mutex);
    if (thread_list_head == NULL) {  // it's the only element in the client list, so no more needs to be done.
        thread_list_head = client;
} else {
    client_t *curr = thread_list_head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr -> next = client;
    client -> prev = curr;
    client -> next = NULL;
}
pthread_mutex_lock(&s_control->server_mutex);
s_control->num_client_threads++;
pthread_mutex_unlock(&s_control->server_mutex);
pthread_mutex_unlock(&thread_list_mutex);
pthread_cleanup_push(thread_cleanup, (void *) client);

    // Step 3: Loop comm_serve (in comm.c) to receive commands and output
    //       responses. Note that the client may terminate the connection at
    //       any moment, in which case reading/writing to the connection stream
    //       on the server side will send this process a SIGPIPE. You must
    //       ensure that the server doesn't crash when this happens!
char response[BUFLEN];
char command[BUFLEN];    
memset(response, '\0', BUFLEN);
memset(command, '\0', BUFLEN);

    // setting up signal ignoring. 
sigset_t set;
sigemptyset(&set);
sigaddset(&set, SIGPIPE);
pthread_sigmask(SIG_BLOCK, &set, 0);


while (comm_serve(client->cxstr, response, command) == 0) {
    client_control_wait();
    interpret_command(command, response, BUFLEN);
}

    // Step 4: When the client is done sending commands, exit the thread
    //       cleanly.

pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
pthread_cleanup_pop(1);
    // Keep the signal handler thread in mind when writing this function!
return NULL;
}

void delete_all() {
    pthread_mutex_lock(&thread_list_mutex);
    // Cancel every thread in the client thread list with the 
    // pthread_cancel function.
    accepting_clients = 0;
    client_t *curr = thread_list_head;

    while (curr != NULL) { 
     pthread_cancel(curr->thread);
     curr = curr->next;
 }
 pthread_mutex_unlock(&thread_list_mutex);
}

// Cleanup routine for client threads, called on cancels and exit.
void thread_cleanup(void *arg) { // takes in the client
    // Remove the client object from thread list
    client_t *client = (client_t *)arg;
    // edge case: to delete is at the front of the list.
    if (client == thread_list_head) { 
        if (client -> next == NULL) { // it's the only item in the list.
            thread_list_head = NULL;
    } else {
        pthread_mutex_lock(&thread_list_mutex);
        thread_list_head = client -> next ;
        thread_list_head -> prev = NULL;
        pthread_mutex_unlock(&thread_list_mutex);
    }
} else {
        if (client->next == NULL) { // edge case: to delete is at the end of the list.
            pthread_mutex_lock(&thread_list_mutex);
            client -> prev -> next = NULL;
            pthread_mutex_unlock(&thread_list_mutex);
        } else { // general case: somewhere in the middle of the list
            pthread_mutex_lock(&thread_list_mutex);
            client -> prev -> next = client -> next;
            client -> next -> prev = client -> prev;
            pthread_mutex_unlock(&thread_list_mutex);
        }
    }
    client_destructor(client);
    // lock the server control mutex 
    pthread_mutex_lock(&s_control->server_mutex);
    // pthread_cond_signal to signal the server cond
    // while the length of the list is greater than zero, use pthread_cond_wait. 
    s_control->num_client_threads--;
    
    if (s_control->num_client_threads == 0) {
        pthread_cond_signal(&s_control->server_cond);
    }

    pthread_mutex_unlock(&s_control->server_mutex);

}

// Code executed by the signal handler thread. For the purpose of this
// assignment, there are two reasonable ways to implement this.
// The one you choose will depend on logic in sig_handler_constructor. 
// 'man 7 signal' and 'man sigwait' are both helpful for making this
// decision. One way or another, all of the server's client threads 
// should terminate on SIGINT. The server (this includes the listener 
// thread) should not, however, terminate on SIGINT!
void *monitor_signal(void *arg) {
    int sig;
    sig_handler_t *handler = (sig_handler_t *) arg;
    while (1) {
        if (sigwait(&handler->set, &sig) == 0) {
            delete_all();
            accepting_clients = 1;
        }
    }
    return NULL;
}
// creates a sig handler. to be called in main
sig_handler_t *sig_handler_constructor() {
    sig_handler_t *handler;
    handler = malloc(sizeof(sig_handler_t));

    // setting up signal ignoring. 
    sigemptyset(&handler->set);
    sigaddset(&handler->set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &handler->set, 0);

    // creating the new thread.
    int err;
    pthread_create(&handler->thread, 0, monitor_signal, (void *) handler);  // passing in the handler as argument to monitor.
    if ((err = pthread_detach((pthread_t) handler->thread)))
        handle_error_en(err, "pthread_detach");
    return handler;
}
// frees resources involve with a sig handler.
void sig_handler_destructor(sig_handler_t *sighandler) {
    pthread_cancel(sighandler->thread);
    free(sighandler);
}


// The arguments to the server should be the port number.
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "%s\n", "usage: server <port>");
        exit(1);
    }
    // mallocing controls
    c_control = malloc(sizeof(client_control_t)); 
    s_control = malloc(sizeof(server_control_t));
    
    // initalizing client control
    pthread_mutex_init(&c_control -> go_mutex, 0);
    pthread_cond_init(&c_control -> go, 0);
    c_control -> stopped = 0;
    
    // initalizing server control
    pthread_mutex_init(&s_control -> server_mutex, 0);
    pthread_cond_init(&s_control -> server_cond, 0);
    s_control -> num_client_threads = 0;

    // Step 1: Set up the signal handler.
    sig_handler_t *handler = sig_handler_constructor();
    // Step 2: Start a listener thread for clients (see start_listener in 
    //       comm.c).
    pthread_t listener_thread = start_listener(atoi(argv[1]), client_constructor);

    // Step 3: Loop for command line input and handle accordingly until EOF.

        /* READING FROM USER INPUT INTO BUFFER. */
    while (1) {
        char buf[BUFLEN];
        int input_bytes;
        input_bytes = (int) read(0, buf, sizeof(buf));
        if (input_bytes < 0) {
          fprintf(stderr, "Unable to read.");
      }
      if (input_bytes == 0) {
        break;        
    }   
        /* TOKENIZING INPUT TO GET ARGUMENTS. */
    char *command = strtok(buf, "\t \n");

    if (command == NULL) {
        continue;
    }

    char *potential_file = strtok(NULL, "\t \n");

    if (strcmp(command, "s") == 0) {
        client_control_stop();
    }
    if (strcmp(command, "g") == 0) {
        client_control_release();
    }
    if (strcmp(command, "p") == 0 && potential_file == NULL) {
        db_print(NULL);
    }
    if (strcmp(command, "p") == 0 && potential_file != NULL) {
        db_print(potential_file);
    } 
}
        // Step 4: Destroy the signal handler, delete all clients, cleanup the
        //       database, cancel the listener thread, and exit.
        // (1) destroy signal handler.
    sig_handler_destructor(handler);
        // (2) delete all clients
    delete_all();
        // lock the server control mutex 
    pthread_mutex_lock(&s_control->server_mutex);
        // while the length of the list is greater than zero, use pthread_cond_wait. 
    pthread_cleanup_push(&cleanup_pthread_mutex_unlock, (void *) &s_control->server_mutex);
    while (s_control->num_client_threads > 0) {
        pthread_cond_wait(&s_control->server_cond, &s_control->server_mutex);
    }
    pthread_cleanup_pop(1);
        // (3) cleanup the database
    db_cleanup();
        // (4) cancel the listener thread
    pthread_cancel(listener_thread);
        // destroy all mutex
    pthread_mutex_destroy(&thread_list_mutex);
    pthread_mutex_destroy(&c_control->go_mutex);
    pthread_mutex_destroy(&s_control->server_mutex);
        // destroy all cond.
    pthread_cond_destroy(&c_control->go);
    pthread_cond_destroy(&s_control->server_cond);
    pthread_exit(0);
}
