#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_steg.h"
#include "png_overlay.h"


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
    // second phase
    // if have time, consider making this a single 
    struct Flags {
        char summary;
        char palette;
        char ihdr;
        char encode_msg;
        char decode_msg;
        struct overlay{
            int width;
            int height;
        };
        char * outfile;
        char * file2;
    } flags;
    
    while(opt = getopt(argc, argv, "spie:o:dm:")){
        switch(opt){
            case 's': // summary
                
                
                break;
            case 'p': // palette summary
                FILE *png_fp = png_open(filename);
                if(!png_fp){
                    return EXIT_FAILURE;
                }
                png_extract_plte(png_fp, );
                break;
            case 'i': // IHDR
                png_extract_ihdr();
                break;
            case 'm': // REQUIRES OPTARG file2; overlay file2 over input
                char * small_path = optarg;
                if(getopt(argc, argv, "o:") != 'o'){
                    PRINT_ERROR_ENCODE_REQUIRES();
                    return EXIT_FAILURE;
                }
                char * output_path = optarg;
                // read width and height
                // might have to add error fixing in the case of duplicate arguments
                uint32_t x_offset = 0, y_offset = 0;
                while(opt2 = getopt(argc, argv, ":w:h:") != -1 && opt2 != '?'){
                    if(opt2 == 'w'){
                        x_offset = (uint32_t)strtoul(optarg, NULL, 10);
                    } else if (opt2 == 'h'){
                        y_offset = (uint32_t)strtoul(optarg, NULL, 10);
                    } else if (optopt == 'w'){
                        PRINT_ERROR_WIDTH_REQUIRES();
                        return EXIT_FAILURE;
                    } else if (optopt == 'h'){
                        PRINT_ERROR_HEIGHT_REQUIRES();
                        return EXIT_FAILURE;
                    }
                };
                if(png_overlay_paste(filename, small_path, output_path, x_offset, y_offset) == EXIT_FAILURE){
                    return EXIT_FAILURE;
                };

            case 'e': // REQUIRES OPTARG message; encode message and write to output file
                char * message = optarg;
                if(getopt(argc, argv, "o:") != 'o'){
                    PRINT_ERROR_ENCODE_REQUIRES();
                    return EXIT_FAILURE;
                }
                if(png_encode_lsb(filename, optarg, message) != EXIT_SUCCESS){
                    return EXIT_FAILURE;
                };
                break;
            case 'd': // decode and print hidden msg
                break;
            case '?': // unrecognized option or missing optarg
                switch(optopt){
                    case 'e':
                        PRINT_ERROR_ENCODE_REQUIRES();
                    case 'o':
                        if(flags.encode_msg){
                            PRINT_ERROR_ENCODE_REQUIRES();
                        } else {
                            PRINT_ERROR_OVERLAY_REQUIRES();
                        }
                        break;
                    case 'm':
                        PRINT_ERROR_OVERLAY_REQUIRES();
                    default: 
                        PRINT_ERROR_UNKNOWN_OPTION(optopt);
                        break;
                } 
                return EXIT_FAILURE;
            default:
                return EXIT_FAILURE;

                
            
            
        }
    }

    return 0;
}
