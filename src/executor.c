#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "executor.h"
#include "builtin.h"

static int setup_redirection(const command_t *cmd) {
    if (cmd->input[0] != '\0') {
        int fd = open(cmd->input, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input);
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->output[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output, flags, 0644);
        if (fd < 0) {
            perror(cmd->output);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

static int run_builtin(command_t *cmd) {
    int saved_in  = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    int result = 0;

    fflush(stdout);
    if (setup_redirection(cmd) == 0) {
        result = execute_builtin(cmd);
    }

    fflush(stdout);
    dup2(saved_in, STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    return result;
}

static void run_external(command_t *cmd) {
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
    } else if (pid == 0) {
        if (setup_redirection(cmd) != 0) {
            exit(1);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        exit(127);
    } else {
        if (cmd->background) {
            printf("[background] pid %d\n", pid);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

int execute_pipeline(pipeline_t *pipeline) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }

    if (pipeline->command_count > 1) {
        printf("shellforge: pipes are not supported yet\n");
        return 0;
    }

    command_t *cmd = &pipeline->commands[0];

    if (cmd->argc == 0) {
        return 0;
    }

    if (is_builtin(cmd->argv[0])) {
        return (run_builtin(cmd) == 1) ? 1 : 0;
    }

    run_external(cmd);
    return 0;
}
