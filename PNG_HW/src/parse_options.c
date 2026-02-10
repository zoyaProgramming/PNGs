#include <parse_options.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png_reader.h>



/*check if an option is valid based on the specifications*/


/*find the position of the start of the next option*/


/*SUMMARY = 's', 
    FILE = 'f', 
    IHDR_FIELDS = 'i', 
    PALETTE_SUMMARY = 'p', 
    ENCODE_MSG = 'e', 
    OUTPUT_FILE = 'o', 
    DECODE_MSG = 'd', 
    OVERLAY = 'm', 
    WIDTH= 'w', 
    HEIGHT= 'h'*/

/*find options*/
inline int getopt(int * argc, char ***  argptr, Option ** out, char* validstring ){
    if(argc == 0){
        return -1;
    }
    *out = malloc(sizeof(Option));
    char ** argv = *argptr;
    char * v = NULL;
    if(argv[1][0] == '-' &&  (v = strchr(validstring, argv[1][1])) > 0 && !argv[1][2] ){
        (*out)->type = argv[1][1];
        *argptr++;  // the word counter increases
        *argc--;
    } else {
        free_opt(*out);
        return -1;
    }
    // check if optarg needs to be parsed
    // parse if necessary
    if(argc > 0 && (*(v+1) == ':') && (*argptr)[1][0] != '-'){ // capture the optarg
        (*out)->optarg_len = strlen((*argptr)[1]);
        (*out)->optarg = (*argptr)[1];
        *argc--;
    } else if ((*(v+1) == ':')){
        // optarg needed, but not found
        free_opt(*out);
        return -1;
    }
    return 0;
}

void free_opt(Option * opt){
    if(opt->num_childs && opt->children){
        for(int i = 0; i< opt->num_childs; i++){
            free_opt(opt->num_childs);
        }
        opt->num_childs = NULL;
        free(opt);
    } else {
        free(opt);
    }
} 
void free_opt_array( int n, Option **arr){
    for(int i = 0; i< n; i++){
        free_opt(arr[i]);
        arr[i] = NULL;
    };
    free(arr);
}

int parse_next_opt(int *arr_len, Option** array, int* argc, char *** argv ){
    Option * opt = array[*arr_len];
    if(getopt(argc, argv, opt, "ipde:m:")){
        return -1;
    } else {
        switch(opt->type){
            case 'h':
            case 'i':
            case 'p':
            case 'd':
                return 0;
            case 'e':
                
            case 'm':
            default: 
                return -1;
        }
    }
    *arr_len++;

}
int parse_opts(int argc, char **argv){
    char ** argptr = argv;

    Option * arr[MAX_SIZE];
    int arr_len = 0;

    while(!parse_next_opt(&arr_len, &arr, &argc, &argv)); // scan all options
    
    
}