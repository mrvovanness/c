#include <stdint.h>
#include <stdio.h>

#define CRC32_POLYNOMIAL 0xEDB88320U

static uint32_t crc32_table[256];

static void build_crc32_table(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;

        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }

        crc32_table[i] = crc;
    }
}

int main(void) {
    build_crc32_table();

    // Печатаем первые 8 записей — можно сверить с эталоном
    for (int i = 0; i < 8; i++) {
        printf("table[%d] = 0x%08X\n", i, crc32_table[i]);
    }

    return 0;
}
