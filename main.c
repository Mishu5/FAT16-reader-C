#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "file_reader.h"
#include "tested_declarations.h"
#include "rdebug.h"


int main() {
    FILE *file = fopen("../fat12_c1.bin", "rb");
    if (file == NULL) {
        return -1;
    }
    struct FAT16 *boot_sector = calloc(512, sizeof(char));
    fread(boot_sector, 512, 1, file);
    char *fat_1 = calloc(boot_sector->bytes_per_sector * boot_sector->size_of_fat, sizeof(char));
    char *fat_2 = calloc(boot_sector->bytes_per_sector * boot_sector->size_of_fat, sizeof(char));
    fseek(file, boot_sector->size_of_reserved_area * boot_sector->bytes_per_sector, SEEK_SET);
    fread(fat_1, boot_sector->size_of_fat * boot_sector->bytes_per_sector, 1, file);
    fread(fat_2, boot_sector->size_of_fat * boot_sector->bytes_per_sector, 1, file);
    if (memcmp(fat_1, fat_2, boot_sector->size_of_fat * boot_sector->bytes_per_sector) != 0) {
        printf("error");
        return -2;
    }
    struct SFN *root = calloc(boot_sector->maximum_number_of_files, sizeof(struct SFN));
    printf("%d\n", 2 * boot_sector->size_of_fat + boot_sector->size_of_reserved_area);
    printf("%lu\n", sizeof(struct SFN));
    fread(root, 2 * boot_sector->size_of_fat + boot_sector->size_of_reserved_area * boot_sector->bytes_per_sector, 1,
          file);

    for (int i = 0; i < boot_sector->maximum_number_of_files; ++i) {
        if ((unsigned char) *((root + i)->filename) == 0)
            continue;
        for (int j = 0; j < 8; ++j) {
            printf("%c", *((root + i)->filename + j));
        }
        printf(".");
        for (int j = 8; j < 11; ++j) {
            printf("%c", *((root + i)->filename + j));
        }
        if ((unsigned char) *((root + i)->filename) == 0xe5) {
            printf(" deleted");
        }
        printf("\n");
    }
    fclose(file);
    free(root);
    free(fat_1);
    free(fat_2);
    free(boot_sector);
    return 0;
}

