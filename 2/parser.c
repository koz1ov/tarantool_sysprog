#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>

#include "parser.h"

void execute(struct command_list *cmd_list);

static void skip_spaces(int *cur_ch)
{
    while (*cur_ch != '\n' && isspace(*cur_ch))
        *cur_ch = getchar();
}

static void skip_comment(int *cur_ch)
{
    while (*cur_ch != '\n' && *cur_ch != EOF)
        *cur_ch = getchar();
}

static char* parse_word_in_quotes(int *cur_ch)
{
    int quote_ch = *cur_ch;

    string word = {NULL, 0, 0};
    int was_backslash = 0;

    while ((*cur_ch = getchar()) != EOF) {
        if (was_backslash) {
            if (*cur_ch != quote_ch)
                push_char(&word, '\\');
            if (*cur_ch != '\\')
                push_char(&word, *cur_ch);
            was_backslash = 0;
            continue;
        }

        if (*cur_ch == quote_ch) {
            *cur_ch = getchar();
            break;
        }

        if (*cur_ch == '\\')
            was_backslash = 1;
        else
            push_char(&word, *cur_ch);
    }

    word.buf = realloc(word.buf, (word.size + 1) * sizeof(char));
    word.buf[word.size] = 0;

    return word.buf;
}

static char* parse_word(int *cur_ch)
{
    if (*cur_ch == '\'' || *cur_ch == '\"') {
        return parse_word_in_quotes(cur_ch);
    }

    string word = {NULL, 0, 0};
    int was_backslash = 0;

    do {
        if (was_backslash) {
            if (*cur_ch != '\n')
                push_char(&word, *cur_ch);
            was_backslash = 0;
            continue;
        }

        if (*cur_ch == '\\')
            was_backslash = 1;
        else if (isspace(*cur_ch) || *cur_ch == '|') {
            break;
        } else
            push_char(&word, *cur_ch);

    } while ((*cur_ch = getchar()) != EOF);

    if (!word.size)
        return NULL;

    word.buf = realloc(word.buf, (word.size + 1) * sizeof(char));
    word.buf[word.size] = 0;

    return word.buf;
}

static void parse_arg(int *ch, command *cur_cmd)
{
    char *arg = parse_word(ch);
    if (!arg)
        return;

    cur_cmd->argc++;
    cur_cmd->argv = realloc(cur_cmd->argv, cur_cmd->argc * sizeof(char*));
    handle_error(cur_cmd->argv);

    cur_cmd->argv[cur_cmd->argc - 1] = arg;
}

static void parse_file(int *ch, command *cur_cmd)
{
    int mode = O_WRONLY | O_CREAT | O_TRUNC;

    switch (*ch = getchar())
    {
        case EOF:
            return;
        case '>': // second
            mode ^= O_TRUNC;
            mode |= O_APPEND;
            *ch = getchar();
            // fallthrough
        default:
            skip_spaces(ch);
    }

    if (*ch == EOF || *ch == '\n')
        return;

    char *filename = parse_word(ch);
    if (!cur_cmd->output_file) {
        cur_cmd->output_file = calloc(1, sizeof(file));
        handle_error(cur_cmd->output_file);
    } else { // rewrite file
        free(cur_cmd->output_file->filename);
    }

    cur_cmd->output_file->filename = filename;
    cur_cmd->output_file->mode = mode;
}

void parse_input()
{
    command_list *cmd_list = calloc(sizeof(*cmd_list), 1);
    handle_error(cmd_list);
    alloc_new_cmd(cmd_list);
    int cur_ch = getchar();

    while (cur_ch != EOF) {

        skip_spaces(&cur_ch);
        switch (cur_ch) {
            case EOF:
                break;
            case '\n':
                execute(cmd_list);
                cur_ch = getchar();
                break;
            case '>':
                parse_file(&cur_ch, &cmd_list->commands[cmd_list->cmd_num - 1]);
                break;
            case '|':
                alloc_new_cmd(cmd_list);
                cur_ch = getchar();
                break;
            case '#':
                skip_comment(&cur_ch);
                break;
            default:
                parse_arg(&cur_ch, &cmd_list->commands[cmd_list->cmd_num - 1]);
                break;
        }
    }

    clear_cmd_list(cmd_list);
    free(cmd_list);
}