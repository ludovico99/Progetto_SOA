// #include <linux/math.h>
// #include <linux/log2.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "userdatamgmt_driver.h"

/*This function is a kernel thread function which periodically checks the grace period for RCU.

Parameters:
void *unused: An unused pointer parameter.
Return Value:
Returns 0 upon successful execution of the function.*/
static int house_keeper(void *unused)
{

    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    DECLARE_WAIT_QUEUE_HEAD(wait_queue);
    //Checks if the mount point of 'mount_md' is not equal to NULL, and returns -ENODEV if it is.
    if (mount_md.mount_point == NULL)
        return -ENODEV;
redo:
    msleep(PERIOD);
    //Acquires the spin lock associated with the 'write_lock' attribute of the 'sh_data' structure using spin_lock() function.
    spin_lock(&sh_data.write_lock);
    //Updates the 'updated_epoch' variable by assigning MASK if 'next_epoch_index' attribute of the 'sh_data' structure is non-zero, else assigns 0 to it.
    updated_epoch = (sh_data.next_epoch_index) ? MASK : 0;
    //Increments the value of 'next_epoch_index' attribute of the 'sh_data' structure by 1 and takes the modulus 2 of it.
    sh_data.next_epoch_index += 1;
    sh_data.next_epoch_index %= 2;
    //Exchanges the value of 'epoch' attribute of the 'sh_data' structure with 'updated_epoch' using __atomic_exchange_n() function and assigns the previous value stored in 'epoch' to 'last_epoch'.
    last_epoch = __atomic_exchange_n(&(sh_data.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    //Computes the value of 'index' as 1 if the least significant bit of 'last_epoch' is set, else as 0.
    index = (last_epoch & MASK) ? 1 : 0;
    //Computes the value of 'grace_period_threads' as the value of 'last_epoch' without the least significant bit.
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("house keeping: waiting grace-full period (target index is %ld)\n", grace_period_threads);
    //Calls wait_event_interruptible() function which waits on the 'wait_queue' until 'standing[index]' of the 'sh_data' structure is greater than or equal to 'grace_period_threads' and can be interrupted by a signal.
    wait_event_interruptible(wait_queue, sh_data.standing[index] >= grace_period_threads);
    //Sets 'standing[index]' attribute of the 'sh_data' structure to zero.
    sh_data.standing[index] = 0;
    //Releases the spin lock associated with the 'write_lock' attribute of the 'sh_data' structure using spin_unlock() function.
    spin_unlock(&sh_data.write_lock);
    //Uses goto statement to go back to the redo label and repeat the above steps in an infinite loop.
    goto redo;

    return 0;
}

/*This function initializes a given rcu_data struct with the initial values.

Parameters:
struct rcu_data *t: A pointer to the data structure to be initialized.
*/
void init(struct rcu_data *t)
{

    int i;
    char name[128] = "the_daemon";
    struct task_struct *the_daemon;

    t->epoch = 0x0;
    t->next_epoch_index = 0x1;
    for (i = 0; i < EPOCHS; i++)
    {
        t->standing[i] = 0x0;
    }
    t->head = NULL;
    t->first = NULL;
    spin_lock_init(&t->write_lock);
    the_daemon = kthread_create(house_keeper, NULL, name);

    if (the_daemon)
    {
        wake_up_process(the_daemon);
        printk("%s: RCU-tree house-keeper activated\n", MOD_NAME);
    }

    else
    {
        AUDIT printk("%s: Kernel daemon initialization has failed \n", MOD_NAME);
    }
}

/*ITERATIVE : This function searches for a node with an index matching the given index in the binary search tree pointed to by 'root'.

Parameters:
struct blk_element *root: A pointer to the root of the binary search tree.
int index: An integer value representing the index of the node to be searched.
Return Value:
Returns a pointer to the node with an index matching the given 'index' value if found in the binary search tree, else returns NULL.
*/
 struct blk_element *tree_lookup(struct blk_element *root, int index)
 {
     struct blk_element * curr = root;
     while (curr != NULL) {
         if (index == curr->index) {
             AUDIT printk("%s: lookup operation completed for block with index %d", MOD_NAME, index);
             return curr;
         } else if (index < curr->index) {
             // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
             curr = curr->left;
         } else {
             // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
             curr = curr->right;
         }
     }
     return NULL;
 }

/*struct blk_element *tree_lookup(struct blk_element *root, int index)
{

    if (root == NULL)
    {
        return NULL;
    }

    if (index > root->index)
    {
        // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
        return tree_lookup(root->right, index);
    }
    else if (index < root->index)
    {
        // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
        return tree_lookup(root->left, index);
    }
    else
    {
        AUDIT printk("%s: lookup operation completed for block with index %d", MOD_NAME, index);
        return root;
    }
}*/

/*ITERATIVE: This function inserts a new node 'newNode' into the binary search tree pointed to by '**root', while maintaining the order of nodes in the tree.

Parameters:
struct blk_element **root: Pointer to a pointer to the root of the binary search tree. This is a pointer to the topmost node in the tree.
struct blk_element *newNode: A pointer to the new node to be inserted into the binary search tree.
Return Value:
Returns void because it only modifies the binary search tree.*/
void tree_insert(struct blk_element **root, struct blk_element *newNode)
{
    int index = newNode->index;
    struct blk_element *curr = *root;
    struct blk_element *parent = NULL;
    // AUDIT printk("%s: insert operation started for block at index %d", MOD_NAME, index);
    if (*root == NULL)
    {
        *root = newNode;
        AUDIT printk("%s: insert operation completed for block with index %d", MOD_NAME, index);
        return;
    }

    while (curr != NULL)
    {
        parent = curr;

        if (index < curr->index)
        {
            // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
            curr = curr->left;
        }
        else
        {
            // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
            curr = curr->right;
        }
    }

    if (index < parent->index)
    {
        parent->left = newNode;
    }
    else
    {
        parent->right = newNode;
    }
}

// void tree_insert(struct blk_element **root, struct blk_element *newNode)
// {
//     int index = newNode->index;
//     // AUDIT printk("%s: insert operation started for block at index %d", MOD_NAME, index);
//     if (*root == NULL)
//     {
//         *root = newNode;
//         AUDIT printk("%s: insert operation completed for block with index %d", MOD_NAME, index);
//         return;
//     }

//     if (index > (*root)->index)
//     {
//         // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
//         tree_insert(&((*root)->right), newNode);
//     }
//     else if (index < (*root)->index)
//     {
//         // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
//         tree_insert(&((*root)->left), newNode);
//     }
// }

/*This function finds the node with the minimum value in a given subtree, starting from the given 'node'.

Parameters:
struct blk_element *node: Pointer to the root of the subtree
Return Value:
Returns a pointer to the node with the minimum value in the subtree.*/
static struct blk_element *find_min(struct blk_element *node)
{

    struct blk_element *curr = node;

    while (curr && curr->left != NULL)
    {
        curr = curr->left;
    }
    return curr;
}
/*This function deletes a node with the given index value from a binary search tree rooted at 'root'.

Parameters:
struct blk_element *root: Pointer to the root of the binary search tree.
int index: The index value of the node to be deleted.
Return Value:
Returns a pointer to the new root of the binary search tree after deletion (which may be the same as the old root).*/
struct blk_element *tree_delete(struct blk_element *root, int index)
{
    struct blk_element *temp = NULL;
    struct blk_element *min_node = NULL;
    if (root == NULL)
    {
        return root;
    }
    if (index < root->index)
    {
        root->left = tree_delete(root->left, index);
        asm volatile("mfence"); // make it visible to readers
    }
    else if (index > root->index)
    {
        root->right = tree_delete(root->right, index);
        asm volatile("mfence"); // make it visible to readers
    }
    else
    {
        // Node to be deleted has 0 or 1 child
        if (root->left == NULL)
        {
            temp = root->right;
            // kfree(root);
            return temp;
        }
        else if (root->right == NULL)
        {
            temp = root->left;
            // kfree(root);
            return temp;
        }
        // Node to be deleted has 2 children
        min_node = find_min(root->right);
        root->index = min_node->index;
        root->right = tree_delete(root->right, min_node->index);
        asm volatile("mfence"); // make it visible to readers
    }
    return root;
}

// Iterative: Funzione per eliminare un nodo con un dato index dall'albero binario
// bool tree_delete_it(struct blk_element **root, int index)
// {
//     struct blk_element *curr = *root;
//     struct blk_element *parent = NULL;
//     struct blk_element *min_node = NULL;
//     bool isLeftChild = false;

//     if (curr == NULL)
//     {
//         return false;
//     }

//     while (curr->index != index) {
//         parent = curr;

//         if (index < curr->index) {
//             curr = curr->left;
//             isLeftChild = true;
//         } else {
//             curr = curr->right;
//             isLeftChild = false;
//         }

//         if (curr == NULL) {
//             return false;
//         }
//     }

//      // caso 1: il nodo da eliminare non ha figli
//     if (curr->left == NULL && curr->right == NULL) {
//         if (curr == *root) {
//             *root = NULL;
//             asm volatile("mfence");
//         } else if (isLeftChild) {
//             parent->left = NULL;
//             asm volatile("mfence");
//         } else {
//             parent->right = NULL;
//             asm volatile("mfence");
//         }
//     }
//     // caso 2: il nodo da eliminare ha solo un figlio
//     else if (curr->left == NULL) {
//         if (curr == *root) {
//             *root = curr->right;
//             asm volatile("mfence");
//         } else if (isLeftChild) {
//             parent->left = curr->right;
//             asm volatile("mfence");
//         } else {
//             parent->right = curr->right;
//             asm volatile("mfence");
//         }
//     } else if (curr->right == NULL) {
//         if (curr == *root) {
//             *root = curr->left;
//             asm volatile("mfence");
//         } else if (isLeftChild) {
//             parent->left = curr->left;
//             asm volatile("mfence");
//         } else {
//             parent->right = curr->left;
//             asm volatile("mfence");
//         }
//     }
//     // caso 3: il nodo da eliminare ha due figli
//     else {
//         min_node = find_min(curr->right);
//         tree_delete(root, min_node->index);
//         curr->index = min_node->index;
//         asm volatile("mfence");
//     }

//     return true;
// }

/*This function is used to print the indices stored in each node of a binary tree.

Parameters:
struct blk_element *root: A pointer to the root of the binary tree.*/
void stampa_albero(struct blk_element *root)
{
    if (root != NULL)
    {
        stampa_albero(root->left);
        AUDIT printk("%d", root->index);
        stampa_albero(root->right);
    }
}

/*This function takes a pointer to the root node of a binary tree containing blk_elements and returns a pointer to the first element encountered in an in-order traversal of the tree that is either invalid or is valid and free.

Parameters:
struct blk_element *root: Pointer to the root node of the binary tree
Return Value:
Returns a pointer to the first blk_element encountered in an in-order traversal of the binary tree that meets the specified conditions (invalid or valid and free)*/
struct blk_element *inorderTraversal(struct blk_element *root)
{
    struct blk_element *left_node = NULL;
    if (root == NULL)
    {
        return NULL;
    }
    // Return a block that is invalid or is valid and free
    if (!get_validity(root->metadata) || (get_validity(root->metadata) && get_free(root->metadata)))
        return root;

    left_node = inorderTraversal(root->left);
    if (left_node != NULL)
    {
        return left_node;
    }
    return inorderTraversal(root->right);
}
/*This function takes a pointer to the root node of a binary tree containing blk_elements and frees all the memory allocated for the nodes in the tree.

Parameters:
struct blk_element *root: Pointer to the root node of the binary tree
Return Value:
This function does not return anything.*/
void free_tree(struct blk_element *root)
{
    if (root == NULL)
    {
        return;
    }

    free_tree(root->left);
    free_tree(root->right);

    kfree(root);
}
/*This function inserts a new node at the end of a doubly-linked list.

Parameters:
struct message **head: Pointer to the head of the list
struct message **tail: Pointer to the tail of the list
struct message *to_insert: Pointer to the message to be inserted
Return Value:
This function does not return anything.*/
void insert(struct message **head, struct message **tail, struct message *to_insert)
{

    to_insert->prev = *tail;
    to_insert->next = NULL;
    if (*tail == NULL)
    {
        *head = to_insert;
        *tail = to_insert;
        return;
    }
    (*tail)->next = to_insert;
    *tail = to_insert;
}
/*This function inserts a new node into a sorted doubly-linked list based on the value of an index in the 'elem' field of each node.

Parameters:
struct message **head: Pointer to the head of the list
struct message **tail: Pointer to the tail of the list
struct message *to_insert: Pointer to the message to be inserted
Return Value:
This function does not return anything.*/
void insert_sorted(struct message ** head, struct message **tail, struct message *to_insert) {

    struct message* curr;
    int index = to_insert -> elem ->index;
    to_insert->prev = *tail;
    to_insert->next = NULL;

    if (*head == NULL) { // se la lista è vuota
        *head = to_insert;
        *tail = to_insert;
    }
    else {
        curr = *head;
        while (curr != NULL && curr->elem->index < index) {
            curr = curr->next;
        }

        if (curr == NULL) { // inserimento in coda
            (*tail)->next = to_insert;
             *tail = to_insert;
        }
        else { // inserimento in mezzo alla lista
            to_insert->next = curr;
            to_insert->prev = curr->prev;

            if (curr->prev != NULL) {
                curr->prev->next = to_insert;
            }
            curr->prev = to_insert;

            if (curr == *head) {
                *head = to_insert;
            }
        }
    }
}


/*This function frees the memory allocated to each node in a doubly-linked list.

Parameters:
struct message *head: Pointer to the head of the list
Return Value:
This function does not return anything.*/
void free_list(struct message *head)
{
    struct message *curr = head;
    struct message *next;

    while (curr != NULL)
    {
        next = curr->next;
        kfree(curr);
        curr = next;
    }
}

/*This function deletes a specific node from a doubly-linked list.

Parameters:
struct message **head: Pointer to the head of the list
struct message **tail: Pointer to the tail of the list
struct message *to_delete: Pointer to the message to be deleted
Return Value:
This function does not return anything.*/
void delete(struct message **head, struct message **tail, struct message *to_delete)
{

    if (*head == NULL || to_delete == NULL)
    {
        return;
    }

    if (*head == to_delete)
    {
        *head = (*head)->next;
        asm volatile("mfence"); // make it visible to readers
    }

    if (*tail == to_delete)
    {
        *tail = (*tail)->prev;
        asm volatile("mfence"); // make it visible to readers
    }

    if (to_delete->next != NULL)
    {
        to_delete->next->prev = to_delete->prev;
        asm volatile("mfence"); // make it visible to readers
    }

    if (to_delete->prev != NULL)
    {
        to_delete->prev->next = to_delete->next;
        asm volatile("mfence"); // make it visible to readers
    }
    AUDIT printk("%s: The delete operation correctly completed", MOD_NAME);
}

/*This function is used for debugging purposes. It prints the indices stored in each node of a given list.

Parameters:
struct message *root: A pointer to the root of the linked list.
*/
void stampa_lista(struct message *root)
{
    while (root != NULL)
    {
        AUDIT printk("%d ", root->elem->index);
        root = root->next;
    }
    return;
}

/*This function recursively constructs an array of balanced indices from a sorted array.

Parameters:
int *array: A pointer to an integer array.
int start: An integer representing the start of the sorted array.
int end: An integer representing the end of the sorted array.
int *index: A pointer to an integer representing the current position in the array.*/
void get_balanced_indices(int *array, int start, int end, int *index)
{
    int mid;

    if (start > end)
    {
        return;
    }

    mid = (start + end) / 2;

    // Aggiungere l'indice di mezzo all'array di indici
    array[*index] = mid;
    (*index)++;
    // Ricorsivamente costruire il sottoarray sinistro dell'array di indici
    get_balanced_indices(array, start, mid - 1, index);

    // Ricorsivamente costruire il sottoarray destro dell'array di indici
    get_balanced_indices(array, mid + 1, end, index);

    return;
}
