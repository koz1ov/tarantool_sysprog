#include "userfs.h"
#include <stddef.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/param.h>

enum {
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 1024,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    int occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;
};

struct file {
    /** Double-linked list of file blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    const char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    int was_deleted;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
    struct file *file;

    struct block *block;
    int block_idx;
    int offset;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

#define check_is_descriptor_valid(fd) ({					\
    if (fd < 0 || fd >= file_descriptor_count || !file_descriptors[fd]) { \
        ufs_error_code = UFS_ERR_NO_FILE;   \
        return -1;                          \
    }				                        \
})

#define handle_error(expr) ({ \
    if (!(expr)) {                \
        fprintf(stderr, "%s\n", strerror(errno)); \
        exit(1);                              \
    }                               \
})

enum ufs_error_code
ufs_errno()
{
    return ufs_error_code;
}

static int
add_descriptor(struct file *file)
{
    int idx = 0;
    while (idx < file_descriptor_count && file_descriptors[idx])
        ++idx;

    if (idx == file_descriptor_capacity) {
        file_descriptor_capacity = file_descriptor_capacity * 2 + 1;
        file_descriptors = realloc(file_descriptors, file_descriptor_capacity * sizeof(struct filedesc*));
        handle_error(file_descriptors);
    }

    if (idx == file_descriptor_count)
        file_descriptor_count++;

    file_descriptors[idx] = calloc(1, sizeof(struct filedesc));
    handle_error(file_descriptors[idx]);

    file_descriptors[idx]->file = file;
    file->refs++;

    return idx;
}

static struct
file *find_file(const char *filename)
{
    struct file *cur_file = file_list;
    while (cur_file && strcmp(cur_file->name, filename))
        cur_file = cur_file->next;

    return cur_file;
}

static const char*
get_str_copy(const char *str)
{
    char *new_str = malloc(strlen(str) + 1);
    handle_error(new_str);

    strcpy(new_str, str);
    return new_str;
}

static
void free_file(struct file *file)
{
    struct block *block = file->block_list;
    while (block) {
        struct block *next_block = block->next;
        free(block->memory);
        free(block);
        block = next_block;
    }

    free((char*)file->name);
    free(file);
}

static void
add_block(struct file* file)
{
    struct block *new_block = calloc(1, sizeof(struct block));
    handle_error(new_block);
    new_block->memory = malloc(BLOCK_SIZE);
    handle_error(new_block->memory);

    if (!file->block_list) {
        file->block_list = file->last_block = new_block;
    } else {
        new_block->prev = file->last_block;
        file->last_block->next = new_block;
        file->last_block = new_block;
    }
}

int
ufs_open(const char *filename, int flags)
{
    struct file *cur_file = find_file(filename);

    if (!cur_file) {
        if (!(flags & UFS_CREATE)) {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }

        cur_file = calloc(1, sizeof(struct file));
        handle_error(cur_file);
        cur_file->name = get_str_copy(filename);

        if (file_list)
            file_list->prev = cur_file;
        cur_file->next = file_list;
        file_list = cur_file;
    }

    return add_descriptor(cur_file);
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    check_is_descriptor_valid(fd);
    if (!size)
        return 0;

    struct filedesc *cur_desc = file_descriptors[fd];

    if (!cur_desc->file->block_list)
        add_block(cur_desc->file);

    if (!cur_desc->block)
        cur_desc->block = cur_desc->file->block_list;

    int bytes_written = 0;

    while (size) {
        if (cur_desc->offset == BLOCK_SIZE) {
            if ((cur_desc->block_idx + 1) * BLOCK_SIZE >= MAX_FILE_SIZE) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }

            if (!cur_desc->block->next)
                add_block(cur_desc->file);

            cur_desc->block = cur_desc->block->next;
            cur_desc->block_idx++;
            cur_desc->offset = 0;
        }

        int bytes_to_write = MIN(BLOCK_SIZE - cur_desc->offset, size);

        memcpy(cur_desc->block->memory + cur_desc->offset, buf + bytes_written, bytes_to_write);

        bytes_written += bytes_to_write;
        cur_desc->offset += bytes_to_write;
        cur_desc->block->occupied = MAX(cur_desc->block->occupied, cur_desc->offset);

        size -= bytes_to_write;
    }

    return bytes_written;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    check_is_descriptor_valid(fd);

    struct filedesc *cur_desc = file_descriptors[fd];
    if (!cur_desc->file->block_list)
        return 0;

    if (!cur_desc->block)
        cur_desc->block = cur_desc->file->block_list;

    int bytes_read = 0;

    while (size)
    {
        if (cur_desc->offset == cur_desc->block->occupied) {
            if (cur_desc->offset == BLOCK_SIZE && cur_desc->block->next) {
                cur_desc->block = cur_desc->block->next;
                cur_desc->offset = 0;
            } else {
                return bytes_read;
            }
        }

        int bytes_to_read = MIN(size, cur_desc->block->occupied - cur_desc->offset);

        memcpy(buf + bytes_read, cur_desc->block->memory + cur_desc->offset, bytes_to_read);

        bytes_read += bytes_to_read;
        cur_desc->offset += bytes_to_read;

        size -= bytes_to_read;
    }

    return bytes_read;
}

int
ufs_close(int fd)
{
    check_is_descriptor_valid(fd);

    struct file *cur_file = file_descriptors[fd]->file;

    cur_file->refs--;
    if (cur_file->was_deleted && !cur_file->refs)
        free_file(cur_file);

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;
    return 0;
}

int
ufs_delete(const char *filename)
{
    struct file *file = find_file(filename);
    if (!file) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    /* remove file from linked list */
    if (file == file_list)
        file_list = file->next;
    if (file->prev)
        file->prev->next = file->next;
    if (file->next)
        file->next->prev = file->prev;

    if (!file->refs)
        free_file(file);
    else
        file->was_deleted = 1;

    return 0;
}

void
free_mem()
{
    for (int i = 0; i < file_descriptor_count; ++i)
        free(file_descriptors[i]);
    free(file_descriptors);

    struct file *file = file_list;

    while (file) {
        struct file *next_file = file->next;
        free_file(file);
        file = next_file;
    }

    free(file_list);
}
