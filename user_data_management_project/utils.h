#ifndef _UTILS_H
#define _UTILS_H

extern void init(struct rcu_data *);

extern void insert(struct message **, struct message **, struct message *);
extern void insert_sorted(struct message **, struct message **, struct message *);
extern void free_list(struct message *);
extern void free_array(struct blk_element **);
extern void delete(struct message **, struct message **, struct message *);

extern struct message *lookup_by_pos(struct message *, int);
extern struct message *lookup_by_index(struct message *, int);
extern void quickSort(struct message *, struct message *);

extern void print_list(struct message *);
extern void print_reverse(struct message *);

#endif