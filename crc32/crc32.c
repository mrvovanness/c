#include <stdint.h>
#include <stdio.h>

#define CRC32_POLYNOMIAL 0xEDB88320U

static unsigned int crc32_table[256] = {0};

static void build_crc32_table(void) {
    for (int i = 0; i < 256; i++) {
        int crc = i;

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
