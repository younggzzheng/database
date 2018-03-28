#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include "./db.h"

#define MAXLEN 256
#define lock(lt, lk) ((lt) == l_read)? pthread_rwlock_rdlock(lk): pthread_rwlock_wrlock(lk)

// The root node of the binary tree, unlike all 
// other nodes in the tree, this one is never 
// freed (it's allocated in the data region).
node_t head = {"", "", 0, 0, PTHREAD_RWLOCK_INITIALIZER};
// constructs a node
node_t *node_constructor(char *arg_name, char *arg_value, node_t *arg_left, node_t *arg_right) {
    size_t name_len = strlen(arg_name);
    size_t val_len = strlen(arg_value);

    if (name_len > MAXLEN || val_len > MAXLEN)
        return 0;

    node_t *new_node = (node_t *)malloc(sizeof(node_t));
    
    if (new_node == 0)
        return 0;
    
    if ((new_node->name = (char *)malloc(name_len+1)) == 0) {
        free(new_node);
        return 0;
    }
    
    if ((new_node->value = (char *)malloc(val_len+1)) == 0) {
        free(new_node->name);
        free(new_node);
        return 0;
    }

    if ((snprintf(new_node->name, MAXLEN, "%s", arg_name)) < 0) {
        free(new_node->value);
        free(new_node->name);
        free(new_node);
        return 0;
    } else if ((snprintf(new_node->value, MAXLEN, "%s", arg_value)) < 0) {
        free(new_node->value);
        free(new_node->name);
        free(new_node);
        return 0;
    }

    new_node->lchild = arg_left;
    new_node->rchild = arg_right;
    return new_node;
}

// destroys a node
void node_destructor(node_t *node) {
    if (node->name != 0)
        free(node->name);
    if (node->value != 0)
        free(node->value);
    free(node);
}

// type for locking
enum locktype {l_read, l_write};

node_t *search(char *, node_t *, node_t **, enum locktype);

