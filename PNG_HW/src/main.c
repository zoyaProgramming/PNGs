#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_steg.h"
#include "png_overlay.h"

#include "hashset.h"
#include <getopt.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if(!argc){
        return 1; // is this the correct output?
    }
    int opt;
    char * filename;
    opterr = 0;
    // getopt, don't permute anything
   
    while((opt = getopt(argc, argv, "+:hf:")) != -1){
        switch(opt){
            case 'h':
                PRINT_USAGE(argv[0]); // print options string out, with program name
                return EXIT_SUCCESS;
            case 'f':
                filename = optarg;
                break;
            case ':':
                if(optopt == 'f'){
                    PRINT_ERROR_F_REQUIRES_FILENAME();
                    return EXIT_FAILURE;
                }
                break;
            default:
                break;
        }
    }

    if(!filename){
        PRINT_ERROR_MISSING_F_FLAG();
        return EXIT_FAILURE;
    }
    
    fflush(stdout);

    typedef struct Option{
        char type;
        int num_related_options; // for -e and -m
        char * optarg;
    } Option;

    struct Option options[20];
    memset(options, 0, 20* sizeof(struct Option));
    int top = -1; // last argument in the array
    // 
    optind = 0;// reset getopt
    opterr=0;
    // avoid processing duplicates
    while((opt = getopt(argc, argv, ":hfsipde:m:")) != -1){
        switch(opt){ 
            case 'h':
            case 'f': // just skip these
                break;
            case 's': // get "Summary" data
            case 'i': // get "IHDR" data
            case 'p': // get "PLTE" data
            case 'd': // decode and print hidden message
                options[++top] = ( Option){opt, 0, NULL}; break;
            case 'e': // encode, needs -o arg
                options[++top] = (Option){opt, .num_related_options = 1, optarg};
                int outfile = getopt(argc, argv, "+o:");
                if(outfile != 'o'){
                    PRINT_ERROR_ENCODE_REQUIRES();
                    return -1;
                }
                options[++top] = (Option){opt, .num_related_options = 0, optarg};
                break;
            case 'm': // overlay
                options[++top] = (Option){opt, .num_related_options = 1, optarg};
                int num_related_options = 1;
                opt = getopt(argc, argv, "o:");
                if(opt != 'o'){
                    PRINT_ERROR_ENCODE_REQUIRES();
                    return -1;
                }
                options[top + 1] = (Option){opt, .num_related_options = 0, optarg};
                // get width and height, and move optind
                options[top].num_related_options = num_related_options;
                top += num_related_options;

                while((opt = getopt(argc, argv, ":w:g:")) != -1){ // 
                    // check for duplicates: only use the first value of a flag
                    long int value = 0; 
                    char * endptr = NULL;
                    if(opt == '?'){
                        //  skip unknown options, reset optind
                        PRINT_ERROR_UNKNOWN_OPTION(optarg);
                        optind--;
                        break;
                    } else if (opt == ':'){
                        if(optopt == 'w') PRINT_ERROR_WIDTH_REQUIRES();
                        else PRINT_ERROR_HEIGHT_REQUIRES();
                        return -1;
                    } else if (!(num_related_options == 1 || (num_related_options < 3 && options[top + num_related_options].type != opt) || 
                    (value = strtol(optarg, &endptr, 10)) >= 0 ) ){
                        // duplicate options detected
                        PRINT_ERROR_UNKNOWN_OPTION(optarg);
                        return -1;
                    } else if (endptr == NULL){
                        if(opt == 'w') PRINT_ERROR_WIDTH_REQUIRES();
                        else PRINT_ERROR_HEIGHT_REQUIRES();
                        return -1;
                    } else {
                        options[top + (++num_related_options)] = (Option){opt, .num_related_options = 0, optarg};
                    }
                }
                break;
            case ':':
                if(optopt == 'e')
                    PRINT_ERROR_ENCODE_REQUIRES();
                else PRINT_ERROR_OVERLAY_REQUIRES();
                return -1;
            case '?':
                PRINT_ERROR_UNKNOWN_OPTION(optarg);
                break;
        }
    }
    // actually call the functions
    for(int i = 0; i <= top; i++){
        Option curr_option = options[i];
        switch(curr_option.type){
            case 'i': // ihdr
                fflush(stdout);
                FILE * fp;
                if((fp = png_open(filename)) == NULL) {
                    PRINT_ERROR_OPEN_FILE(filename);
                    return -1;
                }
                png_ihdr_t ihdr;
                if(png_extract_ihdr(fp, &ihdr) == -1) {
                    PRINT_ERROR_PARSE_IHDR();
                    return -1;
                }
                // success
                PRINT_IHDR(filename, ihdr);
                break;
            case 's':
                png_chunk_t * out_summary = NULL;
                if(png_summary(filename , &out_summary) == -1){
                    return -1;
                }
                PRINT_CHUNK_SUMMARY_HEADER(filename);
                for(int i = 0; strcmp(out_summary[i].type, "IEND") != 0; i++){
                    PRINT_CHUNK_INFO(i, out_summary[i]);
                }
                break;
            case 'p':
                if((fp = png_open(filename)) == NULL) {
                    PRINT_ERROR_OPEN_FILE(filename);
                    return -1;
                }
                png_color_t * out_colors = NULL;
                size_t out_count = 0;
                if(png_extract_plte(fp, &out_colors, &out_count) == -1) {
                    PRINT_ERROR_PLTE_NOT_FOUND();
                    return -1;
                }
                // success
                PRINT_PALETTE_HEADER(filename);
                PRINT_PALETTE_COUNT(out_count);
                for(int i = 0 ; i < out_count; i++){
                    PRINT_PALETTE_COLOR(i, out_colors[i].r, out_colors[i].g, out_colors[i].b);
                }
                free(out_colors);
                break;
            case 'd': // decode hidden msg
                if(!(fp = png_open(filename))){
                    PRINT_ERROR_OPEN_FILE(filename);
                    PRINT_ERROR_EXTRACT_FAILED();
                    return -1;
                }
                
                if ((png_extract_ihdr(fp, &ihdr)) == -1){
                    fclose(fp);
                    PRINT_ERROR_PARSE_IHDR();
                    return -1;
                }
                fclose(fp);
                size_t len = (ihdr.width  * ihdr.height)/8 + 1; // maxsize:
                char * buffer= (char * )malloc(sizeof(char) * len);
                memset(buffer, 0, len);

                if((png_extract_lsb(filename, buffer, len)) == -1){// 256 is the max. colors. in 8 bit types,
                    PRINT_ERROR_EXTRACT_FAILED();
                    return -1;
                } 
                PRINT_HIDDEN_MESSAGE(buffer);
                free(buffer);
                break;
            case 'e': // encode messgae
                char * message = optarg;
                char * output_path = options[++i].optarg; // next option in the list is the input path, always valid
                if ((png_encode_lsb(filename, output_path, message)) == -1){
                    PRINT_ERROR_ENCODE_FAILED();
                    return -1;
                }
                PRINT_ENCODE_SUCCESS(output_path);
                break;
            case 'm': // how is this overlay 
                char * path_smaller = optarg;
                output_path = options[++i].optarg; // next option in the list is the input path, always valid
                //char * endptr = NULL;
                uint32_t x_offset, y_offset = 0;
                // find x_offset and y_offset if included
                for(int j = 2; j < curr_option.num_related_options; j++){
                 //   char * endptr = NULL;
                    if(options[i + j].type =='w') x_offset = strtol(options[i + j].optarg, NULL, 10);
                    else y_offset = strtol(options[i+j].optarg, NULL, 10);
                }
                i+= curr_option.num_related_options;

                if(png_overlay_paste(filename, path_smaller, output_path, x_offset, y_offset) == -1){
                    PRINT_ERROR_OVERLAY_FAILED();
                    return -1;
                }

                PRINT_OVERLAY_SUCCESS(output_path);
                break;
        }
    }

    // second phase
    // if have time, consider making this a single 
    // while((opt = getopt(argc, argv, "spie:dm:"))){
    //     switch(opt){
    //         case 's': // summary
    //             png_chunk_t* out_summary = NULL;
    //             int code = png_summary(filename, &out_summary);
                
    //             break;
    //         case 'p': // palette summary
    //             FILE *png_fp = png_open(filename);
    //             if(!png_fp){
    //                 return EXIT_FAILURE;
    //             }
    //             int code = pal
    //           //  png_extract_plte(png_fp, );
    //             break;
    //         case 'i': // IHDR
    //           //  png_extract_ihdr();
    //             break;
    //         case 'm': // REQUIRES OPTARG file2; overlay file2 over input
    //             char * small_path = optarg;
    //             if(getopt(argc, argv, "o:") != 'o'){
    //                 PRINT_ERROR_ENCODE_REQUIRES();
    //                 return EXIT_FAILURE;
    //             }
    //             char * output_path = optarg;



    //             // read width and height
    //             // might have to add error fixing in the case of duplicate arguments
    //             uint32_t x_offset = 0, y_offset = 0;
    //             int opt2;
    //             while((opt2 = getopt(argc, argv, ":w:h:")) != -1 && opt2 != '?'){
    //                 if(opt2 == 'w'){
    //                     x_offset = (uint32_t)strtoul(optarg, NULL, 10);
    //                 } else if (opt2 == 'h'){
    //                     y_offset = (uint32_t)strtoul(optarg, NULL, 10);
    //                 } else if (optopt == 'w'){
    //                     PRINT_ERROR_WIDTH_REQUIRES();
    //                     return EXIT_FAILURE;
    //                 } else if (optopt == 'h'){
    //                     PRINT_ERROR_HEIGHT_REQUIRES();
    //                     return EXIT_FAILURE;
    //                 }
    //             };
    //             if(png_overlay_paste(filename, small_path, output_path, x_offset, y_offset) == EXIT_FAILURE){
    //                 return EXIT_FAILURE;
    //             };

    //         case 'e': // REQUIRES OPTARG message; encode message and write to output file
    //             char * message = optarg;
    //             if(getopt(argc, argv, "o:") != 'o'){
    //                 PRINT_ERROR_ENCODE_REQUIRES();
    //                 return EXIT_FAILURE;
    //             }
    //             if(png_encode_lsb(filename, optarg, message) != EXIT_SUCCESS){
    //                 return EXIT_FAILURE;
    //             };
    //             break;
    //         case 'd': // decode and print hidden msg
    //             break;
    //         case '?': // unrecognized option or missing optarg
    //             switch(optopt){
    //                 case 'e':
    //                     PRINT_ERROR_ENCODE_REQUIRES();
    //                 case 'm':
    //                     PRINT_ERROR_OVERLAY_REQUIRES();
    //                 default: 
    //                     char opt_string[2] = {optopt, '\0'};
    //                     PRINT_ERROR_UNKNOWN_OPTION( opt_string);
    //                     break;
    //             } 
    //             return EXIT_FAILURE;
    //         default:
    //             return EXIT_FAILURE;

                
            
            
    //     }
    // }

    return 0;
}
