#include "write_helpers.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "png_chunks.h"
#include "util.h"
#include "png_crc.h"
#include <string.h>


void write_be_buf(char (* be_buf)[], uint32_t n){
    (*be_buf)[0] = (n >> 24) & 0xFF;
    (*be_buf)[1] = (n >> 16) & 0xFF;
    (*be_buf)[2] = (n >> 8) & 0xFF;
    (*be_buf)[3] = n & 0xFF;
}
int write_IDAT(FILE * fp, uint8_t * buf, uint32_t size){
    if(buf == NULL){
        return -1;
    }
    //write the size to the IDAT chunk
    char be_buf[4] = {0};
    
    write_be_buf(&be_buf, size);
    fwrite(be_buf, 1, 4, fp);

    fwrite("IDAT", 1, 4, fp);
    fwrite(buf, 1, size, fp);
    
    // calculate crc
    uint8_t temp_data_storage[size + 5];
    memcpy(temp_data_storage, "IDAT", 4);
    memcpy(temp_data_storage + 4, buf, size);
    temp_data_storage[size + 4] = 0;
 
    uint32_t calculated_crc = png_crc(temp_data_storage, (size_t)(size + 4));

    write_be_buf(&be_buf, calculated_crc);
    fwrite(be_buf, 1, 4, fp);
    return 0;
}


int write_PLTE(FILE * fp, png_color_t * buf, uint32_t size){
    if(buf == NULL){
        return -1;
    }
    //write the size to the IDAT chunk
    char be_buf[4] = {0};
    write_be_buf(&be_buf, size*sizeof(png_color_t)); // store be string in be_bug
    fwrite(be_buf, 1, 4, fp); // write big endian to file

    fwrite("PLTE", 1, 4, fp); // write "PLTE" to file
    fwrite(buf, sizeof(png_color_t), size, fp); // write all color data to file
    
    // calculate crc
    uint8_t temp_data_storage[size*sizeof(png_color_t) + 5];
    memcpy(temp_data_storage, "PLTE", 4);
    memcpy(temp_data_storage + 4, buf, size*sizeof(png_color_t));
    temp_data_storage[size*sizeof(png_color_t) + 4] = 0;
 
    
    uint32_t calculated_crc = png_crc(temp_data_storage, size*sizeof(png_color_t) + 4);

    write_be_buf(&be_buf, calculated_crc);
    fwrite(be_buf, 1, 4, fp);
    return 0;
}