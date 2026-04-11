#include <stdint.h>
#include <stdio.h>

static const uint32_t CRC32_POLYNOMIAL = 0xEDB88320U;// значение для полимона, используемого в алгоритме CRC32

// Таблица для хранения предвычисленных значений CRC32 для всех возможных байтов (0-255)
static uint32_t crc32_table[256] = {0};
static void build_crc32_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;

        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) { // Если младший бит установлен, применяем полином

                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1; // Иначе просто сдвигаем вправо
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
