#ifndef ALLOC_H
#define ALLOC_H

struct alloc_state;
struct tree;
struct commit;
struct tag;
struct repository;

void *mem_pool_alloc_blob_node(struct repository *r);
void *mem_pool_alloc_tree_node(struct repository *r);
void init_commit_node(struct commit *c);
void *mem_pool_alloc_commit_node(struct repository *r);
void *mem_pool_alloc_tag_node(struct repository *r);
void *mem_pool_alloc_object_node(struct repository *r);
void alloc_report(struct repository *r);

struct alloc_state *allocate_alloc_state(void);
void clear_alloc_state(struct alloc_state *s);

#endif
