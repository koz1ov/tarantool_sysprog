#include <stdlib.h>
#include <string.h>

#include "parser.h"

void push_char(string *str, char ch)
{
    if (str->size >= str->capacity) {
        str->capacity = str->capacity * 2 + 1;
        str->buf = realloc(str->buf, str->capacity * sizeof(char));
        handle_error(str->buf);
    }

    str->buf[str->size++] = ch;
}

void alloc_new_cmd(command_list *cmd_list)
{
    cmd_list->cmd_num++;
    cmd_list->commands = realloc(cmd_list->commands, cmd_list->cmd_num * sizeof(command));
    handle_error(cmd_list->commands);

    memset(&cmd_list->commands[cmd_list->cmd_num - 1], 0, sizeof(command));
}

void clear_cmd_list(command_list *cmd_list)
{
    for (int i = 0; i < cmd_list->cmd_num; ++i) {
        command *cur_cmd = &cmd_list->commands[i];
        for (int j = 0; j < cur_cmd->argc; ++j)
            free(cur_cmd->argv[j]);

        if (cur_cmd->output_file) {
            free(cur_cmd->output_file->filename);
            free(cur_cmd->output_file);
        }

        free(cur_cmd->argv);
    }

    free(cmd_list->commands);
    memset(cmd_list, 0, sizeof(*cmd_list));
}