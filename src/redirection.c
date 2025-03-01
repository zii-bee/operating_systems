#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "redirection.h"

// redirect input from a file
// this function opens the given file in read-only mode and then uses dup2 to replace
// the standard input with the file descriptor from the opened file.
// if anything goes wrong, it prints an error and returns -1.
int redirect_input(const char *filename) {
    int fd = open(filename, O_RDONLY); // try to open the file for reading
    if (fd < 0) {
        perror("open input file"); // report error if file opening fails
        return -1;
    }
    if (dup2(fd, STDIN_FILENO) < 0) {
        perror("dup2 input"); // report error if duplicating file descriptor fails
        return -1;
    }
    close(fd); // close the original file descriptor as it's no longer needed
    return 0; // indicate success
}

// redirect output to a file
// this function opens or creates the specified file in write-only mode (truncating it if it exists),
// then uses dup2 to redirect standard output to that file.
// errors are reported and -1 is returned if any operation fails.
int redirect_output(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); // open or create the file for writing
    if (fd < 0) {
        perror("open output file"); // report error if file opening fails
        return -1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("dup2 output"); // report error if duplicating file descriptor fails
        return -1;
    }
    close(fd); // close the original file descriptor
    return 0; // indicate success
}

// redirect error output to a file
// this function works similarly to redirect_output but targets standard error.
// it opens or creates the specified file, then redirects standard error to that file.
int redirect_error(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); // open or create the error file
    if (fd < 0) {
        perror("open error file"); // report error if file opening fails
        return -1;
    }
    if (dup2(fd, STDERR_FILENO) < 0) {
        perror("dup2 error"); // report error if duplicating file descriptor fails
        return -1;
    }
    close(fd); // close the original file descriptor
    return 0; // indicate success
}
