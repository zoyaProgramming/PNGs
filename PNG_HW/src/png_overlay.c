#include "png_overlay.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_crc.h"
#include "util.h"
#include "debug.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "hashset.h"
#include "global.h"
#include "write_helpers.h"
int n_channels(png_ihdr_t ihdr){
    int num_channels = 0;
    switch(ihdr.color_type){
        case 0: num_channels = 1; break;  // grayscale
        case 2: num_channels = 3; break;  // RGB
        case 3: num_channels = 1; break;  // palette
        case 4: num_channels = 2; break;  // grayscale + alpha
        case 6: num_channels = 4; break;  // RGBA
        default:
            return -1;
    }
    return num_channels;

}

int count_debugs = 1;

int filter_bytes(uint8_t * original, uint8_t ** out, int color_type, int bpp, int bpr, uint32_t height, uint32_t width){
    uint8_t* filtered = (uint8_t*) malloc((size_t) bpr * (size_t) height); // allocate memory for the output
    uint8_t *above = NULL;  // the row above the current row
    for(int scanline = 0; scanline < (height*bpr) - 1; scanline+= bpr ){
        int filter = 0;
        filtered[scanline] = 0;
        if(filter == 0){
            // no filter: copy the whole scanline to filtered
            memcpy(filtered + scanline, original + scanline, bpr); 
        } else if (filter == 1){ 
             //type sub: each byte is set to the current byte - the updated value of the byte left of it
            memcpy(filtered + scanline, original + scanline, bpp + 1); // copy the filter and the first whole color (size bpp)
            for(int byte = bpp + 1; byte < bpr; byte++){
                filtered[ scanline + byte] = ((int)original[byte] - (int)original[byte - bpp]);
            }
        } else if (filter == 2){
            // type above: each byte is set to the current byte - the updated value of the byte above  it
            if(above){
                for (int byte = 1; byte < bpr; byte++)
                    filtered[scanline + byte] = ((int)original[byte] - (int)above[byte]);
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
    *out = filtered;
    return 0;
}


int unfilter_bytes(uint8_t * filtered, int color_type, int bpp, int bpr, uint32_t height, uint32_t width){
    // an empty row for initializing
    uint8_t * unfiltered = (uint8_t*) malloc((size_t) bpr * (size_t) height);
    uint8_t curr_scanline[bpr];
    uint8_t above[bpr];
    for(size_t scanline = 0; scanline < (height*bpr); scanline+=(size_t) bpr ){
        int filter = filtered[scanline];
        unfiltered[scanline] = filter;
        // store all the  the current scanline before changes in memory
        memcpy(curr_scanline, filtered + scanline, bpr);
        if(filter == 0){
            // no filter - copy whole scanline
            memcpy(unfiltered + scanline, filtered + scanline, bpr); 
        } else if (filter == 1){ // sub: each byte = filtered + left
            // Copy first bpp bytes after filter byte (no left neighbor)
            for(int byte = 1; byte <= bpp; byte++){
                unfiltered[scanline + byte] = filtered[scanline + byte];
            }
            // Remaining bytes: add the unfiltered value bpp positions back
            for(int byte = bpp + 1; byte < bpr; byte++){
                unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + (int)unfiltered[scanline + byte - bpp]) % 256;
            }
        } else if (filter == 2){ // up: each byte = filtered + above
            if(scanline){
                for (int byte = 1; byte < bpr; byte++){
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + (int)above[byte]) % 256;
                }
            } else {
                // First scanline: no above, so just copy
                for (int byte = 1; byte < bpr; byte++){
                    unfiltered[scanline + byte] = filtered[scanline + byte];
                }
            }
        } else if (filter == 3){ // average: each byte = filtered + floor((left + above) / 2)
            if(scanline){
                // First bpp bytes: left = 0
                for(int byte = 1; byte <= bpp; byte++){
                    int prediction = above[byte] / 2;
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + prediction) % 256;
                }
                // Remaining bytes
                for(int byte = bpp + 1; byte < bpr; byte++){
                    int left = unfiltered[scanline + byte - bpp];
                    int prediction = (left + above[byte]) / 2;
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + prediction) % 256;
                }
            } else {
                // First scanline: above = 0
                // First bpp bytes: left = 0, above = 0, so prediction = 0
                for(int byte = 1; byte <= bpp; byte++){
                    unfiltered[scanline + byte] = filtered[scanline + byte];
                }
                // Remaining bytes: above = 0
                for(int byte = bpp + 1; byte < bpr; byte++){
                    int left = unfiltered[scanline + byte - bpp];
                    int prediction = left / 2;
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + prediction) % 256;
                }
            }
        } else if (filter == 4) {
            //Paeth
            /*Considers three neighbors: left (a), above (b), and upper-left (c)
            The Paeth predictor algorithm:

                Calculate p = a + b - c (a linear prediction)
                Compute distances from p to each neighbor: pa = |p - a|, pb = |p - b|, pc = |p - c|
                Choose the neighbor closest to p (smallest distance)

            To unfilter: original[i] = filtered[i] + chosen_neighbor (modulo 256)
            This filter typically provides the best compression for most images*/

            if(scanline){
                // First bpp bytes: left = 0, upper-left = 0, so predictor = above
                for(int byte = 1; byte <= bpp; byte++){
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + above[byte]) % 256;
                }
                // Remaining bytes
                for (int byte = bpp + 1; byte < bpr; byte++){
                    int a = unfiltered[scanline + byte - bpp];  // left (already unfiltered)
                    int b = above[byte];                         // above
                    int c = above[byte - bpp];                   // upper-left
                    int p = a + b - c;
                    int pa = abs(p - a);
                    int pb = abs(p - b);
                    int pc = abs(p - c);

                    int predictor;
                    if (pa <= pb && pa <= pc) {
                        predictor = a;
                    } else if (pb <= pc) {
                        predictor = b;
                    } else {
                        predictor = c;
                    }
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + predictor) % 256;
                }
            } else {
                // First scanline: above = 0, upper-left = 0
                // First bpp bytes: all neighbors = 0
                for(int byte = 1; byte <= bpp; byte++){
                    unfiltered[scanline + byte] = filtered[scanline + byte];
                }
                // Remaining bytes: above = 0, upper-left = 0, so predictor = left
                for(int byte = bpp + 1; byte < bpr; byte++){
                    int left = unfiltered[scanline + byte - bpp];
                    unfiltered[scanline + byte] = ((int)filtered[scanline + byte] + left) % 256;
                }
                    
            }
        }
        memcpy(above, unfiltered + scanline, bpr); // set above to unfiltered scanline
    }
    // Copy unfiltered data back to input buffer
    memcpy(filtered, unfiltered, (size_t)bpr * (size_t)height);
    free(unfiltered);
    return 0;
}
int jump_to_idat_plte(FILE * fp, png_chunk_t * out_chunk, uint32_t * plte_offset_start, uint32_t * plte_offset_end, uint32_t * idat_start){
    int error_code;
    png_chunk_t chunk;
    while(!(error_code = png_read_chunk(fp, &chunk))){
        if(strncmp(chunk.type, "IDAT", 4) == 0){
            *idat_start = ftell(fp) - (12 + chunk.length);
            *out_chunk = chunk;
            return 0;
        } else if(strncmp(chunk.type, "PLTE", 4) == 0){
           * plte_offset_end = (uint32_t) ftell(fp);
            * plte_offset_start = *plte_offset_end - (12 + chunk.length);
            png_free_chunk(&chunk);
        } else {
            png_free_chunk(&chunk);
        }
    };
    return -1;
}
int jump_to_idat(FILE * fp, png_chunk_t * out_chunk, uint32_t * idat_start){
    int error_code;
    png_chunk_t chunk;
    while((error_code = png_read_chunk(fp, &chunk)) != -1){
        if(strncmp(chunk.type, "IDAT", 4) == 0){
            *idat_start = ftell(fp) - (12 + chunk.length);
            *out_chunk = chunk;
            return 0;
        }else {
            png_free_chunk(&chunk);
        }
    };
    return -1;
}

