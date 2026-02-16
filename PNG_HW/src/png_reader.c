#include "png_reader.h"
#include "png_crc.h"
#include "util.h"
#include "global.h"

#include <stdlib.h>
#include <string.h>


/* Opens a PNG file and validates signature */
FILE *png_open(const char *path)
{   
    //0x89 50 4e 47 0d 0a 1a 0a
    //PNG signature: found at top of all png files
    //char * because  no endianness issues as opposed to uint_64t
    char signature[] = {0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
    if(path == NULL){
        return NULL;
    }
    FILE * fp = fopen(path, "r"); 
    if(!fp){
        return NULL;
    }
    uint8_t buf[8];
    if(read_exact(fp, buf, 8) == 1){
        fclose(fp);
        return NULL;
    }
    
    if(strncmp((char*) buf, signature, 8) != 0){
        fclose(fp);
        return NULL;
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
    if(out == NULL){
        
        return -1;
    }
    
    int return_code = read_exact(fp, (uint8_t*)(&out->length), 4); // read the first 4 bytes
    // read length
    if(return_code != 0){
        return -1;
    }
    out->length = read_u32_be((uint8_t*)(&out->length));
    out->data = malloc(sizeof(uint8_t) * out->length);
    // handle malloc errors
    if(out->data == NULL){
        return -1;
    }
    // read type
    return_code = read_exact(fp, (uint8_t*)(&out->type), 4);
    if(return_code != 0){
        return -1;
    }
    out->type[4] = '\0';
    
    
    // read data
    return_code = read_exact(fp, out->data, (size_t)(out->length));
    if(return_code != 0){
        return -1;
    }

    // read CRC
    uint8_t crc_buf[4] = {0};
    return_code = read_exact(fp, crc_buf, 4);
    if(return_code){
        return -1;
    }
    uint32_t crc = read_u32_be(crc_buf);
    // concatenate
    uint8_t temp_data_storage[out->length + 5];
    memcpy(temp_data_storage, out->type, 4);
    memcpy(temp_data_storage + 4, out->data, out->length);
    
    temp_data_storage[out->length + 4] = 0;
    uint32_t calculated_crc = png_crc(temp_data_storage, (size_t)out->length + 4);
    if(crc != calculated_crc){ // crc read error
        out->crc = 0;
        png_free_chunk(out);
        return 0;
    }
    out->crc = 1;
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
    if(!fp || !out){
        return -1;
    }
    png_chunk_t chunk;
    if(png_read_chunk(fp, &chunk) || !chunk.crc){ 
        // check for an error or an invalid crc
        PRINT_ERROR_READ_CHUNKS();
        return -1;
    }
    if(strncmp(chunk.type, "IHDR", 4)){
        PRINT_ERROR_PARSE_IHDR();
        return -1;
    }
    if(png_parse_ihdr(&chunk, out)){
        PRINT_ERROR_PARSE_IHDR();
        return -1;
    };
    png_free_chunk(&chunk);
    return 0;
}

int png_extract_plte(FILE *fp, png_color_t **out_colors, size_t *out_count)
{   
    if(!fp || !out_colors || !out_count){ // null pointers
        return -1;
    }
    png_chunk_t chunk;
    char done = 0;
    while(!done && !(png_read_chunk(fp, &chunk)) && chunk.crc ){
        if(strncmp(chunk.type, "PLTE", 4) == 0) // reached PLTE chunk
            done = 1;
        else if (!strncmp(chunk.type, "IDAT", 4) || !strncmp(chunk.type, "IEND", 4)) { 
            // if its equal to IDAT or IEND return
            PRINT_ERROR_PLTE_NOT_FOUND();
            return -1;
        }
    }
    if(!done) { // early termination due to error
        PRINT_ERROR_PLTE_NOT_FOUND();
        return -1;
    }

    if(png_parse_plte(&chunk, out_colors, out_count)){
        PRINT_ERROR_PLTE_NOT_FOUND();
        return -1;
    };
    png_free_chunk(&chunk);

    return 0;
}

int png_summary(const char *filename, png_chunk_t **out_summary)
{
    //Reads all chunks from a PNG file and returns a summary array. 
    //This function reads chunks until IEND is encountered, storing only the chunk type, length, and CRC validity status
    // (not the actual chunk data).
    //This is useful for displaying chunk information without loading all chunk data into memory.
    fflush(stdout);
    if(!filename || !(out_summary)){
        PRINT_ERROR_OPEN_FILE("file");
        fflush(stdout);

        return -1;
    }
    FILE * fp = png_open(filename);
    if(!fp){ // error opening file
        return -1;
    }
    int n_chunks = 0;
    png_chunk_t chunk;
    int code = png_read_chunk(fp, &chunk);
    // while not IEND
    while(!code){ 
        // if code didn't reach the crc check
        png_chunk_t * new_ptr = realloc(*out_summary, (n_chunks + 1)  * sizeof(png_chunk_t));
        if(!new_ptr){ // malloc failed
            fclose(fp);
            return -1;
        }
        png_free_chunk(&chunk);
        chunk.data = NULL;

        new_ptr[n_chunks] = chunk;
        *out_summary = new_ptr;
        if(strncmp(chunk.type, "IEND", 4) == 0){
            fclose(fp);
            return 0;
        }
        code = png_read_chunk(fp, &chunk);
        n_chunks++;
    }
    free(*out_summary);
    fclose(fp);
    return -1;
}