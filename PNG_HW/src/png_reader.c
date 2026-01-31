#include "png_reader.h"
#include "png_crc.h"
#include "util.h"
#include "global.h"

#include <stdlib.h>
#include <string.h>


/* Opens a PNG file and validates signature */
FILE *png_open(const char *path)
{
    const uint64_t signature = 0x89504e470d0a1a0a;
    const FILE * fp = fopen(path, "r"); 
    if(!fp){
        PRINT_ERROR_OPEN_FILE(path);
        return NULL;
    }
    uint8_t buf[8];
    if(read_exact(fp, buf, 16) == EXIT_FAILURE){
        PRINT_ERROR_OPEN_FILE(path);
        return NULL;
    }
    uint8_t * signature_as_uint8_t = (uint8_t* ) &signature;
    for(int i = 0; i < 8; i++){
        if(signature_as_uint8_t[i] != buf[i]){
            PRINT_ERROR_OPEN_FILE(path);
            return NULL;
        }
    }
    return fp;
}

/* Reads the next chunk from the file */
int png_read_chunk(FILE *fp, png_chunk_t *out)
{
    // Length (4 bytes): The number of bytes in the data field, represented as a big-endian unsigned 32-bit integer.
    // Type (4 bytes): A 4-byte ASCII string identifying the chunk type (e.g., "IHDR", "PLTE", "IDAT", "IEND").
    // Data (variable length): The actual chunk data, whose length is specified by the Length field.
    // CRC (4 bytes): A cyclic redundancy check computed over the type and data of the chunk, represented as a big-endian unsigned 32-bit integer.
    int return_code = read_exact(fp, (uint8_t*)(&out->length), 4); // read the first 4 bytes
    // read length
    if(return_code != 0){
        return EXIT_FAILURE;
    }
    out->data = malloc(sizeof(uint32_t) * out->length);
    // handle malloc errors
    if(out->data == NULL){
        return EXIT_FAILURE;
    }
    // read type
    return_code = read_exact(fp, (uint8_t*)(&out->type), 4);
    if(return_code != 0){
        return EXIT_FAILURE;
    }
    // read data
    return_code = read_exact(fp, (uint8_t*)(&out->data), (size_t)(&out->length));
    if(return_code != 0){
        return EXIT_FAILURE;
    }
    // read CRC
    return_code = read_exact(fp, (uint8_t*)(&out->crc), (4UL));
    if(return_code != png_crc(out->data, out->length)){ // crc read error
        return EXIT_FAILURE;
    }
    return 0;
}

/* Frees memory allocated inside png_chunk_t */
void png_free_chunk(png_chunk_t *chunk)
{
    if(chunk){
        free(chunk->data);
        chunk->data = NULL;
    }
    
}

//
int png_extract_ihdr(FILE *fp, png_ihdr_t *out)
{
    //Extracts and parses the IHDR chunk from a PNG file. 
    //This is a convenience function that reads the first chunk, validates it is IHDR, parses it, and frees the chunk.
    
    return 0;
}

int png_extract_plte(FILE *fp, png_color_t **out_colors, size_t *out_count)
{
    return 0;
}

int png_summary(const char *filename, png_chunk_t **out_summary)
{
    return 0;
}