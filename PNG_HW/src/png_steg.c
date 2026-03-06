#include "png_steg.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_crc.h"
#include "util.h"
#include "write_helpers.h"
#include "hashset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
inline int get_bpp(int color_type){
    switch(color_type){
        case 1:
            return 1;
        case 2:
        case 3: 
            return 3;
            break;
        case 4:
            return 2;
            break;
        case 6:
            return  4;
            break;
    }
    return 0;
}


/* Encode secret string into LSBs of image data */
int png_encode_lsb(const char *input_path, const char *output_path, const char *secret)
{
    /*
    Open and validate the input PNG file
    Read and parse the IHDR chunk to determine image properties
    For palette images (color type 3):
        Extract the PLTE chunk
        Build a table of identical-color pairs in the palette
        If no pairs exist, append duplicate colors to create pairs (up to 256 entries)
        Create a mapping table: pair[i] = j if colors at indices i and j are identical
    Decompress all IDAT chunks into a single buffer
    Calculate encoding capacity: width × height pixels
    
    Encode the message:
        For non-palette images: modify the LSB of the first channel of each pixel
        For palette images: swap between identical-color pair indices (lower index = 0, higher = 1)
    Re-compress the modified image data using PNG-compatible zlib settings
    Write output PNG file, preserving all chunks except IDAT (replaced) and PLTE (if modified)
*/
    // 1) open and validate:
    FILE * inputp = png_open(input_path);
    if(!inputp || !input_path || !output_path || !secret){
        return -1;
    }

    // 2) read & parse IHDR chunk to determine properties 
    png_ihdr_t ihdr;
    png_extract_ihdr(inputp, &ihdr);
    if(ihdr.bit_depth != 8){
        fclose(inputp);
        return -1;
    }

    int num_channels = 0;
    switch(ihdr.color_type){
        case 0: num_channels = 1;
        case 1: num_channels = 1; break;
        case 2:
        case 3: num_channels = 1; break;
        case 4: num_channels = 2; break;
        case 6: num_channels = 4; break;
        default:
            fclose(inputp);
            return -1;
    }

    //3) for palette images : build a table of identical color pairs 
    png_color_t * out_colors = NULL;
    size_t out_count;
    png_color_t palette[256];

    int table[256]; // i = j
    memset(table, -1, sizeof(table));

    // use a hashset for unique colors
    Hashset unique_colors = {0};
    int num_pairs = 0; // 

    // for PLTE types: create the duplicate color table
    long ihdr_end = ftell(inputp);
    long plte_offset_start = -1;
    long plte_offset_end = -1;
    //get the palette for palette types
    if(ihdr.color_type == 3){
        png_extract_plte(inputp, &out_colors, &out_count);
        for(int i = 0; i < out_count; i++){
            Node * curr;
            palette[i] = out_colors[i];
            if((curr = set_fetch(&unique_colors, &out_colors[i]))){ // there is a duplicate of plte[i] in the hashset, can record this
                num_pairs++;
                if(curr->index < 128){
                    table[curr->index] = i; // write down the pair
                    table[i] = curr->index;
                }
                remove_node(&unique_colors, palette + i); // remove hte node from the set, so that future pairs can be found
            } else {
                set_put(&unique_colors, out_colors + i, i);  // mark the node in the set
            }
        }

        // must write unique colors
        int num_single = out_count - 2*num_pairs;
        for (int i =0 ; (i < 256 && num_single > 0); i++){
            if(table[i] == -1){
                palette[out_count] = palette[i];
                table[i] = out_count;
                table[out_count] = i;
                out_count++;
                num_single--;
            }
        }
    };

    // 4) decompress IDAT chunks
    png_chunk_t chunk;
    int error_code = 0;
    long idat_start = -1;

    // go to the first IDAT chunk
    fseek(inputp, ihdr_end, SEEK_SET);
    while(!(error_code = png_read_chunk(inputp, &chunk))){
        if(strncmp(chunk.type, "IDAT", 4) == 0){
            idat_start = ftell(inputp) - (12 + chunk.length);
            break;
        } else if(strncmp(chunk.type, "PLTE", 4) == 0){
            plte_offset_end = ftell(inputp);
            plte_offset_start = plte_offset_end - (12 + chunk.length);
            png_free_chunk(&chunk);
        } else {
            png_free_chunk(&chunk);
        }
    };

    if(idat_start == -1){
        fclose(inputp);
        return -1;
    }

    // load all IDAT chunks into buffer
    size_t total_size = 0L;
    uint8_t* data_buffer = NULL;
    long idat_end = -1;
    while(!(error_code) && strcmp(chunk.type, "IDAT") == 0){
        uint8_t* new_ptr = (uint8_t *) realloc(data_buffer, (sizeof(uint8_t)) * (total_size + chunk.length));
        fflush(stdout);
        if(!new_ptr){
            png_free_chunk(&chunk);
            free(data_buffer);
            fclose(inputp);
            return -1;
        }
        data_buffer = new_ptr;
        memcpy(data_buffer + total_size, chunk.data, chunk.length);

        total_size += chunk.length;
        png_free_chunk(&chunk);
        idat_end = ftell(inputp);
        error_code = png_read_chunk(inputp, &chunk);
    }
    if(error_code || idat_end == -1) {
        fclose(inputp);
        return -1;
    }

    // decompress IDAT data into inflated bufer
    uint8_t * inflated_buffer = NULL;
    size_t inflated_size = 0;
    error_code = util_inflate_data(data_buffer, total_size, &inflated_buffer, &inflated_size);
    if(error_code){
        free(data_buffer);
        fflush(stdout);
        fclose(inputp);
        return error_code;
    }
    // 5) Calculate encoding capacity: width × height pixels
    uint32_t encoding_capacity = ihdr.width * ihdr.height;
    size_t secret_len = strlen(secret) + 1;
    //6 ) Check if the message fits (including null terminator): (strlen(secret) + 1) * 8 bits
    if(encoding_capacity < (secret_len) * 8 ){
        free(data_buffer);
        fclose(inputp);
        return -1;
    }
    //7)Encode the message:
    //For palette images: swap between identical-color pair indices (lower index = 0, higher = 1)
    int bytes_per_pixel = (num_channels * ihdr.bit_depth)/8;
    int bytes_per_row = 1 + bytes_per_pixel * ihdr.width;
    int bytes_written = 0;
    int bits_written = 0;
    if(ihdr.color_type == 3){
        int byte = secret[bytes_written];
        for(int scanline = 0; (scanline < ihdr.height*bytes_per_row) && (bytes_written < secret_len); scanline+= bytes_per_row){
            for(int pixel = 1; (pixel < bytes_per_row && bytes_written < secret_len); pixel+=bytes_per_pixel){
                uint8_t palette_index = inflated_buffer[scanline + pixel];
                uint8_t pair = table[palette_index];
                if(byte & 1){
                    inflated_buffer[scanline + pixel] = palette_index > pair ? palette_index : pair;
                } else {
                    inflated_buffer[scanline + pixel] = palette_index < pair ? palette_index : pair;
                }
                // check if need to start writing to the next byte of secret
                if(bits_written%8 == 7){
                    bytes_written++;
                    byte = secret[bytes_written];
                    bits_written = 0;
                } else {
                    bits_written++;
                    byte >>= 1;
                }

            }
        }
    } else {
    //For non-palette images: modify the LSB of the first channel of each pixel
        unsigned char byte = secret[0];
        for(int scanline = 0; (scanline < ihdr.height*bytes_per_row) && (bytes_written < secret_len); scanline+= bytes_per_row){
            //go bit by bit through the byte and record results
            for(int pixel = 1; (pixel < bytes_per_row) && (bytes_written < secret_len); pixel += bytes_per_pixel){
                if(byte & 1){ // find the higher index
                    inflated_buffer[scanline + pixel]|= 1;
                } else {
                    inflated_buffer[scanline + pixel] &= 0xFE;
                }
                // check if need to start writing to the next byte of secret
                if(bits_written%8 == 7){
                    bytes_written++;
                    byte = secret[bytes_written];
                    bits_written = 0;
                } else {
                    bits_written++;
                    byte >>= 1;
                }
            }
        }
    }
   

   //8)Re-compress the modified image data using PNG-compatible zlib settings
    error_code = util_deflate_data_png(inflated_buffer, inflated_size, &data_buffer, &total_size);
    free(inflated_buffer);
    if(error_code){
        free(inflated_buffer);
        free(data_buffer);
        fclose(inputp);
        return -1;
    }

   //9) Write output PNG file, preserving all chunks except IDAT (replaced) and PLTE (if modified)
    FILE * fp = fopen(output_path, "wb");
    if(!fp){
        fclose(inputp);
        return -1;
    } 

    rewind(inputp);
    if(ihdr.color_type == 3 && plte_offset_start != -1){
        // copy the pre: PLTE chunks
        for(int i = 0; i < plte_offset_start; i++){
            fputc(fgetc(inputp), fp);
        };
        write_PLTE(fp, palette, out_count);
        // copy the Plte
        fseek(inputp, plte_offset_end, SEEK_SET);
        for(int i = plte_offset_end; i < idat_start; i++){
            fputc(fgetc(inputp), fp);
        };
    } else { 
        // copy from input file up to IDAT if the plte isn't altered
        for(int i = 0; i < idat_start; i++){
            fputc(fgetc(inputp), fp);
        };
    }
    write_IDAT(fp, data_buffer, total_size);
    fseek(inputp, idat_end, SEEK_SET);
    int c;
    while((c = fgetc(inputp) ) != EOF){
        fputc(c, fp);
    }
    fclose(fp);
    fclose(inputp);
    free(data_buffer);
    return 0;
}





