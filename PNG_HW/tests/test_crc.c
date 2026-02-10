#include <criterion/criterion.h>
#include "png_crc.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

Test(crc, known_crc_ihdr) {
    const char *s = "IHDR";
    uint32_t crc = png_crc((const uint8_t *)s, strlen(s)); //3314227430

    /* Verified CRC from PNG spec  */ 
                // C58B18E6
    cr_assert_eq(crc, 0xA8A1AE0A); //77650228.  4034014118
}

Test(crc, crc_changes_on_input_change) {
    const char *a = "IHDR";
    const char *b = "IHDS";

    cr_assert_neq(
        png_crc((const uint8_t *)a, 4),
        png_crc((const uint8_t *)b, 4),
        "CRC should change when input changes"
    );
}

Test(crc, crc_empty_string) {
    uint32_t crc = png_crc((const uint8_t *)"", 0);
    /* CRC of empty string should be consistent */
    cr_assert_eq(crc, png_crc((const uint8_t *)"", 0), "Empty string CRC should be consistent");
}