uint32_t max(int a, int b){
    return a>b?a:b;
}

uint32_t min(int a, int b){
    return a<b?a:b;
}

int read_idat(FILE * fp, uint8_t ** data_buffer,  uint32_t * idat_end, size_t * total_size){
    // initialize variables for reading IDAT chunk
    
    int error_code = 0;
    size_t temp_total_size = *total_size;
    int found_idat = 0;  // track if we found at least one IDAT
    //scan all idat
    png_chunk_t chunk = {0};
    error_code = png_read_chunk(fp,  &chunk);
    while(!(error_code) && strcmp(chunk.type, "IDAT") == 0){
        found_idat = 1;
        uint8_t* new_ptr = (uint8_t *) realloc(*data_buffer, (sizeof(uint8_t)) * (temp_total_size + chunk.length));
        fflush(stdout);
        if(!new_ptr){
            png_free_chunk(&chunk);
            if(data_buffer) {
                free(*data_buffer);
                *data_buffer = NULL;
            }
            else free(data_buffer);
            return -1;
        }
        *data_buffer = new_ptr;
        memcpy(*data_buffer + temp_total_size, chunk.data, chunk.length);

        temp_total_size += chunk.length;
        png_free_chunk(&chunk);
        *idat_end = ftell(fp);
        error_code = png_read_chunk(fp,  &chunk);
    }
    // Free the last non-IDAT chunk that was read
    if (!error_code) {
        png_free_chunk(&chunk);
    }
    if(error_code || !found_idat) {
        if(*data_buffer) {
            free(*data_buffer);
            *data_buffer = NULL;
        }
        return -1;
    }
    *total_size = temp_total_size;
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
    if((png_extract_ihdr(fp_lg, &ihdr_lg)) == -1 || (png_extract_ihdr(fp_sm, &ihdr_sm)) == -1){
        PRINT_ERROR_PARSE_IHDR();
        return -1;
    }

    // Validate compatibility: both images must have same bit depth (8) and color type
    if(ihdr_sm.bit_depth != 8 || ihdr_lg.bit_depth != 8 || ihdr_lg.color_type != ihdr_sm.color_type){
        PRINT_ERROR_OVERLAY_FAILED();
        return -1;
    }
    int color_type = (int) ihdr_lg.color_type;
    // get bpp (always bit depth)
    int bpp = n_channels(ihdr_lg);
    
    
    // For palette images (color type 3):
    //     Extract PLTE chunks from both images (rewind files to read from beginning)
    //     Merge palettes: add colors from small image that don't exist in large image
    //     Create index mapping: map small image indices to merged palette indices
    //     Remap pixel indices in small image data to use merged palette
    uint8_t * inflated_buf_sm = NULL, *inflated_buf_lg = NULL;
    size_t inflated_size_sm = 0, inflated_size_lg = 0;
    uint32_t idat_start_sm = 0, idat_start_lg = 0;
    uint32_t idat_end_sm = 0, idat_end_lg = 0;
    uint8_t * sm_ancillary = NULL;
    uint32_t sm_ancillary_len = 0;
    size_t total_size_out = 0L;
    
    size_t idat_size_sm=0L, idat_size_lg = 0L;

    uint8_t * out_buf = NULL;
    // Part 1 : extract the data from the small image
    // if the color type is 3, extract mapped data
    if(color_type == 3){
        png_color_t * colors_sm, * colors_lg;
        size_t size_sm, size_lg;
        // Extract PLTE from both files
        fseek(fp_lg, 8, SEEK_SET);  // after PNG signature
        fseek(fp_sm, 8, SEEK_SET);
        if(png_extract_plte(fp_lg, &colors_lg, &size_lg) == -1 || png_extract_plte(fp_sm, &colors_sm, &size_sm) == -1){
            PRINT_ERROR_PLTE_NOT_FOUND();
            fclose(fp_lg); fclose(fp_sm);
            return -1;
        }
        png_color_t * merged_palette = (png_color_t* ) malloc(sizeof(png_color_t) * (size_lg + size_sm));
        Hashset h  = (Hashset){.buckets = {(struct Bucket){0}}, .length = 0, .type = PNG_COLOR_T};

        // Copy  large palette colors to merged palette and track  in hashset
        for(int i = 0; i < size_lg; i++ ){
            merged_palette[i] = colors_lg[i];  // Copy color
            set_put(&h, &colors_lg[i], i);     // Store pointer to original array element
        }
        int merged_len = size_lg;  // Track actual merged palette length
        int index_map[256]; // map sm to lg

        // find matching colors in small palette
        for(int i = 0; i < size_sm; i++ ){
            // check if its already found
            Node * node = set_fetch(&h, &colors_sm[i]);
            if(node != NULL){ //
                index_map[i] = (node->index); 
            } else { // append the new color
                merged_palette[merged_len] = colors_sm[i];
                set_put(&h, &merged_palette[merged_len], merged_len);  // pto to merged palette entry
                index_map[i] = merged_len;
                merged_len++;
            }
        }

        // initialize variables for reading IDAT chunk
        uint8_t* data_buffer = NULL;
        uint32_t plte_start = -1, plte_end = -1;
        png_chunk_t chunk = {0};
        int error_code = 0;

        // jump to IDAT - reset file position first
        fseek(fp_sm, 8, SEEK_SET);
        if( jump_to_idat_plte(fp_sm, &chunk, &idat_start_sm, &plte_start, &plte_end) == -1 ){
            fclose(fp_lg), fclose(fp_sm);
            return -1;
        } else if (plte_start == -1 || plte_end == -1){
            fclose(fp_lg);
            fclose(fp_sm);
            png_free_chunk(&chunk);
            return -1;

        }
        // write into compressed IDAT buffer
        while(!(error_code) && strcmp(chunk.type, "IDAT") == 0){
            uint8_t* new_ptr = (uint8_t *) realloc(data_buffer, (sizeof(uint8_t)) * (idat_size_sm + chunk.length));
            fflush(stdout);
            if(!new_ptr){
                png_free_chunk(&chunk);
                free(data_buffer);
                fclose(fp_sm), fclose(fp_lg);
                return -1;
            }
            data_buffer = new_ptr;
            memcpy(data_buffer + idat_size_sm, chunk.data, chunk.length);
            idat_size_sm += chunk.length;
            png_free_chunk(&chunk);
            idat_end_sm = ftell(fp_sm);
            error_code = png_read_chunk(fp_sm, &chunk);
        }
        if(error_code || idat_end_sm == -1) {
            png_free_chunk(&chunk);
            free(data_buffer);
            fclose(fp_sm), fclose(fp_lg);
            return -1;
        }
        // decompress IDAT data into inflated bufer
        error_code = util_inflate_data(data_buffer, idat_size_sm, &inflated_buf_sm, &inflated_size_sm);
        if(error_code){
            png_free_chunk(&chunk);
            free(data_buffer);
            fclose(fp_sm), fclose(fp_lg);
            return -1;
            return error_code;
        }
        //compressed IDAT BUFFER no longer needed;
        free(data_buffer);
        data_buffer = NULL;
        // remap IDAT chunks in the small image
        // PLTE, so 1 byte per pixel
        uint32_t bpr = (uint32_t) ihdr_sm.width + 1; // 1 pixel per row is filter byte
        for(uint32_t scanline = 0 ; scanline < inflated_size_sm; scanline+= bpr ){
            for(uint32_t byte = 1; byte < bpr; byte ++){
                uint8_t color_index_old  = inflated_buf_sm[scanline + byte]; // the index of the color in old PLTE
                uint8_t color_index_new = index_map[color_index_old];       // the index of the color in merged PLTE
                inflated_buf_sm[scanline + byte] = color_index_new;
            }
        }

        //resize output buffer so it can fit IHDR plus merged PLTE
        // IHDR chunk: 25 bytes
        // PLTE chunk: 4 (length) + 4 (type) + merged_len*3 (data) + 4 (CRC) = 12 + merged_len*3
        uint32_t plte_chunk_size = 12 + merged_len * 3;
        fseek(fp_lg, 8, SEEK_SET);  // start after PNG signature
        uint8_t * temp = realloc(out_buf, 25 + plte_chunk_size);
        if( !temp ){
            fclose(fp_lg); fclose(fp_sm);
            free(out_buf);
            out_buf = NULL;
            free(inflated_buf_sm);
            free_set(&h);
            return -1;
        }
        out_buf = temp;
        total_size_out = 25 + plte_chunk_size; 

        //store IHDR from large file (25 bytes, starting after signature)
        fread(out_buf, 1, 25, fp_lg);
        // Write merged PLTE right after IHDR (position 25)
        if((buf_write_PLTE(out_buf + 25, merged_palette, merged_len)) == -1){
            fclose(fp_lg); fclose(fp_sm);
            free(inflated_buf_sm);
            free(out_buf);
            free_set(&h);
            return -1;
        };
        free_set(&h);

    } else {
        png_chunk_t chunk;
        if(jump_to_idat(fp_sm, &chunk,  &idat_start_sm) == -1){
            fclose(fp_lg), fclose(fp_sm);
            return -1;
        }
        fseek(fp_sm, idat_start_sm, SEEK_SET);

        uint8_t * data = NULL;
        // load compressed  data into data buffer
        if(read_idat(fp_sm, &data, &idat_end_sm, &idat_size_sm) == -1){
            fclose(fp_lg), fclose(fp_sm);
            png_free_chunk(&chunk);
            return -1;
        }

        // inflate the data buffer
        int error_code = util_inflate_data(data, idat_size_sm, &inflated_buf_sm, &inflated_size_sm);
        if(error_code == -1){

            png_free_chunk(&chunk);
            free(data);
            fclose(fp_sm), fclose(fp_lg);
            return -1;
            return error_code;
        }
        // free data buffer and chunk pointers
        free(data); png_free_chunk(&chunk);
        
        // resize out buffer to fit IHDR chunk (4 length + 4 type + 13 data + 4 CRC = 25)
        // plus ancillary chunks from small image (between IHDR and IDAT)
        sm_ancillary_len = idat_start_sm - 33; // 33 = 8 signature + 25 IHDR
        uint8_t * temp = realloc(out_buf, 25);
        total_size_out = 25;
        if( !temp ){
            fclose(fp_lg); fclose(fp_sm);
            free(inflated_buf_sm);
            return -1;
        }
        out_buf = temp;
        // store large IHDR chunk in out_buf
        fseek(fp_lg, 8, SEEK_SET);
        fread(out_buf, 1, 25, fp_lg);
        // save small image ancillary chunks for later (after large ancillary chunks)
        if (sm_ancillary_len > 0) {
            sm_ancillary = malloc(sm_ancillary_len);
            fseek(fp_sm, 33, SEEK_SET);
            fread(sm_ancillary, 1, sm_ancillary_len, fp_sm);
        }
    }

    // Part 2: extract data from the large image
    // jump to first IDAT chunk
    png_chunk_t chunk;
    if(color_type == 3){
        // go to the start of the IDAT in the large image
        uint32_t plte_start = -1, plte_end = -1;
        fseek(fp_lg, 8, SEEK_SET);  // reset to after PNG signature
        if(jump_to_idat_plte(fp_lg, &chunk, &plte_start, &plte_end, &idat_start_lg) == -1){
            free(out_buf); free(inflated_buf_sm);
            fclose(fp_lg); fclose(fp_sm);
            return -1;
        }
        // expand buffer to include ancilliary chunks
        uint32_t ancilliary_len =  (idat_start_lg - plte_end);
        uint8_t * temp = realloc(out_buf,  total_size_out + ancilliary_len);
        if( !temp ){
            png_free_chunk(&chunk);
            fclose(fp_lg); fclose(fp_sm);
            free(inflated_buf_sm); free(out_buf);
            return -1;
        }
        out_buf = temp; 
        // copy ancilliary bytes from fp_lg
        fseek(fp_lg, plte_end, SEEK_SET);
        fread(out_buf + total_size_out, 1, ancilliary_len, fp_lg);
        total_size_out = total_size_out + ancilliary_len; // set the new size

    } else {
        fseek(fp_lg, 8, SEEK_SET); // reset to first chunk after PNG signature
        if(jump_to_idat(fp_lg, &chunk, &idat_start_lg) == -1){
            free(out_buf);
            free(inflated_buf_sm);
            fclose(fp_lg); fclose(fp_sm);
            return -1;
        }
        fseek(fp_lg, idat_start_lg, SEEK_SET);
        uint32_t ancilliary_len =  (idat_start_lg - 33); // 33 = 8 signature + 25 IHDR chunk
        uint8_t * temp = realloc(out_buf,  total_size_out + ancilliary_len);
        if( !temp ){
            png_free_chunk(&chunk);
            fclose(fp_lg); fclose(fp_sm);
            free(inflated_buf_sm); free(out_buf);
            return -1;
        }
         out_buf = temp; 
        // copy ancillary chunks from fp_lg (after IHDR, before IDAT)
        fseek(fp_lg, 33, SEEK_SET);
        fread(out_buf + total_size_out, 1, ancilliary_len, fp_lg);
        total_size_out = total_size_out + ancilliary_len; // set the new size
        
        //  add small image ancillary chunks  to buf(after large ancillary, before IDAT)
        if (sm_ancillary_len > 0 && sm_ancillary != NULL) {
            temp = realloc(out_buf, total_size_out + sm_ancillary_len);
            if (!temp) {
                png_free_chunk(&chunk);
                fclose(fp_lg); fclose(fp_sm);
                free(inflated_buf_sm); free(out_buf); free(sm_ancillary);
                return -1;
            }
            out_buf = temp;
            memcpy(out_buf + total_size_out, sm_ancillary, sm_ancillary_len);
            total_size_out += sm_ancillary_len;
            free(sm_ancillary);
            sm_ancillary = NULL;
        }
    }
    // read compressed idat data into temp buffer:
    uint8_t * idat_temp = NULL;

    if(read_idat(fp_lg, &idat_temp, &idat_end_lg, &idat_size_lg) == -1){
        png_free_chunk(&chunk);
        free(out_buf);
        fclose(fp_lg); fclose(fp_sm);
        return -1;
    }
    fseek(fp_lg, idat_start_lg, SEEK_SET);
    // decompress IDAT data into inflated bufer
    int error_code = util_inflate_data(idat_temp, idat_size_lg, &inflated_buf_lg, &inflated_size_lg);
    if(error_code){
        png_free_chunk(&chunk);
        free(idat_temp);
        free(out_buf); free(inflated_buf_sm); 
        fclose(fp_sm), fclose(fp_lg);
        return -1;
        return error_code;
    }
    free(idat_temp);
    // part 3: unfilter all bytes
    // out buf contains IHDR + PLTE if necessary
    

    int bpr_lg  = bpp* ihdr_lg.width + 1, bpr_sm = bpp*ihdr_sm.width + 1;
    if((unfilter_bytes(inflated_buf_lg, color_type, bpp, bpr_lg, ihdr_lg.height, ihdr_lg.width) == -1) || 
    (unfilter_bytes(inflated_buf_sm, color_type, bpp, bpr_sm, ihdr_sm.height, ihdr_sm.width)) == -1){
        fclose(fp_lg); fclose(fp_sm);
        free(inflated_buf_lg); free(inflated_buf_sm);
         return -1;
    }
    
    // part 5: calculate paste region
    if(x_offset > ihdr_lg.width || y_offset > ihdr_lg.height){
        // cannot be copied
        free(idat_temp);
        free(inflated_buf_lg); free(inflated_buf_sm); free(out_buf);
        fclose(fp_lg); fclose(fp_sm);
        return -1;
    }
    
    // start of x copying range: used to determine whether to copy
    long y_start_safe = y_offset > ihdr_lg.height ? ihdr_lg.height : y_offset;
    long x_start_safe = x_offset > ihdr_lg.width ? ihdr_lg.width : x_offset;
    long paste_x_length = max(0, min((int)ihdr_sm.width, (int)(ihdr_lg.width - x_offset)));  // pixels to copy from small
    // end of x copying range
    long paste_x_end = min((int)(x_offset + ihdr_sm.width), (int)(ihdr_lg.width));
    uint32_t paste_y_end = min((int)(y_offset + ihdr_sm.height), (int)(ihdr_lg.height));
    
    // copy the bytes but set all filter bytes to 0
    uint8_t  * idat = (uint8_t * ) malloc(inflated_size_lg);
    if(idat == NULL){
        free(inflated_buf_lg); free(inflated_buf_sm); free(out_buf);
        fclose(fp_lg); fclose(fp_sm);
        return -1;
    }
    memset(idat, 0, inflated_size_lg);

    // for(long scanline = 0; scanline < ihdr_lg.height; scanline++){
    //     memcpy(idat + scanline * bpr_lg + 1, inflated_buf_lg + scanline * bpr_lg + 1, bpr_lg + 1);
    // }
    for(long scanline = 0; scanline < y_start_safe; scanline++){
        idat[scanline * bpr_lg] = 0;  // filter byte
        uint8_t  * copy_to = idat + scanline * bpr_lg + 1;
        uint8_t * copy_end = inflated_buf_lg  + scanline * bpr_lg + 1;
        memcpy(copy_to,copy_end , bpr_lg - 1);
    }
    // copy all other bytes;
    for(long scanline = y_start_safe; scanline < paste_y_end; scanline++){
         idat[scanline * bpr_lg] = 0;  // filter byte
        uint8_t *  copy_to =  idat + scanline * bpr_lg + 1; 
        uint8_t  * lg_ptr = inflated_buf_lg +  scanline * bpr_lg + 1;
        uint8_t *  xsm = inflated_buf_sm +  (scanline - y_offset) * bpr_sm + 1;  // use small image scanline
        
        memcpy(copy_to, lg_ptr,  x_start_safe * bpp); // copy the bytes up to offset
        memcpy(copy_to + x_start_safe * bpp, xsm, paste_x_length * bpp);  // copy from start of small row
        memcpy(copy_to + paste_x_end * bpp, lg_ptr + paste_x_end * bpp, (ihdr_lg.width - paste_x_end) * bpp);
    }
   
    for(size_t scanline = paste_y_end; scanline < ihdr_lg.height; scanline++){
        idat[scanline * bpr_lg] = 0;  // filter byte
        uint8_t * copy_to = idat +  scanline * bpr_lg + 1;
        uint8_t  * copy_end =  inflated_buf_lg + scanline * bpr_lg + 1;
        memcpy(copy_to, copy_end, bpr_lg - 1);
    }

    // Data is already in filter-0 format (filter bytes set to 0 in copy loops)
    uint8_t*  deflated = NULL;
    size_t deflated_size = 0L;
    if(util_deflate_data_png(idat, inflated_size_lg, &deflated, &deflated_size) == -1){
        free(inflated_buf_lg);
        free(inflated_buf_sm);
        free(idat);
        fclose(fp_lg);
        fclose(fp_sm);
        return -1;
    }

    char c;

    // close all files and free pointers because data is in output buf
    free(inflated_buf_lg);
    free(inflated_buf_sm);
  //  fclose(fp_sm);
    FILE * fp_out = fopen(output_path, "w+");
    if(fp_out == NULL){
        return -1;
    }
    // write PNG signature
    uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(png_sig, 1, 8, fp_out);
    // write the out buf IHDR -> ancilliary chunks
    for(int i = 0; i <  total_size_out; i++ ){
        fputc(out_buf[i], fp_out);
    }
    write_IDAT(fp_out, deflated, deflated_size);
    free(deflated);
    free(idat);
    // Seek past all original IDAT chunks to copy only IEND
    fseek(fp_lg, idat_end_lg, SEEK_SET);
    while((c = fgetc(fp_lg) ) != EOF){
        fputc(c, fp_out);
    }
    fclose(fp_lg); fclose(fp_out);
    return 0;
    
}



    // try to open output path
    
 
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