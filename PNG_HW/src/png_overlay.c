#include "png_overlay.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_crc.h"
#include "util.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "hashset.h"
#include "global.h"

int filter_bytes(uint8_t * original, uint8_t ** out, int color_type, int bpp, int bpr, size_t height, size_t width){
    uint8_t* filtered = (uint8_t*)malloc(bpr * height); // allocate memory for the output
    uint8_t *above = NULL;  // the row above the current row

    for(int scanline = 0; scanline < (height*bpr) - 1; scanline+= bpr ){
        int filter = original[0];
        if(filter == 0){
            // no filter: copy the whole scanline to filtered
            memcpy(filtered + scanline, original + scanline, bpr); 
        } else if (filter == 1){ 
             //type sub: each byte is set to the current byte - the updated value of the byte left of it
            memcpy(filtered + scanline, original + scanline, bpp + 1); // copy the filter and the first whole color (size bpp)
            for(int byte = bpp + 1; byte < bpr; byte++){
                filtered[byte] = ((int)original[byte] - (int)original[byte - bpp]);
            }
        } else if (filter == 2){
            // type above: each byte is set to the current byte - the updated value of the byte above  it
            if(above){
                for (int byte = 1; byte < bpr; byte++)
                    filtered[byte] = ((int)original[byte] - (int)above[byte]);
            } else {
                memcpy(filtered + scanline, original + scanline, bpr); // preserve the original scanline
            }
        } else if (filter == 3){ //( average)
            if(above){
                int left = original[scanline+ 1];
                for(int byte = 1; byte < bpp + 1; byte ++) // filter the first byte on the scanline which has no left byte
                    filtered[scanline + byte] = (original[scanline + byte] - above[byte]/2U);

                for (int byte = bpp + 1; byte < bpr; byte++){ // filter remaining bytes on the scanline
                    int prediction = (left + above[byte])/2;
                    filtered[scanline + byte] = ((int)original[scanline + byte] - prediction) ;
                    left = (int)original[scanline + byte];
                }
            } else {
                for (int byte = 1; byte < bpr; byte++){
                    int prediction = (original[byte - bpp])/2;
                    filtered[scanline + byte] = ((int)original[scanline + byte] - (int)prediction);
                }
            }
        } else if (filter == 4) {//Paeth
            /*Considers three neighbors: left (a), above (b), and upper-left (c)
            The Paeth predictor algorithm:

                Calculate p = a + b - c (a linear prediction)
                Compute distances from p to each neighbor: pa = |p - a|, pb = |p - b|, pc = |p - c|
                Choose the neighbor closest to p (smallest distance)

            To unfilter: original[i] = filtered[i] + chosen_neighbor (modulo 256)
            This filter typically provides the best compression for most images*/

            if(above){
    
                for(int byte = 1; byte < bpp + 1; byte ++) // unfilter the first byte on the scanline which has no left byte
                    filtered[scanline + byte] = (original[scanline + byte] + above[byte])%256;
                int a = (int)original[scanline + 1];
                int c = (int)above[1];
                for (int byte = bpp + 1; byte < bpr; byte++){// unfilter remaining bytes on the scanline
                    int p = (a + above[byte] + c);
                    int d_pa = abs(p - a);              // |p -a|   
                    int d_pb = abs(p - above[byte]);    // |p -b|   
                    int d_pc = abs(p - c);              // |p -c|   
                    if((d_pa <= d_pb) && (d_pa <= d_pc) ){ // closest neigbor is left
                        filtered[scanline + byte] = ((int)original[scanline + byte] - a);
                    } else if((d_pb <= d_pa ) && (d_pb <= d_pc)) { // closest neighbor is above
                        filtered[scanline + byte] = (uint8_t)((int)original[scanline + byte] - (uint8_t)above[byte]);
                    } else  { // closest neighbor upper left
                        filtered[scanline + byte] = (uint8_t)((int)original[scanline + byte] - c);
                    } 
                    // reset variables 
                    a = (int)original[scanline + byte]; c = (int)above[byte];
                }
            } else { 
                // first scanline: compute similar to the sub. method
                memcpy(filtered + scanline, original + scanline, bpp + 1); // copy the filter and the first whole color (size bpp)
                int left = original[scanline + 1]; //
                for(int byte = bpp + 1; byte < bpr; byte++){
                    filtered[scanline + byte] = ((int)original[scanline + byte] - left);
                    left = (int)original[scanline + byte];
                }
            }
        }
        above = original + scanline;
            
    }
    return 0;
}


