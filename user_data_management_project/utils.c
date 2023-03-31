// #include <linux/math.h>
// #include <linux/log2.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "userdatamgmt_driver.h"

static int house_keeper(void *unused)
{

    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    DECLARE_WAIT_QUEUE_HEAD(wait_queue);
    if (mount_md.mount_point == NULL)
        return -ENODEV;
redo:
    msleep(PERIOD);

    spin_lock(&sh_data.write_lock);

    updated_epoch = (sh_data.next_epoch_index) ? MASK : 0;

    sh_data.next_epoch_index += 1;
    sh_data.next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(sh_data.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0;
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("house keeping: waiting grace-full period (target index is %ld)\n", grace_period_threads);
    wait_event_interruptible(wait_queue, sh_data.standing[index] >= grace_period_threads);
    sh_data.standing[index] = 0;

    spin_unlock(&sh_data.write_lock);

    goto redo;

    return 0;
}

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

//ITERATIVE : tree_lookup
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

// struct blk_element *tree_lookup(struct blk_element *root, int index)
// {

//     if (root == NULL)
//     {
//         return NULL;
//     }

//     if (index > root->index)
//     {
//         // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
//         return tree_lookup(root->right, index);
//     }
//     else if (index < root->index)
//     {
//         // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
//         return tree_lookup(root->left, index);
//     }
//     else
//     {
//         AUDIT printk("%s: lookup operation completed for block with index %d", MOD_NAME, index);
//         return root;
//     }
// }

// ITERATIVE: tree_insert
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

// Trova il nodo con valore minimo nell'albero
static struct blk_element *find_min(struct blk_element *node)
{

    struct blk_element *curr = node;

    while (curr && curr->left != NULL)
    {
        curr = curr->left;
    }
    return curr;
}

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

// For debugging purposes
void stampa_albero(struct blk_element *root)
{
    if (root != NULL)
    {
        stampa_albero(root->left);
        AUDIT printk("%d", root->index);
        stampa_albero(root->right);
    }
}

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

// For debugging purposes
void stampa_lista(struct message *root)
{
    while (root != NULL)
    {
        AUDIT printk("%d ", root->elem->index);
        root = root->next;
    }
    return;
}

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
