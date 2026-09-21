#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/* Runs one command (builtin or external).
 * Returns the exit status of the command, or -1 if cmd is NULL / empty. */
int execute_command(command_t *cmd);

/* Runs a whole pipeline (one or more commands joined with |).
 * Returns 1 if the shell should exit (the "exit" builtin), else 0.
 * The exit status of the last command is saved in last_exit_status. */
int execute_pipeline(pipeline_t *pipeline);

/* Installs the SIGCHLD handler that reaps finished background jobs.
 * It is called automatically the first time a command is executed. */
void setup_background_handler(void);

extern int last_exit_status;

#endif
