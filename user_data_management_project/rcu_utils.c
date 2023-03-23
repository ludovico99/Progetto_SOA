#include <linux/math.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "linux/kthread.h"
#include "userdatamgmt_driver.h"

static int house_keeper(void *the_tree)
{

    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;

    struct blk_rcu_tree *t = (struct blk_rcu_tree *)the_tree;

redo:
    msleep(PERIOD);

    spin_lock(&t->write_lock);

    updated_epoch = (t->next_epoch_index) ? MASK : 0;

    t->next_epoch_index += 1;
    t->next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(t->epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0;
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("house keeping: waiting grace-full period (target value is %ld)\n", grace_period_threads);
    while (t->standing[index] < grace_period_threads)
        ;
    t->standing[index] = 0;

    spin_unlock(&t->write_lock);

    goto redo;

    return 0;
}

void rcu_tree_init(struct blk_rcu_tree *t)
{

    int i;
    // char name[128] = "the_daemon";
    // struct task_struct *the_daemon;

    t->epoch = 0x0;
    t->next_epoch_index = 0x1;
    for (i = 0; i < EPOCHS; i++)
    {
        t->standing[i] = 0x0;
    }
    t->head = NULL;
    spin_lock_init(&t->write_lock);
    // the_daemon = kthread_create(house_keeper, NULL, name);

    // if(the_daemon) {
    // 	wake_up_process(the_daemon);
    //     printk("%s: RCU-tree house-keeper activated\n",MOD_NAME);
    // }

    // else{
    //     AUDIT printk("%s: Kernel daemon initialization has failed \n",MOD_NAME);

    //  }
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

// void stampa_albero(struct blk_element *root)
// {
//     if (root != NULL)
//     {
//         stampa_albero(root->left);
//         AUDIT printk("%d", root->index);
//         stampa_albero(root->right);
//     }
// }


void free_tree(struct blk_element *root)
{      
     if (root == NULL)
    {   
        return;
    }
   
    free_tree(root->left);
    free_tree(root->right);

    kfree (root);
}

