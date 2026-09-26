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

    // Loop until user hits Ctrl-D (end of input)
    // or some other input error occurs
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

        pid_t pid;
        switch (pid = fork()) {
            case -1:
                perror("fork");
                exit(1);
                break;
            case 0:
                // for child process replace with the requested program
                execvp(line_words[0], line_words);
                // if execvp returns tgen it failed
                perror(line_words[0]);
                exit(1);
                break;
            default:
                // for parent process wait for the child to finish
                if (wait(NULL) == -1) {
                    perror("wait");
                    exit(1);
                }
                break;
        }
    }

    return 0;
}