int unfilter_bytes(uint8_t * filtered, int color_type, int bpp, int bpr, size_t height, size_t width){
    // an empty row for initializing
   
    uint8_t curr_scanline[bpr];
    uint8_t above[bpr];

    uint8_t * above = NULL;

    for(int scanline = 0; scanline < (height*bpr) - 1; scanline+= bpr ){
        int filter = filtered[0];
        filtered[scanline] = filter;
        // store all the  the current scanline before changes in memory
        memcpy(curr_scanline, filtered + scanline, bpr);

        if(filter == 0){
        } else if (filter == 1){
            int left = filtered[1];
            for(int byte = bpp; byte < bpr; byte++){
                int filtered_val  = ((int)filtered[byte] + left)%256;
                left = filtered[byte];
                filtered[byte] = filtered_val;
            }
        } else if (filter == 2){
            if(scanline){
                for (int byte = 1; byte < bpr; byte++){
                    filtered[byte] = ((int)filtered[byte] + (int)above[byte]) % 256;
                }
            } else {
                for (int byte = 1; byte < bpr; byte++){
                    filtered[byte] = ((int)filtered[byte]) % 256;
                }
            }
        } else if (filter == 3){ //( average)
            if(scanline){
                int left = (int)filtered[scanline+ 1];
                for(int byte = 1; byte < bpp + 1; byte ++) // unfilter the first byte on the scanline which has no left byte
                    filtered[scanline + byte] = (filtered[scanline + byte] + above[byte]/2U)%256;

                for (int byte = bpp + 1; byte < bpr; byte++){ // unfilter remaining bytes on the scanline
                    int prediction = (left + above[byte])/2;
                    left = (int)filtered[scanline + byte]; // store the unfiltered value of the current byte
                    filtered[byte] = ((int)filtered[byte] + prediction) % 256;
                }
            } else {
                // if this is the first scanline, only consider the left byte
                uint8_t left = (int)filtered[scanline+ 1]; // record the left byte
                for (int byte = 1; byte < bpr; byte++){
                    int prediction = (filtered[scanline + 1])/2;
                    left = filtered[scanline+ 1];
                    filtered[byte] = ((int)filtered[byte] + prediction) % 256;
                    
                }
            }
        } else if (filter == 4) {//Paeth
            /*Considers three neighbors: left (a), above (b), and upper-left (c)
            The Paeth predictor algorithm:

                Calculate p = a + b - c (a linear prediction)
                Compute distances from p to each neighbor: pa = |p - a|, pb = |p - b|, pc = |p - c|
                Choose the neighbor closest to p (smallest distance)

            To unfilter: original[i] = filtered[i] + chosen_neighbor (modulo 256)
            This filter typically provides the best compression for most images*/

            if(scanline){
                int a = (int)filtered[scanline + 1], c = (int)above[1];
                for(int byte = 1; byte < bpp + 1; byte ++) // unfilter the first byte on the scanline which has no left byte
                    filtered[scanline + byte] = (filtered[scanline + byte] + above[byte])%256;

                for (int byte = bpp + 1; byte < bpr; byte++){// unfilter remaining bytes on the scanline
                    int p = (a + above[byte] + c);
                    int d_pa = abs(p - a);              // |p -a|   
                    int d_pb = abs(p - above[byte]);    // |p -b|   
                    int d_pc = abs(p - c);              // |p -c|  

                    int  filtered_val = 0;
                    if((d_pa <= d_pb) && (d_pa <= d_pc) ){ // closest neigbor is left
                        filtered_val = ((int)filtered[scanline + byte] + a)%256;
                    } else if((d_pb <= d_pa ) && (d_pb <= d_pc)) { // closest neighbor is above
                        filtered_val = (uint8_t)((int)filtered[scanline + byte] + (uint8_t)above[byte])%256;
                    } else  { // closest neighbor upper left
                        filtered_val = (uint8_t)((int)filtered[scanline + byte] + c)%256;
                    } 
                    // reset variables 
                    a = (int)filtered[scanline + byte]; c = (int)above[byte];
                    filtered[scanline + byte ] = filtered_val; // set the current val inplace
                }
            } else {
                int left = filtered[scanline + 1]; // store
                for(int byte = bpp + 1; byte < bpr; byte++){
                    int filtered_val= ((int)filtered[scanline + byte] + left)%256;
                    left = (int)filtered[scanline + byte]; 
                    filtered[scanline + byte] = filtered_val;
                }
                    
            }
        }
        memcpy(above, curr_scanline, bpr); // set above to new scanline

    }
    
    return 0;
}

