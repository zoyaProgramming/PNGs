#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_steg.h"
#include "png_overlay.h"
#include "parse_options.h"

#include <getopt.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if(!argc){
        return 1; // is this the correct output?
    }
    int opt;
    char * filename;
    while((opt = getopt(argc, argv, "hf:")) != -1){
        switch(opt){
            case 'h':
                PRINT_USAGE(argv[0]); // print options string out, with program name
                return EXIT_SUCCESS;
            case 'f':
                filename = optarg;
                break;
            case '?':
                if(optopt == 'f'){
                    PRINT_ERROR_F_REQUIRES_FILENAME();
                    return EXIT_FAILURE;
                } 
                break;
        }
    }

    if(!filename){
        return EXIT_FAILURE;
    }

    Option * opts;
    int num_opts = 0;
    int error_code = 0;
    if((error_code = parse_opts(argc, argv, &opts, &num_opts))){
        switch(error_code){
            case 'm':
                PRINT_ERROR_OVERLAY_REQUIRES();
            case 'e':
                PRINT_ERROR_ENCODE_REQUIRES();
            case 'w':
                PRINT_ERROR_WIDTH_REQUIRES();
            case 'b':
                PRINT_ERROR_HEIGHT_REQUIRES();
        }
    }
    else {
       
        for(int i = 0; i< num_opts; i++){
            Option curr = opts[i];
            switch(curr.type){
                case 'm':
                    char * outfile = curr.children[0].optarg;
                    uint32_t x_offset = 0, y_offset = 0;
                    for(int i = 2 ; i< curr.num_childs; i++){
                        if(curr.children[i].type == 'w'){
                            x_offset = (uint32_t) strtol(curr.children[i].optarg, NULL, 10);
                        } else {
                            y_offset = (uint32_t) strtol(curr.children[i].optarg, NULL, 10);
                        }
                    }
                    int return_code = png_overlay_paste(filename, curr.optarg, outfile, x_offset, y_offset);
                    if(!return_code){
                        PRINT_OVERLAY_SUCCESS(outfile);
                    } else {
                        PRINT_ERROR_OVERLAY_FAILED();
                    }
                    break;
                case 'e':
                    outfile = curr.children[0].optarg;
                    return_code = png_encode_lsb(filename, outfile, curr.optarg);
                    if(return_code){
                        PRINT_ERROR_ENCODE_FAILED();
                        return -1;
                    } else {
                        PRINT_ENCODE_SUCCESS(outfile);
                    }
                    break;
                case 's':
                    png_chunk_t * out_summary = NULL;
                    if(png_summary(filename, &out_summary) == -1){
                        PRINT_ERROR_READ_CHUNKS();
                    } else {
                        PRINT_CHUNK_SUMMARY_HEADER(filename);
                        int i = 0;
                        while(strncmp(out_summary[i].type, "IEND", 4)){
                            PRINT_CHUNK_INFO(i, out_summary[i]);
                            i++;
                        }
                        return 0;
                    }
                    
                case 'p':
                    FILE * fp = png_open(filename);
                    if(!fp){
                        PRINT_ERROR_OPEN_FILE(filename);
                        return -1;
                    }
                    png_color_t * out_colors;
                    size_t out_count;
                    if(png_extract_plte(fp, &out_colors, &out_count)){
                        PRINT_ERROR_PLTE_NOT_FOUND();
                        fclose(fp);
                        return -1;
                    }
                    
                case 'i':
                    fp = png_open(filename);
                    if(!fp){
                        PRINT_ERROR_OPEN_FILE(filename);
                        return -1;
                    }
                    png_ihdr_t ihdr;
                    if(png_extract_ihdr(fp, &ihdr)){
                        fclose(fp);
                        PRINT_ERROR_READ_IHDR();
                        return -1;
                    }
                    
            }
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
