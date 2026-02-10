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

    //89 50 4e 47  0d 0a 1a 0a
    unsigned char signature[] = {0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
    FILE * fp = fopen(path, "r"); 
    if(!fp){
        PRINT_ERROR_OPEN_FILE(path);
        return NULL;
    }
    uint8_t buf[8];
    if(read_exact(fp, buf, 8) == EXIT_FAILURE){
        PRINT_ERROR_OPEN_FILE(path);
        return NULL;
    }
    
    for(int i = 0; i < 8; i++){
        if(signature[i] != buf[i]){
            fflush(stdout);
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
    out->length = read_u32_be((uint8_t*)(&out->length));
    out->data = malloc(sizeof(uint8_t) * out->length);
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
    
    return_code = read_exact(fp, out->data, (size_t)(out->length));
    if(return_code != 0){
        return EXIT_FAILURE;
    }

    // read CRC
    uint8_t crc_temp[4] = {0};
    return_code = read_exact(fp, crc_temp, 4);
    if(return_code){
        return EXIT_FAILURE;
    }
    out->crc = read_u32_be(crc_temp);
    
    // concatenate
    uint8_t temp_data_storage[out->length + 5];
    memcpy(temp_data_storage, out->type, 4);
    memcpy(temp_data_storage +4, out->data, (size_t)13);
    
    temp_data_storage[out->length + 4] = 0;
    uint32_t calculated_crc =   png_crc(temp_data_storage, (size_t)out->length + 4);
    if(out->crc != calculated_crc){ // crc read error
        png_free_chunk(out);
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
    if(!fp || !out){
        return -1;
    }
    png_chunk_t chunk;
    if(png_read_chunk(fp, &chunk)){
        PRINT_ERROR_READ_CHUNKS();
        return -1;
    }
    char * str = "IHDR";
    for(char* t = chunk.type; t<chunk.type + 4; t++){
        if(*t != *str){
            printf("failed\n");
            fflush(stdout);
            PRINT_ERROR_PARSE_IHDR();
            return -1;
        }
        str++;
    }

    if(png_parse_ihdr(&chunk, out)){
        printf("failed parsing\n");
        fflush(stdout);
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

    while(!done && !(png_read_chunk(fp, &chunk)) ){
        if(strncmp(chunk.type, "PLTE", 4) == 0)
            done = 1;
        else if (!strncmp(chunk.type, "IDAT", 4) || !strncmp(chunk.type, "IDAT", 4)) {
            PRINT_ERROR_PLTE_NOT_FOUND();
            return -1;
        }
    }
    if(!done) { // early termination due to error
        PRINT_ERROR_PLTE_NOT_FOUND();
        return -1;
    }

    if(png_parse_plte(&chunk, out_colors, out_count)){
        printf("failed parsing\n");
        fflush(stdout);
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
    
    FILE * fp = png_open(filename);
    if(!fp){ // error opening file
        return EXIT_FAILURE;
    }

    
    int n_chunks = 0;
    png_chunk_t temp;
    int code = png_read_chunk(fp, &temp);

    while(strncmp(temp.type, "IEND", 4)){
        n_chunks++;
        // if code didn't reach the crc check
        if(code && temp.crc == NULL){
            return EXIT_FAILURE;
        } else {
            
        }
        png_chunk_t * new_ptr = realloc(*out_summary, n_chunks * sizeof(png_chunk_t));
        new_ptr[n_chunks - 1] = 

        if(!new_ptr){ // malloc failed
            return EXIT_FAILURE;
        }
        code = png_read_chunk(fp, &temp);

    }
    return 0;
}