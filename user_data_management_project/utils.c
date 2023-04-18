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
    t->first = NULL;
    t->last = NULL;
    spin_lock_init(&t->write_lock);
    the_daemon = kthread_create(house_keeper, NULL, name);

    if (the_daemon)
    {
        wake_up_process(the_daemon);
        printk("%s: RCU-double linked list house-keeper activated\n", MOD_NAME);
    }

    else
    {
        AUDIT printk("%s: Kernel daemon initialization has failed \n", MOD_NAME);
    }
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
unsigned int position: Express the position in the double linked list where message should be placed
Return Value:
This function does not return anything.*/
void insert_sorted(struct message ** head, struct message **tail, struct message *to_insert, int position) {

    struct message* curr;
    to_insert->prev = *tail;
    to_insert->next = NULL;

    if (*head == NULL) { // se la lista è vuota
        *head = to_insert;
        *tail = to_insert;
    }
    else {
        curr = *head;
        while (curr != NULL && curr->position < position) {
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
        asm volatile("mfence"); 
    }

    if (to_delete->prev != NULL)
    {
        to_delete->prev->next = to_delete->next;
        asm volatile("mfence");
    }

    if (to_delete->next != NULL)
    {
        to_delete->next->prev = to_delete->prev;
        asm volatile("mfence"); // make it visible to readers
    }

    if (*tail == to_delete)
    {
        *tail = (*tail)->prev;
        asm volatile("mfence"); 
    }

    
    AUDIT printk("%s: The delete operation correctly completed", MOD_NAME);
}

/*This function is used for debugging purposes. It prints the indices stored in each node of a given list.

Parameters:
struct message *head: A pointer to the head of the linked list.
*/
void stampa_lista(struct message *head)
{
    while (head != NULL)
    {
        AUDIT printk("%d ", head->index);
        head = head->next;
    }
    return;
}

/*This function searches for a message in a linked list based on the position of the element.
Parameters:
struct message*: A pointer to the head of the double linked list.
int pos: An integer representing the value to be searched*/
struct message *search(struct message *head, int pos) {

    struct message *curr = head;

    while (curr != NULL && curr->position < pos) {
        curr = curr->next;
    }

    return curr;
}

/*This function searches for a message in a linked list based on the index of the element.
Parameters:
struct message*: A pointer to the head of the double linked list.
int index: An integer representing the index in the device driver to be searched*/
struct message *lookup(struct message *head, int index) {

    struct message *curr = head;

    while (curr != NULL && curr->index != index) {
        curr = curr->next;
    }

    return curr;
}