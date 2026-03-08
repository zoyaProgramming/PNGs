#include "png_chunks.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Parse IHDR data from chunk */
/* Chunk must be an IHDR chunk with length 13 */
int png_parse_ihdr(const png_chunk_t *chunk, png_ihdr_t *out)
{
    if(chunk == NULL || out == NULL || chunk->length != 13U){
        return -1;
    } 
    uint8_t * dataPtr  = chunk->data;
    *out = *(png_ihdr_t*)dataPtr;

    out->width = read_u32_be((uint8_t*)&out->width);
    out->height = read_u32_be((uint8_t *)&out->height);

    // check for invalid IHDR chunk values :
    if((out->bit_depth != 1 && out->bit_depth != 2 && out->bit_depth != 4 && out->bit_depth != 8 && out->bit_depth != 16) ||
    (out->color_type != 0 && out->color_type != 2 && out->color_type != 3 && out->color_type != 4&& out->color_type != 6)||
    (out->compression != 0 || out->filter != 0 || out->interlace != 0 || out->interlace > 1 )){
        return -1;
    }
    return 0;

}

/* Parse PLTE data from chunk into an allocated array of colors */
/* Chunk must be a PLTE chunk with length multiple of 3 */
int png_parse_plte(const png_chunk_t *chunk, png_color_t **out_colors, size_t *out_count)
{
    if(!chunk || !out_colors || !out_count || chunk->length % 3 || (chunk->length)/3 > 256){ // check for invalid inputs
        return -1;
    }

    // allocaate color array
    *out_colors = malloc(sizeof(png_color_t) * (chunk->length)/3);
    if(!*out_colors){
        return -1;
    }
    uint32_t len = chunk->length;
    *out_count = len/3;
    // copy to outcolors
    // should be aligned correctly by default
    memcpy(*out_colors, chunk->data, (size_t)len);
    return 0;
}