#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H
#include <stdlib.h>

#define MAX_SIZE 100
typedef struct Option{
    char type;
    int num_childs;
    struct Option * children;

    int optarg_len;
    char * optarg;
} Option;

/*find and run all options*/
int parse_opts(int argc, char **argv, Option ** out, int * out_len);

#endif