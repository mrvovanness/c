#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint32_t CRC32_POLYNOMIAL =
    0xEDB88320U; /* значение для полимона, используемого в алгоритме CRC32 */

/* Таблица для хранения предвычисленных значений CRC32 для всех возможных байтов
   (0-255) */
static uint32_t crc32_table[256] = {0};
static void build_crc32_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;

        for (int bit = 0; bit < 8; bit++) {
            /* & 1 - маска для правого бита, например, начальный crc =
               0x00000000, при i=0, тогда crc & 1 = 0, значит мы просто сдвигаем
               вправо, а при i=1, тогда crc & 1 = 1, значит мы сдвигаем вправо и
               применяем XOR с полиномом */
            if (crc & 1) { /* помещается ли полином на текущем шаге (reversed,
                              поэтому проверяем младший бит) */
                crc = (crc >> 1) ^
                      CRC32_POLYNOMIAL; /* сдвиг вправо, вычитание делителя
                                           (XOR с полиномом) */
            } else {
                crc >>= 1; /* Иначе просто сдвигаем вправо */
            }
        }

        crc32_table[i] = crc;
    }
}

/* Считает CRC32 для буфера длины length */
static uint32_t crc32_compute(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU; /* стартовое значение по стандарту IEEE */

    for (size_t i = 0; i < length; i++) {
        uint8_t index = (uint8_t)((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ crc32_table[index];
    }

    return crc ^ 0xFFFFFFFFU; /* финальный XOR по стандарту */
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <path to file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* open file, check error and take stats - size only */
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    struct stat st = {0};
    int rc = fstat(fd, &st);
    if (rc < 0) {
        perror("Error getting file stats");
        close(fd);
        return EXIT_FAILURE;
    }
    off_t file_size = st.st_size;

    void* mm = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mm == MAP_FAILED) {
        perror("Error mapping file");
        close(fd);
        return EXIT_FAILURE;
    }

    build_crc32_table();

    uint32_t result = crc32_compute((const uint8_t*)mm, file_size);

    printf("CRC32 = 0x%08X\n", result);

    munmap(mm, file_size);
    close(fd);
    return EXIT_SUCCESS;
}
