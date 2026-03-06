#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "png_chunks.h"
#include "parse_options.h"
#include "hashset.h"

#define MAX_LEN 256

void print_color(png_color_t  * color){
    printf("r: %d, g: %d, b:%d\n", color->r, color->g, color->b);
}
 void print_option(Option * option) {
    printf("optarg: %s, num_childs: %d, type: %c\n", option->optarg, option->num_childs, option->type);
}
int isequal(void * a, void * b, enum Type type){
    if(type == PNG_COLOR_T){
        png_color_t* colora =  (png_color_t *)a;
        png_color_t* colorb = (png_color_t *)b;
        return ((colora->r == colorb->r) && (colora->g == colorb->g) && (colora->b == colorb->b));
    } else {
        Option * opta = (Option*)a;
        Option * optb = (Option*)b;
        return opta->type==optb->type;
    }
}
void print_node(Node * node){
    if(node->type == PNG_COLOR_T){
        print_color((png_color_t*) node->contents);
    } else {
        print_option((Option * )node->contents);
    }
}

int hash_function(void * data, enum Type type){
    if(type == OPTION){
        return ((Option *) data)->type % 256;
    } else {
        png_color_t  d = *(( png_color_t *) data);
        return (d.b + d.g + d.r)%256;
    }
}

int set_getindex(Hashset * set, void * data){
    int data_hashed = hash_function(data, set->type);
    Bucket b = set->buckets[data_hashed];
    Node * curr = b.head;
    while(curr){
        if(isequal(&(curr->contents), data, set->type)){
            return curr->index;
        }
        curr = curr->next;
    }
    return 0;
}

Node * set_fetch(Hashset * set, png_color_t * data){
    int data_hashed = hash_function(data, set->type);
    Bucket b = set->buckets[data_hashed];
    Node * curr = b.head;
    while(curr){
        if(isequal((curr->contents), data, set->type)){
            fflush(stdout);
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int set_contains(Hashset * set, void * data){
    int data_hashed = hash_function(data, set->type);
    Bucket b = set->buckets[data_hashed];
    Node * curr = b.head;
    while(curr){
        if(isequal((curr->contents), data, set->type)){
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

int set_put(Hashset * set, void * data, int index){
    if(!set_contains(set, data)){
        int data_hashed = hash_function(data, set->type);
        int n = set->buckets[data_hashed].n;
        
        Node * temp = (Node*)calloc(1, sizeof(Node));
        if(!temp){
            return -1;
        }
        temp->contents = data;
        temp->index = index;
        if(n==0){
            set->buckets[data_hashed].head = temp;
        } else {
            set->buckets[data_hashed].tail->next = temp;
            temp->prev = set->buckets[data_hashed].tail;
        }
        set->buckets[data_hashed].n += 1;
        set->buckets[data_hashed].tail = temp;
        set->length ++;
        return 0;
    }
    return -1;
}

int remove_node(Hashset * hash, void * data){
    Node * node  = set_fetch(hash, data);
    Bucket * curr_bucket = hash->buckets + hash_function(data, hash->type);
    if(node && curr_bucket->tail != node && curr_bucket->head != node ){
        if(node->prev){
            node->prev->next = node->next;
        }
        if(node->next){
            node->next->prev = node->prev;
        }
        if(curr_bucket->tail == node){
            curr_bucket->tail = node->prev;
        }
        if(curr_bucket->head == node){
            curr_bucket->head = node->next;
        }
        hash->length--;
        free(node);
        return 0;
    }
    return 1;
}

/* frees the tail and,  removes it from the bucket*/
int  remove_last_node(Bucket * b){ 
    if(!b->tail){
        return 1;
    }
    Node * newTail = b->tail->prev;
    if(newTail){
        newTail->next = NULL; // remove reference to current tail
    } else {
        b->head = NULL; // removing the last and only node  
    }
    free(b->tail);
    b->tail = newTail;
    return 0;
}

void free_set(Hashset * set){// clear the hashset
    for(int i = 0; i < 256; i++){
        while(!remove_last_node(set->buckets + i)){
            set->length--;
        }
    }
}
