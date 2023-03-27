#include <linux/math.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "linux/kthread.h"
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

    spin_lock(&the_tree.write_lock);

    updated_epoch = (the_tree.next_epoch_index) ? MASK : 0;

    the_tree.next_epoch_index += 1;
    the_tree.next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(the_tree.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0;
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("house keeping: waiting grace-full period (target index is %ld)\n", grace_period_threads);
    wait_event_interruptible(wait_queue, the_tree.standing[index] >= grace_period_threads);
    the_tree.standing[index] = 0;

    spin_unlock(&the_tree.write_lock);

    goto redo;

    return 0;
}

void rcu_tree_init(struct blk_rcu_tree *t)
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

struct blk_element *lookup(struct blk_element *root, int index)
{

    if (root == NULL)
    {
        return NULL;
    }

    if (index > root->index)
    {
        // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
        return lookup(root->right, index);
    }
    else if (index < root->index)
    {
        // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
        return lookup(root->left, index);
    }
    else
    {
        AUDIT printk("%s: lookup operation completed for block with index %d", MOD_NAME, index);
        return root;
    }
}

void insert(struct blk_element **root, struct blk_element *newNode)
{
    int index = newNode->index;
    // AUDIT printk("%s: insert operation started for block at index %d", MOD_NAME, index);
    if (*root == NULL)
    {
        *root = newNode;
        AUDIT printk("%s: insert operation completed for block with index %d", MOD_NAME, index);
        return;
    }

    if (index > (*root)->index)
    {
        // AUDIT printk("%s: The block with index %d follows the right subtree", MOD_NAME, index);
        insert(&((*root)->right), newNode);
    }
    else if (index < (*root)->index)
    {
        // AUDIT printk("%s: The block with index %d follows the left subtree", MOD_NAME, index);
        insert(&((*root)->left), newNode);
    }
}

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

// Funzione per eliminare un nodo con un dato index dall'albero binario
struct blk_element *delete(struct blk_element *root, int index)
{
    struct blk_element *temp = NULL;
    struct blk_element *min_node = NULL;
    if (root == NULL)
    {
        return root;
    }
    if (index < root->index)
    {
        root->left = delete (root->left, index);
        asm volatile("mfence"); // make it visible to readers
    }
    else if (index > root->index)
    {
        root->right = delete (root->right, index);
        asm volatile("mfence"); // make it visible to readers
    }
    else
    {
        // Node to be deleted has 0 or 1 child
        if (root->left == NULL)
        {
            temp = root->right;
            kfree(root);
            return temp;
        }
        else if (root->right == NULL)
        {
            temp = root->left;
            kfree(root);
            return temp;
        }
        // Node to be deleted has 2 children
        min_node = find_min(root->right);
        root->index = min_node->index;
        root->right = delete (root->right, min_node->index);
        asm volatile("mfence"); // make it visible to readers
    }
    return root;
}

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
