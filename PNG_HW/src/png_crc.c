#include "png_crc.h"
#define CRC_POLYNOMIAL 0xEDB88320
uint32_t png_crc(const uint8_t *buf, size_t len)
{

//     The CRC-32 algorithm can be implemented as follows:
//     Initialize a lookup table: Create a 256-entry table where each entry is computed by applying the CRC polynomial 0xEDB88320 to each possible byte value (0-255).
//  For each byte value n, iterate 8 times: if the least significant bit is set, XOR with the polynomial and shift right; otherwise just shift right.
//     Initialize CRC register: Start with 0xFFFFFFFF (all bits set).
//     Process each byte: For each byte in the input buffer, XOR the current CRC value with the byte, 
//      use the result (masked to 8 bits) as an index into the lookup table, then XOR the table value with the CRC
// shifted right by 8 bits.
//     Finalize: XOR the final CRC value with 0xFFFFFFFF to get the result.
// If this doesn't make sense to you, look closely at the provided PNG documentation where a more thorough discussion of how to implement CRC is provided.
    uint32_t lookup[256];
    for(int n = 0; n < 256; n++){
        uint32_t temp = (uint32_t) n;
        for(int i = 0; i < 8 ; i++){
            if(temp & 1){
                temp = (uint32_t)CRC_POLYNOMIAL ^ (temp >> 1);
            } else {
                temp = temp >> 1;
            }
        }
        lookup[n] = temp;
    }
    uint32_t crc = 0xFFFFFFFFL;
    for(int i = 0; i < len; i++){
        crc = lookup[(crc^buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFL;
}

