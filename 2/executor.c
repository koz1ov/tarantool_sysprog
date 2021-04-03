#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "parser.h"

static int proc_builtins(command_list *cmd_list) {
    if (cmd_list->cmd_num > 1)
        return 0; // not builtin command

    command *first_cmd = &cmd_list->commands[0];
    if (!strcmp("exit", first_cmd->argv[0]))
        exit(0);

    if (!strcmp("cd", first_cmd->argv[0])) {

        int chdir_res;
        if (first_cmd->argc == 1)
            chdir_res = chdir(getenv("HOME"));
        else
            chdir_res = chdir(first_cmd->argv[1]);

        if (chdir_res < 0) {
            fprintf(stderr, "%s\n", strerror(errno));
        }

        clear_cmd_list(cmd_list);
        alloc_new_cmd(cmd_list);
        return 1;
    }

    return 0;
}

void execute(struct command_list *cmd_list)
{
    if (!cmd_list->commands[0].argc)
        return;

    // check if it is built in command
    if (proc_builtins(cmd_list))
        return;

    int (*pipefd)[2] = calloc(cmd_list->cmd_num - 1, sizeof(int[2]));
    handle_error(pipefd);

    for (int i = 0; i < cmd_list->cmd_num; ++i) {
        if (i != cmd_list->cmd_num - 1)
            handle_error(pipe(pipefd[i]) == 0);

        command *cur_cmd = &cmd_list->commands[i];

        pid_t pid = fork();
        handle_error(pid >= 0);
        if (!pid) {
            if (i > 0) {
                handle_error(dup2(pipefd[i - 1][0], 0) != -1);
                handle_error(close(pipefd[i - 1][0]) == 0);
            }
            if (i < cmd_list->cmd_num - 1) {
                handle_error(dup2(pipefd[i][1], 1) != -1);
                handle_error(close(pipefd[i][1]) == 0);
            }

            if (cur_cmd->output_file) {
                int file_fd = open(cur_cmd->output_file->filename, cur_cmd->output_file->mode, 0666);
                handle_error(file_fd >= 0);

                handle_error(dup2(file_fd, 1) != -1);
                handle_error(close(file_fd) == 0);
            }

            cur_cmd->argv = realloc(cur_cmd->argv, (cur_cmd->argc + 1) * sizeof(char *));
            handle_error(cur_cmd->argv);

            cur_cmd->argv[cur_cmd->argc] = NULL;

            execvp(cur_cmd->argv[0], cur_cmd->argv);
            exit(1);
        }

        if (i != cmd_list->cmd_num - 1)
            close(pipefd[i][1]);
        if (i != 0)
            close(pipefd[i-1][0]);
    }

    free(pipefd);

    for (int i = 0; i < cmd_list->cmd_num; ++i)
        wait(NULL);

    clear_cmd_list(cmd_list);
    alloc_new_cmd(cmd_list);
}
