#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "redirection.h"

int redirect_input(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open input file");
        return -1;
    }
    if (dup2(fd, STDIN_FILENO) < 0) {
        perror("dup2 input");
        return -1;
    }
    close(fd);
    return 0;
}

int redirect_output(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open output file");
        return -1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("dup2 output");
        return -1;
    }
    close(fd);
    return 0;
}

int redirect_error(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open error file");
        return -1;
    }
    if (dup2(fd, STDERR_FILENO) < 0) {
        perror("dup2 error");
        return -1;
    }
    close(fd);
    return 0;
}
