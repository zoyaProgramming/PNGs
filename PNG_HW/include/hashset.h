#ifndef HASHSET_H
#define HASHSET_H
#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include "png_chunks.h"
#include "parse_options.h"

#define MAX_LEN 256

enum Type {PNG_COLOR_T, OPTION};
typedef struct Node{
    struct Node * next;
    struct Node * prev;
    void * contents;
    int index;
     enum Type type;
} Node;

int isequal(void * a, void * b, enum Type type);

typedef struct Bucket {
    int n;
    Node * head;
    Node * tail;
} Bucket; 
typedef struct Hashset {
    struct Bucket buckets[MAX_LEN];
    int length;
    enum Type type;
} Hashset;

int hash_function(void * data, enum Type type);

int set_getindex(Hashset * set, void * data);

Node * set_fetch(Hashset * set, png_color_t * data);

int set_contains(Hashset * set, void * data);

int set_put(Hashset * set, void * data, int index);
int remove_node(Hashset * hash, void * data);
/* frees the tail and,  removes it from the bucket*/
int  remove_last_node(Bucket * b);
void free_set(Hashset * set);
#endif