// queries for a key
void db_query(char *name, char *result, int len) {
    node_t *target;
    if (lock(l_read, &head.lock) == EDEADLK) {
        fprintf(stderr, "%s\n", "lock failed. deadlock1.");
        exit(1);
    }  
    target = search(name, &head, 0, l_read);
    
    if (target == 0) {
        snprintf(result, len, "not found");
        return;
    } else {
        snprintf(result, len, "%s", target->value);
        // UNLOCK thing that is found.
        if (pthread_rwlock_unlock(&target->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        return;
    }
}

// adds a new node into the tree
int db_add(char *name, char *value) {
    node_t *parent;
    node_t *target;
    node_t *newnode;
    pthread_rwlock_wrlock(&head.lock);
    if ((target = search(name, &head, &parent, l_write)) != 0) {
        pthread_rwlock_unlock(&target->lock);
        pthread_rwlock_unlock(&parent->lock);
        return(0);
    }

    newnode = node_constructor(name, value, 0, 0);
    pthread_rwlock_init(&newnode->lock, 0);

    if (strcmp(name, parent->name) < 0)
        parent->lchild = newnode;
    else
        parent->rchild = newnode;
    pthread_rwlock_unlock(&parent->lock);
    return(1);
}
// removes a the input argument from the tree.
int db_remove(char *name) {
    node_t *parent;
    node_t *dnode;
    node_t *next;

    // first, find the node to be removed 
    // rdlock the head before search.
    if (lock(l_read, &head.lock) == EDEADLK) {
        fprintf(stderr, "%s\n", "lock failed. deadlock2.");
        exit(1);
    }  
    if ((dnode = search(name, &head, &parent, l_write)) == 0) {
        // it's not there
        // unlock the parent
        if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        return(0);
    }

    // We found it, if the node has no
    // right child, then we can merely replace its parent's pointer to
    // it with the node's left child.

    if (dnode->rchild == 0) {
        if (strcmp(dnode->name, parent->name) < 0)
            parent->lchild = dnode->lchild;
        else
            parent->rchild = dnode->lchild;

        // done with dnode,  unlock dnode. 
        if (pthread_rwlock_unlock(&dnode->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        node_destructor(dnode);
        // unlock the parent after destroying dnode.
        if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
    } else if (dnode->lchild == 0) {
        // ditto if the node had no left child

        if (strcmp(dnode->name, parent->name) < 0)
            parent->lchild = dnode->rchild;
        else
            parent->rchild = dnode->rchild;
        
        // done with dnode,  unlock dnode. 
        if (pthread_rwlock_unlock(&dnode->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        node_destructor(dnode);
        
        // unloock the parent 
        if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
            exit(1);
        }  
    } else { // DNODE AND DNODE PARENT ARE LOCKED HERE.

        // Find the lexicographically smallest node in the right subtree and
        // replace the node to be deleted with that node. This new node thus is
        // lexicographically smaller than all nodes in its right subtree, and
        // greater than all nodes in its left subtree
        
        // wrlock the right child.
        if (lock(l_write, &dnode->rchild->lock) == EDEADLK) {
            fprintf(stderr, "%s\n", "lock failed. deadlock3.");
            exit(1);
        }  
        next = dnode->rchild;
        node_t **pnext = &dnode->rchild; // pnext is the connection between dnode and the child you're moving in the direction of. 
        // // make a node_t* pparent; 
        // node_t* next_parent = NULL;
        // next_parent = dnode;


        while (next->lchild != 0) {
            // work our way down the lchild chain, finding the smallest node
            // in the subtree.
            //  writelock the left child. 
            if (lock(l_write, &next->lchild->lock) == EDEADLK) { 
                fprintf(stderr, "%s\n", "lock failed. deadlock4.");
                exit(1);
            }  
            //  unlock the next. 
            if (pthread_rwlock_unlock(&next->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }
            
            
            node_t *nextl = next->lchild;
            pnext = &next->lchild;
            next = nextl;
        }
        // NEXT IS LOCKD ON OUTSIDE OF WHILE LOOP
        dnode->name = realloc(dnode->name, strlen(next->name)+1);
        dnode->value = realloc(dnode->value, strlen(next->value)+1);
        
        snprintf(dnode->name, MAXLEN, "%s", next->name);
        snprintf(dnode->value, MAXLEN, "%s", next->value);
        *pnext = next->rchild;
        // : unlock the next_parent. 
        if (pthread_rwlock_unlock(&next->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        node_destructor(next);
        // : unlock original parent. 
        if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
        //  : unlock dnode. 
        if (pthread_rwlock_unlock(&dnode->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
    }
    return(1);
}
    // Search the tree, starting at parent, for a node containing
    // name (the "target node").  Return a pointer to the node,
    // if found, otherwise return 0.  If parentpp is not 0, then it points
    // to a location at which the address of the parent of the target node
    // is stored.  If the target node is not found, the location pointed to
    // by parentpp is set to what would be the the address of the parent of
    // the target node, if it were there.
    //
node_t *search(char *name, node_t *parent, node_t **parentpp, enum locktype lt) {

    node_t *next;
    node_t *result;

    if (strcmp(name, parent->name) < 0) {
        next = parent->lchild;
    } else {
        next = parent->rchild;
    }

    if (next == NULL) {
        result = NULL;
    } else {
    if (lock(lt, &next->lock) == EDEADLK) {
        fprintf(stderr, "%s\n", "lock failed. deadlock5.");
        exit(1);
    }      
    if (strcmp(name, next->name) == 0) {
        result = next;
        } else {
            if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
                fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
                exit(1);
            }  
            return search(name, next, parentpp, lt);
        }
    }

    if (parentpp != NULL) {
        *parentpp = parent;
    } else {
        // unlock parent
        if (pthread_rwlock_unlock(&parent->lock) == EPERM) {
            fprintf(stderr, "%s\n", "unlock failed. wasn't locked");
            exit(1);
        }  
    }
    return result;
}

static inline void print_spaces(int lvl, FILE *out) {
    for (int i = 0; i < lvl; i++) {
        fprintf(out, " ");
    }
}

/* Recursively traverses the database tree and prints nodes
 * pre-order. */
void db_print_recurs(node_t *node, int lvl, FILE *out) {
    // print spaces to differentiate levels
    print_spaces(lvl, out);

    // print out the current node
    if (node == NULL) {
        fprintf(out, "(null)\n");
        return;
    }

    if (node == &head) {
        fprintf(out, "(root)\n");
    } else {
        fprintf(out, "%s %s\n", node->name, node->value);
    }

    db_print_recurs(node->lchild, lvl + 1, out);
    db_print_recurs(node->rchild, lvl + 1, out);
}

/* Prints the whole database, using db_print_recurs, to a file with
 * the given filename, or to stdout if the filename is empty or NULL.
 * If the file does not exist, it is created. The file is truncated
 * in all cases.
 *
 * Returns 0 on success, or -1 if the file could not be opened
 * for writing. */
int db_print(char *filename) {
    FILE *out;
    if (filename == NULL) {
        db_print_recurs(&head, 0, stdout);
        return 0;
    }
    
    // skip over leading whitespace
    while (isspace(*filename)) {
        filename++;
    }

    if (*filename == '\0') {
        db_print_recurs(&head, 0, stdout);
        return 0;
    }

    if ((out = fopen(filename, "w+")) == NULL) {
        return -1;
    }

    db_print_recurs(&head, 0, out);
    fclose(out);

    return 0;
}

/* Recursively destroys node and all its children. */
void db_cleanup_recurs(node_t *node) {
    if (node == NULL) {
        return;
    }

    db_cleanup_recurs(node->lchild);
    db_cleanup_recurs(node->rchild);

    node_destructor(node);
}

/* Destroys all nodes in the database other than the head.
 * No threads should be using the database when this is called. */
void db_cleanup() {
    db_cleanup_recurs(head.lchild);
    db_cleanup_recurs(head.rchild);
}

/* Interprets the given command string and calls the appropriate database
 * function. Writes up to len-1 bytes of the response message string produced 
 * by the database to the response buffer. */
void interpret_command(char *command, char *response, int len) {
    char value[MAXLEN];
    char ibuf[MAXLEN];
    char name[MAXLEN];
    int sscanf_ret;

    if (strlen(command) <= 1) {
        snprintf(response, len, "ill-formed command");
        return;
    }

    // which command is it?
    switch (command[0]) {
    case 'q':
         // Query
        sscanf_ret = sscanf(&command[1], "%255s", name);
        if (sscanf_ret < 1) {
            snprintf(response, len, "ill-formed command");
            return;
        }
        db_query(name, response, len);
        if (strlen(response) == 0) {
            snprintf(response, len, "not found");
        }

        return;

    case 'a':
        // Add to the database
        sscanf_ret = sscanf(&command[1], "%255s %255s", name, value);
        if (sscanf_ret < 2) {
            snprintf(response, len, "ill-formed command");
            return;
        }
        if (db_add(name, value)) {
            snprintf(response, len, "added");
        } else {
            snprintf(response, len, "already in database");
        }

        return;

    case 'd':
        // Delete from the database
        sscanf_ret = sscanf(&command[1], "%255s", name);
        if (sscanf_ret < 1) {
            snprintf(response, len, "ill-formed command");
            return;
        }
        if (db_remove(name)) {
            snprintf(response, len, "removed");
        } else {
            snprintf(response, len, "not in database");
        }

        return;

    case 'f':
        // process the commands in a file (silently)
        sscanf_ret = sscanf(&command[1], "%255s", name);
        if (sscanf_ret < 1) {
            snprintf(response, len, "ill-formed command");
            return;
        }

        FILE *finput = fopen(name, "r");
        if (!finput) {
            snprintf(response, len, "bad file name");
            return;
        }
        while (fgets(ibuf, sizeof(ibuf), finput) != 0) {
            pthread_testcancel();  // fgets is not a cancellation point
            interpret_command(ibuf, response, len);
        }
        fclose(finput);
        snprintf(response, len, "file processed");
        return;

    default:
        snprintf(response, len, "ill-formed command");
        return;
    }
}
