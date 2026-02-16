#ifndef WRITE_HELPERS_H
#define WRITE_HELPERS_H
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "util.h"
#include "png_crc.h"
#include <string.h>
#include "png_chunks.h"
/*

typedef struct {
    uint32_t length;
    char     type[5];   Null-terminated 
    uint8_t *data;   length bytes 
    uint32_t crc;
} png_chunk_t;
*/


void write_be_buf(char ** be_buf, uint32_t n);
int write_IDAT(FILE * fp, uint8_t * buf, uint32_t size);
int write_PLTE(FILE * fp, png_color_t * buf, uint32_t size);
#endif
