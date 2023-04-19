#ifndef _UTILS_H
#define _UTILS_H

extern void init(struct rcu_data *);

extern void insert(struct message **, struct message **,struct message *);
extern void insert_sorted(struct message **, struct message **,struct message *, int);
extern void free_list(struct message*);
extern void free_array(struct blk_element **);
extern void delete(struct message**,struct message **, struct message*);
extern void stampa_lista(struct message *);
extern struct message *search(struct message *, int );
extern struct message *lookup(struct message *, int);

#endif