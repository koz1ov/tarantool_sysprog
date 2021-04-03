#ifndef SHELL_PARSER_H
#define SHELL_PARSER_H

#include <errno.h>
#include <stdio.h>

typedef struct file
{
    char *filename;
    int mode;
} file;

typedef struct command
{
    char **argv;
    int argc;
    file *output_file;
} command;

typedef struct command_list
{
    command *commands;
    int cmd_num;
} command_list;

typedef struct string
{
    char *buf;
    int size;
    int capacity;
} string;

#define handle_error(expr) do { \
    if (!(expr)) {                \
        fprintf(stderr, "%s\n", strerror(errno)); \
        exit(1);                              \
    }                               \
} while (0)

void push_char(string *str, char ch);
void alloc_new_cmd(command_list *cmd_list);
void clear_cmd_list(command_list *cmd_list);

#endif

