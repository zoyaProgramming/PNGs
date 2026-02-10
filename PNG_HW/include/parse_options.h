#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H
#include <stdlib.h>

#define MAX_SIZE 100
typedef struct {
    int arr[MAX_SIZE];
    int top;
} Stack;

enum OPT_TYPE {
    SUMMARY = 's', 
    FILE = 'f', 
    IHDR_FIELDS = 'i', 
    PALETTE_SUMMARY = 'p', 
    ENCODE_MSG = 'e', 
    OUTPUT_FILE = 'o', 
    DECODE_MSG = 'd', 
    OVERLAY = 'm', 
    WIDTH= 'w', 
    HEIGHT= 'h'
};


void initialize(Stack* stack) {
    stack->top = -1;
};
int push(Stack* stack, int val){
    if(stack->top == MAX_SIZE -1){
        return -1;
    }
    else {
        stack->arr[stack->top] = val;
        stack->top++;
        return 0;
    }
}
int * pop(Stack* stack){
    if(stack->top == -1){
        return NULL;
    } else {
        return &stack->arr[stack->top--];
    }
}
int* peek(Stack* stack){
    if(stack->top == -1){
        return NULL;
    } else {
        return &stack->arr[stack->top];
    }
}


typedef struct Option{
    char type;
    int num_childs;
    struct Option * children;

    int optarg_len;
    char * optarg;
} Option;

/*check if an option is valid based on the specifications*/
int validate_opt(Option opt);

/*find the position of the start of the next option*/
int get_next_opt(int*out_len, Option ** out, int strlen, char * str);

/*find and run all options*/
int parse_opts(int argc, char **argv);

#endif