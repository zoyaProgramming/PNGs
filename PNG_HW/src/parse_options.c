#include <parse_options.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h> // used for strtol
#include <png_reader.h>
#include "global.h"
#include "hashset.h"

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

//recognizes an option code (-e, -f, -m, etc... name always starts w/ a '-')
//takes a ptr to argv array, so that we can consume an input token using this function instead of
//handling it in the caller
//for example, argv is ["-f", "filename.png", "-h"]
//argv_ptr may point to "-f", and through this function it will 
//set argv_ptr to point to "filename.png"
//and argc will change from 3 to 2
//will not change argv or argc if argv is empty, or if the malloc fails, or if the current token is not a valid option
void free_opt(Option * result);
inline Option * getopt(int * argc, char *** argv_ptr, char* validstring ){
    if(argc == 0){
        return NULL;
    }
    Option * result = malloc(sizeof(Option));
    char ** argv = *argv_ptr;
    char * v = NULL;
    if(argv[1][0] == '-' &&  (v = strchr(validstring, argv[1][1])) > 0 && !argv[1][2] ){
        result->type = argv[1][1];
        *argv_ptr = *argv_ptr + 1;  // the word counter increases
        *argc = *argc + 1;
    } else {
        PRINT_ERROR_UNKNOWN_OPTION(argv[1]);
        free_opt(result);
        return NULL;
    }

    // if the option takes an argument, capture the optarg:
    if(*argc > 0 && *(v+1) == ':' && (*argv_ptr)[1][0] != '-'){ // capture the optarg
        result->optarg_len = strlen((*argv_ptr)[1]);
        result->optarg = (*argv_ptr)[1]; 
        *argv_ptr = *argv_ptr + 1;
        *argc = *argc -1;
    }
    return result;
}

//a wrapper for getopt
//parse options that take arguments: 
//useful for -e, -m, -w, -h, -o
//no error if argument not found, leaves room for manually handling errors (because we want to print them)

void free_opt(Option * opt){
    if(opt->num_childs && opt->children){
        for(int i = 0; i< opt->num_childs; i++){
            free_opt(&opt->children[i]);
        }
        opt->num_childs = 0;
        free(opt->children);
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

// parses options, returning a code 
// 0 for 1, else returns the char code where it threw the error 
// idea: separate away validating arguments, 
int parse_next_opt( int* argc, char *** argv, Option ** out){
  //  Option * opt = array[*arr_len];
    // if one of the main valid options isn't successfully parsed
    Option * result;
    if(!(result = getopt(argc, argv, "ipde:m:"))){ 
        return 1;
    } else {
        switch(result->type){
            case 'h':
            case 'i':
            case 'p':
            case 'd':
                *out = result;
                return 0;
            case 'e': // needs -o arg
                if(!result->optarg){
                    free(result);
                    return 'e';
                } 
                Option * outfile = getopt(argc, argv, "o:");
                if(!outfile->optarg){
                    free(result);
                    free(outfile);
                    return 'e';
                }
                result->children = outfile;
                result->num_childs = 1;
                *out = result;
                return 0;
            case 'm':
                if(!result->optarg){
                    free(result);
                    return 'e';
                } 
                outfile = getopt(argc, argv, "o:");
                if(!outfile || !outfile->optarg){
                    free(result);
                    free(outfile);
                    return 'e';
                }
                result->children = outfile;
                result->num_childs = 1;
                
                
                // if width , use strtol to validate width argument is an integer
                Option * width = getopt(argc, argv, "w:");
                if(width){
                    char * endptr;
                    errno = 0;
                    if( width && !strtol(width->optarg, &endptr, 10)){
                        free_opt(result);
                        free(width);
                        return 'w';
                    }
                    
                    // copy the data from the width to the children array
                    // refactor later :(
                    Option * new_ptr = realloc(result->children, sizeof(Option) * (result->num_childs + 1));
                    if(!new_ptr)
                    {
                        free_opt(result);
                        free(width);
                        return 'w';
                    }
                    memcpy(new_ptr + result->num_childs, width, sizeof(Option));
                    result->children = new_ptr;
                    free(width);
                }

                // validate height argument, use strtol to validate height argument is an integer
                Option * height = getopt(argc, argv, "g:");
                if(height){
                    char * endptr;
                    errno = 0;
                    if(height && !strtol(height->optarg, &endptr, 10)){
                        free_opt(result);
                        free(height);
                        return 'g';
                    }
                    
                    // copy the data from the width to the children array
                    // refactor later :(
                    Option * new_ptr = realloc(result->children, sizeof(Option) * (result->num_childs + 1));
                    if(!new_ptr)
                    {
                        free_opt(result);
                        free(height);
                        return 'g';
                    }
                    memcpy(new_ptr + result->num_childs, height, sizeof(Option));
                    result->children = new_ptr;
                    free(height);
                }
                return 0;
            default: 
                return 1;
        }
    }
    return 0;

}
int parse_opts(int argc, char **argv, Option ** out, int * out_len){
    printf("disfj\n");
    fflush(stdout);
    Option * arr = malloc(sizeof(Option) * MAX_SIZE);
   int error_code;
    Option * latest_option;
    Hashset options;
    options.type  = OPTION;
    int n = 0;
    // scan all options


    while(!(error_code = parse_next_opt( &argc, &argv, &latest_option)) ) {
        printf("disfj\n");
        fflush(stdout);
        if(!set_contains(&options, &(latest_option->type))){
            set_put(&options, &(latest_option->type), n);
            arr[n++] = *latest_option;
        }
    }; 

    printf("sfj %d\n",n);
    fflush(stdout);
    free_opt_array(n, &arr);
    free_set(&options);
    
    return 0;
}