/* Extract secret string from LSBs of image data */
int png_extract_lsb(const char *input_path, char *out, size_t max_len)
{
    // 1) check for NULL parameters b4 opening
    if(!input_path || !max_len || !out){
        return -1;
    }
    FILE * input_fp = png_open(input_path); // open file
    if(!input_fp) return -1;
    
    // 2) read & parse IHDR chunk to determine properties 
    png_ihdr_t ihdr;
    png_extract_ihdr(input_fp, &ihdr);
    if(ihdr.bit_depth != 8){
        fclose(input_fp);
        return -1;
    }

    int num_channels = 0;
    switch(ihdr.color_type){
        case 0: num_channels = 1;
        case 1: num_channels = 1; break;
        case 2:
        case 3: num_channels = 1; break;
        case 4: num_channels = 2; break;
        case 6: num_channels = 4; break;
        default:
            fclose(input_fp);
            return -1;
    }

    //3) for palette images : build a table of identical color pairs 
    png_color_t * out_colors = NULL;
    size_t out_count;
    png_color_t palette[256];
    int identical_colors[256];
    memset(identical_colors, -1, sizeof(identical_colors));
    // use a hashset for unique colors
    Hashset unique_colors = {0}; 
    unique_colors.type = 0;

    int num_pairs = 0; 
    long ihdr_end = ftell(input_fp);
    if(ihdr.color_type == 3){
        png_extract_plte(input_fp, &out_colors, &out_count);
        // for color in colors
        for(int i = 0; i < out_count; i++){
            Node * curr;
            palette[i] = out_colors[i];
            if((curr = set_fetch(&unique_colors, &out_colors[i]))){ // there is a duplicate of plte[i] in the hashset, can record this
                num_pairs++;
                if(1 || curr->index < 128){
                    identical_colors[curr->index] = i; // write down the pair
                    identical_colors[i] = curr->index;
                }
                remove_node(&unique_colors, palette + i); // remove hte node from the set, so that future pairs can be found
            } else {
                set_put(&unique_colors, out_colors + i, i);  // mark the node in the set
            }
        }
        
    };
    // 4) decompress IDAT chunks
    png_chunk_t chunk;
    int error_code = 0;
    long idat_start = -1;
    // go to the first IDAT chunk
    fseek(input_fp, ihdr_end, SEEK_SET);
    while(!(error_code = png_read_chunk(input_fp, &chunk))){
        if(strncmp(chunk.type, "IDAT", 4) == 0){
            idat_start = ftell(input_fp) - (12 + chunk.length);
            break;
        } else if(strncmp(chunk.type, "PLTE", 4) == 0){
            png_free_chunk(&chunk);
        } else {
            png_free_chunk(&chunk);
        }
    };
    if(idat_start == -1 || error_code){
        fclose(input_fp); png_free_chunk(&chunk); 
        return -1;
    }
    // load all IDAT chunks into buffer
    size_t total_size = 0L;
    uint8_t* data_buffer = NULL;
    long idat_end = -1;
    while(!(error_code) && strcmp(chunk.type, "IDAT") == 0){
        uint8_t* new_ptr = (uint8_t *) realloc(data_buffer, (sizeof(uint8_t)) * (total_size + chunk.length));
        fflush(stdout);
        if(!new_ptr){
            png_free_chunk(&chunk);
            free(data_buffer);
            fclose(input_fp);
            return -1;
        }
        data_buffer = new_ptr;
        memcpy(data_buffer + total_size, chunk.data, chunk.length);
        total_size += chunk.length;
        png_free_chunk(&chunk);
        idat_end = ftell(input_fp);
        error_code = png_read_chunk(input_fp, &chunk);
    }
    if(error_code || idat_end == -1) {
        fclose(input_fp);
        return -1;
    }

    // decompress IDAT data into inflated bufer
    uint8_t * inflated_buffer = NULL;
    size_t inflated_size = 0;
    error_code = util_inflate_data(data_buffer, total_size, &inflated_buffer, &inflated_size);
    if(error_code){
        free(data_buffer);
        fclose(input_fp);
        return error_code;
    }
    
    // 5) Calculate encoding capacity: width × height pixels
   //7)Decody the message:
    //For palette images: swap between identical-color pair indices (lower index = 0, higher = 1)
    int bytes_per_pixel = (num_channels * ihdr.bit_depth)/8;
    int bytes_per_row = 1 + bytes_per_pixel * ihdr.width;
    int bytes_written = 0;
    int bits_written = 0;
    char secret[max_len];
    if(ihdr.color_type == 3){
        uint8_t byte = 0;
        uint8_t mask = 1U;
        for(int scanline = 0; (scanline < ihdr.height*bytes_per_row) && (bytes_written < max_len); scanline+= bytes_per_row){
            for(int pixel = 1; (pixel < bytes_per_row && bytes_written < max_len); pixel+=bytes_per_pixel){
                int palette_index = inflated_buffer[scanline + pixel];
                int pair = identical_colors[palette_index];
                if(pair != -1){
                    if(palette_index > pair){ // larger index in the palette
                        byte |= mask; // set to 1
                    }
                    // check if need to start reading next byte
                    if(bits_written%8 == 7 && byte != 0){
                        secret[bytes_written] = byte;
                        bytes_written++;
                        byte = 0;
                        bits_written++;
                        mask = 1U;
                    }else if(bits_written%8 ==7 && byte == 0){
                        secret[bytes_written] = byte;
                        free(data_buffer);
                        memcpy(out, secret, bytes_written);
                        return bytes_written;
                    } else {
                        bits_written++;
                        mask <<= 1;
                    }
                }
            }
        }
    } else {
    //For non-palette images: check the LSB of the first channel of each pixel
        uint8_t byte = 0x0;
        uint8_t mask = 1U;
        for(int scanline = 0; (scanline < ihdr.height*bytes_per_row) && (bytes_written < max_len); scanline+= bytes_per_row){
            //go bit by bit through the byte and record results
            for(int pixel = 1; (pixel < bytes_per_row) && (bytes_written < max_len); pixel += bytes_per_pixel){
                if((inflated_buffer[scanline + pixel]) & (uint8_t)(1)){ // find the higher index
                    byte |= mask;
                }
                // check if need to start writing to the next byte of secret
                if((bits_written%8 == 7) && (byte != 0)){
                    secret[bytes_written] = byte;
                    bytes_written++; bits_written++;
                    byte = 0;
                    mask = 1U;
                } else if(bits_written%8 ==7 && byte == 0){
                    secret[bytes_written] = byte;
                    secret[bytes_written + 1] = '\0';
                    free(data_buffer);
                    memcpy(out, secret, bytes_written);
                    return bytes_written;
                }   else {
                    bits_written++;
                    mask <<= 1;
                }
            }
        }
    }

    free(data_buffer);
    memcpy(out, secret, bytes_written);
    free(inflated_buffer);
    fclose(input_fp);
    return 0;
}

