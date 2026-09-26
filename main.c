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

            // scan this cmds argv for < > >> and pull out the file names
            // flip each operator and filename into NULL so execvp doesnt see them
            // check >> before > so > doesnt match the first char of >>
            char* infile = NULL;
            char* outfile = NULL;
            int append_mode = 0;
            char** argv = cmd_starts[i];
            for (int j = 0; argv[j] != NULL; j++) {
                char* tok = argv[j];
                if (tok[0] == INPUT_REDIRECT && tok[1] == '\0') {
                    infile = argv[j + 1];
                    argv[j] = NULL;
                    argv[j + 1] = NULL;
                    j++;
                } 
                else if (tok[0] == OUTPUT_REDIRECT && tok[1] == OUTPUT_REDIRECT && tok[2] == '\0') {
                    outfile = argv[j + 1];
                    append_mode = 1;
                    argv[j] = NULL;
                    argv[j + 1] = NULL;
                    j++;
                } 
                else if (tok[0] == OUTPUT_REDIRECT && tok[1] == '\0') {
                    outfile = argv[j + 1];
                    append_mode = 0;
                    argv[j] = NULL;
                    argv[j + 1] = NULL;
                    j++;
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

                    // add file redirects last so it overrides the pipe dup2s
                    if (infile != NULL) {
                        int fd = open(infile, O_RDONLY);
                        if (fd == -1) {
                            perror(infile);
                            exit(1);
                        }
                        if (dup2(fd, STDIN_FILENO) == -1) {
                            perror("dup2");
                            exit(1);
                        }
                        if (close(fd) == -1) {
                            perror("close");
                            exit(1);
                        }
                    }
                    if (outfile != NULL) {
                        // this is for test 5 and was debugged with the help of claude
                        // 0666 so umask 002 gives a 664 file
                        int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
                        int fd = open(outfile, flags, 0666);
                        if (fd == -1) {
                            perror(outfile);
                            exit(1);
                        }
                        if(dup2(fd, STDOUT_FILENO) == -1){
                            perror("dup2");
                            exit(1);
                        }
                        if(close(fd) == -1){
                            perror("close");
                            exit(1);
                        }
                    }

                    execvp(argv[0], argv);
                    // if execvp returns then it failed
                    perror(argv[0]);
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


