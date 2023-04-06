//
// Created by root on 1/9/23.
//

#include "file_reader.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "tested_declarations.h"
#include "rdebug.h"
#include "tested_declarations.h"
#include "rdebug.h"


struct disk_t *disk_open_from_file(const char *volume_file_name) {
    if (volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct disk_t *disk = calloc(1, sizeof(struct disk_t));
    if (disk == NULL) {
        return NULL;
    }

    disk->f = fopen(volume_file_name, "rb");
    if (disk->f == NULL) {
        free(disk);
        return NULL;
    }

    return disk;
}

int disk_close(struct disk_t *pdisk) {

    if (pdisk == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (pdisk->f == NULL) {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->f);
    free(pdisk);

    return 0;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {

    if (pdisk == NULL || buffer == NULL || sectors_to_read <= 0) {
        errno = EFAULT;
        return -1;
    }
    if (pdisk->f == NULL) {
        errno = EFAULT;
        return -1;
    }

    uint8_t *temp = (uint8_t *) buffer;

    if (first_sector != -1) {
        fseek(pdisk->f, first_sector, SEEK_SET);
    }

    int32_t result = (int32_t) fread(temp, 512, sectors_to_read, pdisk->f);
    if (result != sectors_to_read) {
        errno = ERANGE;
        return -1;
    }

    return sectors_to_read;
}

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {

    if (pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }
    if (pdisk->f == NULL) {
        errno = EFAULT;
        return NULL;
    }

    int error;

    struct volume_t *result = calloc(1, sizeof(struct volume_t));
    if (result == NULL) {
        return NULL;
    }
    result->boot_sector = calloc(512, sizeof(char));
    if (result->boot_sector == NULL) {
        free(result);
        return NULL;
    }

    error = disk_read(pdisk, (int32_t) first_sector * 512, result->boot_sector, 1);
    if (error != 1) {
        free(result->boot_sector);
        free(result);
        return NULL;
    }

    if (result->boot_sector->signature != 0xaa55) {
        errno = EINVAL;
        free(result->boot_sector);
        free(result);
        return NULL;
    }

    result->fat1 = calloc(result->boot_sector->bytes_per_sector * result->boot_sector->size_of_fat, sizeof(char));
    if (result->fat1 == NULL) {
        free(result->boot_sector);
        free(result);
        return NULL;
    }
    result->fat2 = calloc(result->boot_sector->bytes_per_sector * result->boot_sector->size_of_fat, sizeof(char));
    if (result->fat2 == NULL) {
        free(result->boot_sector);
        free(result->fat1);
        free(result);
        return NULL;
    }

    error = disk_read(pdisk, result->boot_sector->size_of_reserved_area * result->boot_sector->bytes_per_sector,
                      result->fat1,
                      result->boot_sector->size_of_fat);
    if (error != result->boot_sector->size_of_fat) {
        free(result->boot_sector);
        free(result->fat1);
        free(result->fat2);
        free(result);
        return NULL;
    }
    error = disk_read(pdisk, -1, result->fat2, result->boot_sector->size_of_fat);
    if (error != result->boot_sector->size_of_fat) {
        free(result->boot_sector);
        free(result->fat1);
        free(result->fat2);
        free(result);
        return NULL;
    }

    if (memcmp(result->fat1, result->fat2, result->boot_sector->size_of_fat * result->boot_sector->bytes_per_sector) !=
        0) {
        free(result->boot_sector);
        free(result->fat1);
        free(result->fat2);
        free(result);
        errno = EINVAL;
        return NULL;
    }

    result->root = calloc(result->boot_sector->maximum_number_of_files, sizeof(struct SFN));
    if (result->root == NULL) {
        free(result->boot_sector);
        free(result->fat1);
        free(result->fat2);
        free(result);
        return NULL;
    }

    error = disk_read(pdisk, -1, result->root, result->boot_sector->maximum_number_of_files * 32 / 512);

    if (error != result->boot_sector->maximum_number_of_files * 32 / 512) {
        free(result->boot_sector);
        free(result->fat1);
        free(result->fat2);
        free(result);
        return NULL;
    }

    result->disk = pdisk;

    return result;
}

int fat_close(struct volume_t *pvolume) {
    if (pvolume == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (pvolume->boot_sector != NULL)
        free(pvolume->boot_sector);
    if (pvolume->fat1 != NULL)
        free(pvolume->fat1);
    if (pvolume->fat2 != NULL)
        free(pvolume->fat2);
    if (pvolume->root != NULL)
        free(pvolume->root);

    free(pvolume);

    return 0;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if (pvolume == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    int file_pos = find_file(pvolume, file_name);
    if (file_pos == -1) {
        errno = ENOENT;
        return NULL;
    }

    struct file_t *result = calloc(1, sizeof(struct file_t));
    if (result == NULL) {
        return NULL;
    }

    copy_file(&result->file, &pvolume->root[file_pos]);

    if ((result->file.file_attributes & 0x10) == 0x10) {
        errno = EISDIR;
        free(result);
        return NULL;
    }

    result->pos = 0;
    result->bytes_per_sector = pvolume->boot_sector->bytes_per_sector;
    result->sectors_per_clusters = pvolume->boot_sector->sectors_per_clusters;
    result->volume = pvolume;

    return result;
}

void copy_file(struct SFN *dest, const struct SFN *src) {

    strcpy(dest->filename, src->filename);
    dest->file_attributes = src->file_attributes;
    dest->reserved = src->reserved;
    dest->file_creation_time = src->file_creation_time;
    dest->creation_time = src->creation_time;
    dest->creation_date = src->creation_date;
    dest->access_date = src->access_date;
    dest->high_order_address_of_first_cluster = src->high_order_address_of_first_cluster;
    dest->modified_time = src->modified_time;
    dest->modified_date = src->modified_date;
    dest->low_order_address_of_first_cluster = src->low_order_address_of_first_cluster;
    dest->size = src->size;

    return;
}

int find_file(const struct volume_t *files, const char *filename) {

    for (int i = 0; i < files->boot_sector->maximum_number_of_files; ++i) {
        if (strncmp(files->root[i].filename, filename, find_dot_pos(filename)) == 0) {
            return i;
        }
    }

    return -1;
}

int find_dot_pos(const char *name) {

    int pos = 0;

    for (; name[pos] != '\0'; ++pos) {
        if (name[pos] == '.') {
            break;
        }
    }

    return pos;
}

int file_close(struct file_t *stream) {

    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(stream);

    return 0;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {

    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }


    switch (whence) {
        case SEEK_SET: {
            if (offset < 0 || (uint32_t) (0 + offset) > stream->file.size) {
                errno = EINVAL;
                return -1;
            }
            stream->pos = offset;

        }
            break;
        case SEEK_END: {
            if (offset > 0 || (int32_t) (stream->file.size + offset) < 0) {
                errno = EINVAL;
                return -1;
            }
            stream->pos = stream->file.size + offset;
        }
            break;
        case SEEK_CUR: {
            if ((int32_t) (stream->pos + offset) < 0 || stream->pos + offset > stream->file.size) {
                errno = EINVAL;
                return -1;
            }
            stream->pos = stream->pos + offset;
        }
            break;
        default:
            break;
    }

    return (int32_t) stream->pos;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {

    if (ptr == NULL || size <= 0 || nmemb <= 0 || stream == NULL) {
        errno = EFAULT;
        return -1;
    }
    size_t total = size * nmemb;
    size_t to_return = nmemb;
    if (total > stream->file.size) {
        total = stream->file.size;
    }

    if (total + stream->pos > stream->file.size) {
        total = stream->file.size - (stream->pos);
        to_return = 0;
    }

    // size_t to_return = total;
    size_t read = 0;
    int error;
    char *buffer = calloc(stream->sectors_per_clusters * 512, sizeof(char));
    uint32_t address;

    uint16_t data_start =
            stream->volume->boot_sector->size_of_reserved_area + //boot
            2 * stream->volume->boot_sector->size_of_fat + //FATs
            stream->volume->boot_sector->maximum_number_of_files * sizeof(struct SFN) / 512;//root

    uint16_t fat_sector = stream->file.low_order_address_of_first_cluster;

    while (total) {

        address = (data_start + (fat_sector - 2) * stream->volume->boot_sector->sectors_per_clusters);

        error = disk_read(stream->volume->disk, address * 512, buffer, stream->sectors_per_clusters);
        if (error != stream->sectors_per_clusters) {
            free(buffer);
            return -1;
        }

        read += (512 * stream->sectors_per_clusters);

        if (total >= stream->bytes_per_sector * stream->sectors_per_clusters && stream->pos <= read) {

            add_string(&stream->pos, stream->file.size, stream->bytes_per_sector * stream->sectors_per_clusters,
                       ptr,
                       buffer, stream->sectors_per_clusters);
            total -= stream->bytes_per_sector * stream->sectors_per_clusters;
            ptr = (void *) ((char *) ptr + 512 * stream->sectors_per_clusters);

        } else if (stream->pos < read) {

            size_t emergency_temp = total;
            int rest = add_string(&stream->pos, stream->file.size, total, ptr, buffer,
                                  stream->sectors_per_clusters);
            if (rest) {
                total = rest;
            } else {
                total -= total;
            }
            ptr = (void *) ((char *) ptr + emergency_temp - rest);

        }

        if (*((uint16_t *) (stream->volume->fat1) + fat_sector) >= 0xFFF8) {
            break;
        }

        fat_sector = *((uint16_t *) (stream->volume->fat1) + fat_sector);
    }

    free(buffer);
    if (size * nmemb > stream->file.size) {
        return stream->file.size;
    }
    return to_return;
}

int
add_string(uint32_t *position, size_t dest_size, size_t size, void *dest, const char *src,
           size_t sector_per_cluster) {

    if (*position + size > dest_size) {
        return -1;
    }

    size_t offset = *position % (512 * sector_per_cluster);
    size_t rest = 0;

    if (offset + size > 512 * sector_per_cluster) {
        rest = size - (512 * sector_per_cluster - offset);
        size = 512 * sector_per_cluster - offset;
    }

    for (size_t i = 0; i < size; ++i) {
        *(((char *) (dest)) + i) = src[i + offset];
        ++(*position);
        //printf("%d:%c ",(int)i,src[i+offset]);
    }
    //   printf("\n\n");
    return rest;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
    if (pvolume == NULL || dir_path == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct dir_t *result = calloc(1, sizeof(struct dir_t));
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (strcmp(dir_path, "\\") == 0) {

        result->volume = pvolume;
        result->file_count = pvolume->boot_sector->maximum_number_of_files;
        result->files = calloc(result->file_count, sizeof(struct dir_entry_t));
        if (result->files == NULL) {
            free(result);
            errno = ENOMEM;
            return NULL;
        }
        result->volume = pvolume;
        result->pos = 0;
        /*
        for (size_t i = 0; i < result->file_count; ++i) {
            result->files[i].name = generate_name(&pvolume->root[i]);
            if (result->files[i].name == NULL) {
                for (size_t y = 0; y <= i; ++y) {
                    free(result->files[i].name);
                }
                free(result->files);
                free(result);
                errno = ENOMEM;
                return NULL;
            }

        }
        */
    } else {
        free(result);
        return NULL;
    }

    return result;
}

int generate_name(const struct SFN *file, char *dest) {
    if (file == NULL || dest == NULL) {
        return -1;
    }

    if ((unsigned char) (*file->filename) == 0xe5) {
        return 0;
    }

    if(is_name_empty(file->filename)){
        return 0;
    }

    memset(dest, '\0', 13);
/*
    int is_dir = 4;

    if ((file->file_attributes & 0x10) >> 4) {
        is_dir = 0;
    }

    size_t end_of_filename = 0;
    for (; file->filename[end_of_filename] != ' ' && end_of_filename < 11; ++end_of_filename);

    if (is_dir != 0 && end_of_filename >= 8) {
        end_of_filename -= 3;
    }
    size_t i = 0;
    for (; i < end_of_filename; ++i) {
        dest[i] = file->filename[i];
    }
    if (is_dir != 0) {
        dest[i] = '.';
        ++i;
        for (size_t y = 8; y < 11; ++y, ++i) {
            dest[i] = file->filename[y];
        }
    }
*/
    int pos = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        if (file->filename[i] == ' ')
            break;
        dest[pos] = file->filename[i];
    }

    if (file->filename[8] == ' ') {
        return 1;
    }
    dest[pos] = '.';
    ++pos;
    for (int i = 8; i < 11; ++i, ++pos) {
        if (file->filename[i] == ' ')
            break;
        dest[pos] = file->filename[i];
    }

    return 1;
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }
    if(pdir->pos >= pdir->file_count){
        return 1;
    }
    size_t current_pos = pdir->pos;
    struct SFN *temp = &pdir->volume->root[current_pos];

    while (1) {
        if(pdir->pos >= pdir->file_count){
            return 1;
        }
        if (generate_name(temp, pentry->name) == 0) {
            ++(pdir->pos);
            ++current_pos;
            temp = &pdir->volume->root[current_pos];
            continue;
        }
        pentry->size = temp->size;
        pentry->volume = pdir->volume;
        pentry->is_archived = (temp->file_attributes & 0x20) >> 5;
        pentry->is_readonly = temp->file_attributes & 0x01;
        pentry->is_system = (temp->file_attributes & 0x04) >> 2;
        pentry->is_directory = (temp->file_attributes & 0x10) >> 4;
        pentry->is_hidden = (temp->file_attributes & 0x02) >> 1;

        ++(pdir->pos);
        break;
    }

    return 0;
}

int dir_close(struct dir_t *pdir) {
    if (pdir == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pdir->files);

    free(pdir);
    return 0;
}

struct clusters_chain_t *get_chain_fat16(const void *const buffer, size_t size, uint16_t first_cluster) {
    if (buffer == NULL || size <= 0 || first_cluster <= 0) {
        return NULL;
    }

    struct clusters_chain_t *result = malloc(sizeof(struct clusters_chain_t));
    if (result == NULL) {
        return NULL;
    }
    result->clusters = malloc(sizeof(uint16_t));
    if (result->clusters == NULL) {
        return NULL;
    }
    result->size = 1;
    result->clusters[0] = first_cluster;

    uint16_t *fatTable = (uint16_t *) buffer;

    uint16_t temp_val = first_cluster;

    while (1) {

        if (fatTable[temp_val] >= 0xFFF8) {
            break;
        }
        temp_val = fatTable[temp_val];

        result->size++;
        uint16_t *temp = realloc(result->clusters, sizeof(uint16_t) * result->size);
        if (temp == NULL) {
            free(result->clusters);
            free(result);
            return NULL;
        }
        result->clusters = temp;
        result->clusters[result->size - 1] = temp_val;

    }

    return result;
}

int is_name_empty(const char *name) {

    if (name[0] == '\0' || name[1] == '\0') {
        return 1;
    }

    return 0;
}