int png_overlay_paste(const char *large_path, const char *small_path,
                      const char *output_path, uint32_t x_offset, uint32_t y_offset)
{
    
//     Open both input files and validate PNG signatures
    
    FILE * fp_lg = png_open(large_path), *fp_sm = png_open(small_path);
    if(!fp_lg){
        PRINT_ERROR_OPEN_FILE(large_path);
        return -1;
    }
    if(!fp_sm){
        PRINT_ERROR_OPEN_FILE(small_path);
        return -1;
    }  
    // Read and parse IHDR chunks from both images
    png_ihdr_t ihdr_sm, ihdr_lg;
    if(!png_extract_ihdr(fp_lg, &ihdr_lg) || !png_extract_ihdr(fp_sm, &ihdr_sm)){
        PRINT_ERROR_PARSE_IHDR();
        return -1;
    }
    // Validate compatibility: both images must have same bit depth (8) and color type
    if(ihdr_sm.bit_depth != 8 || ihdr_lg.bit_depth != 8 || ihdr_lg.color_type != ihdr_sm.color_type){
        PRINT_ERROR_OVERLAY_FAILED();
        return -1;
    }
    int color_type = ihdr_lg.color_type;
    
    // For palette images (color type 3):
    //     Extract PLTE chunks from both images (rewind files to read from beginning)
    //     Merge palettes: add colors from small image that don't exist in large image
    //     Create index mapping: map small image indices to merged palette indices
    //     Remap pixel indices in small image data to use merged palette
    
    if(color_type == 3){
        png_color_t * colors_sm, * colors_lg;
        
        size_t size_sm, size_lg;
        png_chunk_t plte_sm, plte_lg;
        if(png_parse_plte(&plte_lg, &colors_lg, &size_lg) == -1 || png_parse_plte(&plte_sm, &colors_sm, &size_sm) == -1){
            PRINT_ERROR_PLTE_NOT_FOUND();
            return -1;
        }
        png_color_t * merged_palette = malloc(sizeof(png_color_t) * (size_lg + size_sm));
        Hashset h  = {.buckets = {0}, .length = 0, .type = PNG_COLOR_T};
        for(int i = 0; i<  size_lg; i++ ){
            png_color_t current_color = colors_lg[i];
            if (set_put(&h, &current_color, i) == 0) // if its unique, add it to merged palette
                merged_palette[i] = current_color;

        }
        size_t merged_size =  h.length;
        int index_map[256]; // map sm to lg
        // find matching colors in small palette
        for(int i = 0; i < size_sm; i++ ){
            png_color_t current_color = colors_sm[i];
            // check if its already found
            Node * node = set_fetch(&h, &current_color);
            if(node != NULL){
                index_map[i] = (node->index); // map smaller 
            } else {
                merged_palette[h.length] = current_color;
                set_put(&h, &current_color, h.length);
            }
        }
        png_chunk_t data;
        




    }

// Reopen files and skip IHDR chunks for IDAT processing
// Decompress all IDAT chunks from both images into single buffers
// Unfilter scanlines to reconstruct actual pixel values (handles filter types 0-4)
// Paste operation: replace pixels in large image with pixels from small image

//     Calculate paste region (may be clipped if small image extends beyond large)
//     Copy pixels directly using memcpy (no alpha blending)
//     Handle scanline structure: skip filter byte, copy pixel data

// Re-filter scanlines (set all filter bytes to type 0: None)
// Compress modified image data using zlib with PNG-compatible settings
// Write output file with proper chunk ordering:

//     PNG signature
//     IHDR chunk
//     PLTE chunk (if palette image, using merged palette)
//     Ancillary chunks from source (tRNS, bKGD, etc.) between PLTE and IDAT
//     IDAT chunk with compressed data
//     Remaining ancillary chunks from source (after IDAT)
//     IEND chun
    if(!large_path || !small_path || !output_path ){ // check for null
        return -1;
    }
    return 0;
}


// int num_channels = 0;
    // switch(ihdr.color_type){
    //     case 0: num_channels = 1;
    //     case 1: num_channels = 1; break;
    //     case 2:
    //     case 3: num_channels = 1; break;
    //     case 4: num_channels = 2; break;
    //     case 6: num_channels = 4; break;
    //     default:
    //         fclose(inputp);
    //         return -1;
    // }
    // int bytes_per_pixel = (num_channels * ihdr.bit_depth)/8;
    // int bytes_per_row = 1 + bytes_per_pixel * ihdr.width;
    // get all filter bytes