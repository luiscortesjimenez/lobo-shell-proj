#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "constants.h"
#include "parsetools.h"


int main() {

    // Buffer for reading one line of input
    char line[MAX_LINE_CHARS];
    // holds separated words based on whitespace
    char* line_words[MAX_LINE_WORDS + 1];
    // True when stdin is connected to a terminal
    int interactive = isatty(STDIN_FILENO);

    // Loop until user ends or theres an error
    while (1) {
        if (interactive) {
            printf("lobo> ");
            fflush(stdout);
        }
        if (fgets(line, MAX_LINE_CHARS, stdin) == NULL) {
            break;
        }

        int num_words = split_cmd_line(line, line_words);

        // for blank lines do nothing
        if (num_words == 0) {
            continue;
        }

        // splits commands based off pipes into a list of sub commands
        // each cmd_starts[i] points at the first word of sub command i
        // flips every | to null so each sub command is null terminated for execvp
        char** cmd_starts[MAX_LINE_WORDS];
        int num_cmds = 0;
        cmd_starts[num_cmds++] = &line_words[0];
        for (int i = 0; i < num_words; i++) {
            if (line_words[i][0] == PIPE && line_words[i][1] == '\0') {
                line_words[i] = NULL;
                cmd_starts[num_cmds++] = &line_words[i + 1];
            }
        }

        // walk sub commands and fork each one
        // prev_read is the read end of the pipe linked to current ccommand stdin
        // its -1 when theres no incoming pipe which is the first cmd
        int prev_read = -1;
        int pipe_fds[2];
        pid_t pid;
        for (int i = 0; i < num_cmds; i++) {
            // make a new pipe unless this is the last cmd
            if (i < num_cmds - 1) {
                if (pipe(pipe_fds) == -1) {
                    perror("pipe");
                    exit(1);
                }
            }

            switch (pid = fork()) {
                case -1:
                    perror("fork");
                    exit(1);
                    break;
                case 0:
                    // for child process wire up the pipes then exec
                    if (prev_read != -1) {
                        // read stdin from the previous pipe
                        if (dup2(prev_read, STDIN_FILENO) == -1) {
                            perror("dup2");
                            exit(1);
                        }
                        if (close(prev_read) == -1) {
                            perror("close");
                            exit(1);
                        }
                    }
                    if (i < num_cmds - 1) {
                        // send stdout into the current pipe
                        if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
                            perror("dup2");
                            exit(1);
                        }
                        if (close(pipe_fds[0]) == -1 || close(pipe_fds[1]) == -1) {
                            perror("close");
                            exit(1);
                        }
                    }
                    execvp(cmd_starts[i][0], cmd_starts[i]);
                    // if execvp returns tgen it failed
                    perror(cmd_starts[i][0]);
                    exit(1);
                    break;
                default:
                    // for parent process close any pipe ends we no longer need
                    if (prev_read != -1) {
                        if (close(prev_read) == -1) {
                            perror("close");
                            exit(1);
                        }
                        prev_read = -1;
                    }
                    if (i < num_cmds - 1) {
                        // hand off the read end for the next cmd and drop the write end
                        if (close(pipe_fds[1]) == -1) {
                            perror("close");
                            exit(1);
                        }
                        prev_read = pipe_fds[0];
                    }
                    break;
            }
        }

        // reap every child we just made
        while (wait(NULL) != -1) {
            ;
        }
    }

    return 0;
}


