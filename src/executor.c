#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "executor.h"
#include "builtin.h"

int last_exit_status = 0;

/* ---------- helpers ---------- */

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

static int status_of(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

/* Builtin in the shell itself (no fork): apply redirection, run, restore. */
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

/* ---------- single command ---------- */

int execute_command(command_t *cmd) {
    if (cmd == NULL || cmd->argc == 0) {
        return -1;
    }

    if (is_builtin(cmd->argv[0])) {
        return run_builtin(cmd);
    }

    fflush(stdout);
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* child */
        if (setup_redirection(cmd) != 0) {
            _exit(1);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        _exit(127);
    }

    /* parent */
    if (cmd->background) {
        printf("[background] pid %d\n", pid);
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    return status_of(status);
}

/* ---------- pipeline ---------- */

int execute_pipeline(pipeline_t *pipeline) {
    /* clean up finished background children */
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }

    if (pipeline == NULL || pipeline->command_count <= 0) {
        return 0;
    }

    int n = pipeline->command_count;

    /* only one command */
    if (n == 1) {
        command_t *cmd = &pipeline->commands[0];

        if (cmd->argc > 0 && is_builtin(cmd->argv[0])) {
            int r = run_builtin(cmd);
            last_exit_status = 0;
            return (r == 1) ? 1 : 0;
        }

        int r = execute_command(cmd);
        last_exit_status = (r < 0) ? 1 : r;
        return 0;
    }

    /* every command in a pipeline must have a name (e.g. "| wc" is an error) */
    for (int i = 0; i < n; i++) {
        if (pipeline->commands[i].argc == 0) {
            printf("shellforge: syntax error near '|'\n");
            last_exit_status = 1;
            return 0;
        }
    }

    pid_t pids[MAX_COMMANDS];
    int forked = 0;
    int prev_read = -1;
    int pipefd[2];

    fflush(stdout);

    for (int i = 0; i < n; i++) {
        command_t *cmd = &pipeline->commands[i];
        pipefd[0] = -1;
        pipefd[1] = -1;

        /* pipe for every command except the last */
        if (i < n - 1 && pipe(pipefd) < 0) {
            perror("pipe");
            break;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            break;
        }

        if (pid == 0) {
            /* ---- child ---- */
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (i < n - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            /* < and > given by the user override the pipe */
            if (setup_redirection(cmd) != 0) {
                _exit(1);
            }

            if (is_builtin(cmd->argv[0])) {
                execute_builtin(cmd);
                fflush(stdout);
                _exit(0);
            }

            execvp(cmd->argv[0], cmd->argv);
            perror(cmd->argv[0]);
            _exit(127);
        }

        /* ---- parent ---- */
        pids[forked++] = pid;

        if (prev_read != -1) {
            close(prev_read);
        }
        if (i < n - 1) {
            close(pipefd[1]);
            prev_read = pipefd[0];
        } else {
            prev_read = -1;
        }
    }

    if (prev_read != -1) {
        close(prev_read);
    }

    /* background pipeline: do not wait */
    if (pipeline->commands[n - 1].background && forked == n) {
        printf("[background] pid %d\n", pids[n - 1]);
        last_exit_status = 0;
        return 0;
    }

    /* wait for all children, keep the status of the last command */
    int last_status = 1;
    for (int i = 0; i < forked; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == n - 1) {
            last_status = status_of(status);
        }
    }

    last_exit_status = last_status;
    return 0;
}
