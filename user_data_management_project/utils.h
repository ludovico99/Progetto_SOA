#ifndef _UTILS_H
#define _UTILS_H

extern void init(struct rcu_data *);

extern struct blk_element *tree_lookup(struct blk_element *, int);
extern void tree_insert(struct blk_element **, struct blk_element *);
// extern bool tree_delete_it(struct blk_element**, int);
extern struct blk_element* tree_delete (struct blk_element*, int);
extern struct blk_element* inorderTraversal(struct blk_element*);
extern void stampa_albero(struct blk_element *root);
extern void free_tree(struct blk_element *);
extern void get_balanced_indices(int *, int , int, int*);

extern void insert(struct message **, struct message **,struct message *);
extern void insert_sorted(struct message **, struct message **,struct message *, int);
extern void free_list(struct message*);
extern void delete(struct message**,struct message **, struct message*);
extern void stampa_lista(struct message *);

#endif