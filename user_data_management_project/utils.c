// #include <linux/math.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

#include "userdatamgmt_driver.h"
#include "userdatamgmt_fs.h"
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
    // Checks if the mount point of 'mount_md' is not equal to NULL, and returns -ENODEV if it is.
    if (mount_md.mount_point == NULL)
        return -ENODEV;
redo:
    msleep(PERIOD);
    AUDIT printk("%s: house keeper re-started\n",MOD_NAME);
    // Acquires the spin lock associated with the 'write_lock' attribute of the 'rcu' structure using spin_lock() function.
    spin_lock(&(rcu.write_lock));
    // Updates the 'updated_epoch' variable by assigning MASK if 'next_epoch_index' attribute of the 'rcu' structure is non-zero, else assigns 0 to it.
    updated_epoch = (rcu.next_epoch_index) ? MASK : 0;
    // Increments the value of 'next_epoch_index' attribute of the 'rcu' structure by 1 and takes the modulus 2 of it.
    rcu.next_epoch_index += 1;
    rcu.next_epoch_index %= 2;
    // Exchanges the value of 'epoch' attribute of the 'rcu' structure with 'updated_epoch' using __atomic_exchange_n() function and assigns the previous value stored in 'epoch' to 'last_epoch'.
    last_epoch = __atomic_exchange_n(&(rcu.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    // Computes the value of 'index' as 1 if the least significant bit of 'last_epoch' is set, else as 0.
    index = (last_epoch & MASK) ? 1 : 0;
    // Computes the value of 'grace_period_threads' as the value of 'last_epoch' without the least significant bit.
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("%s: house keeping: waiting grace-full period (target index is %ld)\n",MOD_NAME, grace_period_threads);
    // Calls wait_event_interruptible() function which waits on the 'wait_queue' until 'standing[index]' of the 'rcu' structure is greater than or equal to 'grace_period_threads' and can be interrupted by a signal.
    wait_event_interruptible(wait_queue, rcu.standing[index] >= grace_period_threads);
    // Sets 'standing[index]' attribute of the 'rcu' structure to zero.
    rcu.standing[index] = 0;
    // Releases the spin lock associated with the 'write_lock' attribute of the 'rcu' structure using spin_unlock() function.
    spin_unlock(&rcu.write_lock);
    // Uses goto statement to go back to the redo label and repeat the above steps in an infinite loop.
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

/*This function inserts a new node into a sorted doubly-linked list based on the position.

Parameters:
struct message **head: Pointer to the head of the list
struct message **tail: Pointer to the tail of the list
struct message *to_insert: Pointer to the message to be inserted
Return Value:
This function does not return anything.*/
void insert_sorted(struct message **head, struct message **tail, struct message *to_insert)
{   
    struct message *curr = NULL;
    int position = 0;

    if (to_insert == NULL) return;
    position = to_insert->ordering.position;
    to_insert->prev = *tail;
    to_insert->next = NULL;


    if (*head == NULL)
    { // se la lista è vuota
        *head = to_insert;
        *tail = to_insert;
    }
    else
    {
        curr = *head;
        while (curr != NULL &&  is_before(curr->ordering.position, position)) //to avoid overflow comparison
        {
            curr = curr->next;
        }

        if (curr == NULL)
        { // inserimento in coda
            (*tail)->next = to_insert;
            *tail = to_insert;
        }
        else
        { // inserimento in mezzo alla lista
            to_insert->next = curr;
            to_insert->prev = curr->prev;

            if (curr->prev != NULL)
            {
                curr->prev->next = to_insert;
            }
            curr->prev = to_insert;

            if (curr == *head)
            {
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

/*This function frees the memory allocated in the array pointed by the global variable head.

Parameters:
struct blk_element **head: Pointer to the head of the array
Return Value:
This function does not return anything.*/
void free_array(struct blk_element **head)
{
    int i;
    /* It then frees the memory allocated for the array and sorted list using the kfree (or vmalloc) and free_list() functions, respectively.*/
    for (i = 0; i < nblocks; i++)
    {
        if (head[i] == NULL)
            break;
        kfree(head[i]);
    }

    // Freeing the head of the array...
    if (sizeof(struct blk_element *) * nblocks < 128 * 1024)
        kfree(head);
    else
        vfree(head);
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

/*This function is used for debugging purposes. It prints the indices and positions stored in each node of a given list.

Parameters:
struct message *head: A pointer to the head of the linked list.
*/
void print_list(struct message *head)
{
    while (head != NULL)
    {
        AUDIT printk("%s: INDEX: %d - INSERT_INDEX: %d", MOD_NAME, head->index, head->ordering.insert_index);
        head = head->next;
    }
    return;
}

/*This function is used for debugging purposes. It prints the indices and positions in each node of a given list.

Parameters:
struct message *tail: A pointer to the tail of the linked list.
*/
void print_reverse(struct message *tail)
{
    while (tail != NULL)
    {
        AUDIT printk("%s: INDEX: %d - INSERT_INDEX: %d", MOD_NAME, tail->index, tail->ordering.insert_index);
        tail = tail->prev;
    }
    return;
}

/*This function searches for a message in a linked list based on the position of the element.
Parameters:
struct message*: A pointer to the head of the double linked list.
int insert_index: An integer representing the value to be searched*/
struct message *lookup_by_insert_index(struct message *head, unsigned int insert_index)
{

    struct message *curr = head;
    while (curr != NULL && is_before(curr->ordering.insert_index, insert_index)) //to avoid overflow comparison
    {
        curr = curr->next;
    }

    return curr;
}

/*This function searches for a message in a double-linked list based on the index of the element.
Parameters:
struct message*: A pointer to the head of the double linked list.
int index: An integer representing the index in the device driver to be searched*/
struct message *lookup_by_index(struct message *head, int index)
{

    struct message *curr = head;

    while (curr != NULL && curr->index != index)
    {
        curr = curr->next;
    }

    return curr;
}

// swapNodes takes 2 parameters two nodes to be swapped.
void swapNodes(struct message *node1, struct message *node2)
{   
    int temp;
    struct blk_element * temp_elem;

    if (node1 == node2)
    {
        return;
    }

    /* Swapping the insert_index*/
    temp = node1->ordering.insert_index;
    node1->ordering.insert_index = node2->ordering.insert_index;
    node2->ordering.insert_index = temp;

    /* Swapping the index*/
    temp = node1->index;
    node1->index = node2->index;
    node2->index = temp;

    /* Swapping blk_element*/
    temp_elem = node1->elem;
    node1->elem = node2->elem;
    node2->elem = temp_elem;

}

/*partition takes two nodes as its parameters, a lower node and a higher node. It finds the pivot by selecting the last item in the list,
and iterates through the list checking if each insert_index is less than or equal to the pivot value.
If it is, it swaps that node with the node at the 'i' insert_index, where 'i' is one greater than the current 'i'.
When all items up until the high node have been processed in this way,
then the node at the 'i' insert_index is swapped with the high node thereby providing us with the pivot element.*/
static struct message *partition(struct message *start, struct message *end)
{   
    struct message *pivot = end;
    struct message *i = start->prev;
    struct message *j = NULL;

    if (start == end || start == NULL || end == NULL)
        return start;

    for (j = start; j != end; j = j->next)
    {
        if (is_before_eq(j->ordering.insert_index, pivot->ordering.insert_index)) //to avoid overflow comparison
        {   
            i = (i == NULL ? start : i->next);
            swapNodes(i, j);
        }
    }
    i = (i == NULL ? start : i->next);
    swapNodes(i, end);
    return i;
}

/*quickSort performs the main sorting logic. It takes two nodes as its parameters, a low node and a high node.
 it calls findPivot to get the pivot element, then recursively calls quickSort function for the lower half and upper half of the linked list based on the pivot element.
 The resulting sorted list is obtained by combining the two halves.*/
void quickSort(struct message *start, struct message *end)
{   
    struct message *pivot = NULL;
    if (start == NULL || start == end || start == end->next)
        return;

    pivot = partition(start, end);
    quickSort(start, pivot->prev);

    // if pivot is picked and moved to the start, that means start and pivot is same so pick from next of pivot
    if (pivot != NULL && pivot == start)
        quickSort(pivot->next, end);

    // if pivot is in between of the list, start from next of pivot,
    else if (pivot != NULL && pivot->next != NULL)
        quickSort(pivot->next, end);
}
