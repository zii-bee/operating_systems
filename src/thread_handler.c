#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "parser.h"
#include "executor.h"
#include "pipes.h"
#include "thread_handler.h"

#define MAX_INPUT_SIZE 1024
#define MAX_OUTPUT_SIZE 4096

// this function handles the execution of commands from clients, captures any output,
// and sends the results back to the client who requested it
void execute_shell_command(int client_socket, const char *input, char *client_ip, int client_port, int client_id) {
    // let's log the incoming command so we can track what clients are doing
    printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n", 
           client_id, client_ip, client_port, input);
    
    // log that we're about to execute this command
    printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n", 
           client_id, client_ip, client_port, input);
    
    // special handling for cd command since it's a bit unique - we need to avoid
    // extra newlines in the client output and handle it differently than other commands
    if (strncmp(input, "cd", 2) == 0 && (input[2] == ' ' || input[2] == '\0')) {
        // parse the command string into our command structure
        Command *cmd = parse_command(input);
        if (cmd) {
            // actually change directories - this modifies the current working directory
            // of the thread handling this client
            execute_command(cmd);
            free_command(cmd);
            
            // log what we're sending back (nothing in this case)
            printf("[OUTPUT] [Client #%d - %s:%d] Sending empty response (cd command)\n\n",
                  client_id, client_ip, client_port);
            
            // for cd, we send an empty string - not even a newline
            // this prevents extra line breaks in the client terminal
            send(client_socket, "", 0, 0);
            return;
        }
    }
    
    // for all other commands, we need to capture their output to send back to the client
    
    // first save the current stdout and stderr so we can restore them later
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    
    // create a pipe to capture the command's output - this lets us
    // redirect stdout/stderr to a pipe we can read from
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }
    
    // redirect stdout and stderr to our pipe's write end so we can capture all output
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);  // close the write end in this process since we dup'd it
    
    // now we can execute the command and its output will go to our pipe
    if (strchr(input, '|')) {
        // if there's a pipe character, this is a pipeline of commands
        if (strstr(input, "||") == NULL) {
            // valid pipeline, so execute it
            execute_pipeline(input);
        } else {
            // there's an empty command in the pipeline (like "ls | | wc")
            fprintf(stderr, "Error: Empty command between pipes.\n");
        }
    } else {
        // this is a single command, so parse and execute it
        Command *cmd = parse_command(input);
        if (cmd) {
            execute_command(cmd);
            free_command(cmd);
        } else {
            fprintf(stderr, "Parsing error.\n");
        }
    }
    
    // make sure all output is flushed to our pipe
    fflush(stdout);
    fflush(stderr);
    
    // restore the original stdout and stderr
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);
    
    // read what was captured in our pipe - this is the command's output
    char buffer[MAX_OUTPUT_SIZE];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);  // we're done with the pipe
    
    // if we got some output, process and send it to the client
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';  // null-terminate for string operations
        
        // check if what we got contains error messages - this helps us format the output
        // appropriately for the client and server logs
        int is_error = (strstr(buffer, "Error:") != NULL || 
                        strstr(buffer, "not found") != NULL ||
                        strstr(buffer, ": missing operand") != NULL ||
                        strstr(buffer, "Parsing error") != NULL);
        
        if (is_error) {
            // we got an error message, so log it accordingly
            printf("[ERROR] [Client #%d - %s:%d] %s", 
                  client_id, client_ip, client_port, buffer);
                  
            // log what we're sending back to the client
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n\"%s\"\n\n", 
                  client_id, client_ip, client_port, buffer);
                  
            // send the error message to the client
            send(client_socket, buffer, bytes_read, 0);
        } 
        else if (strcmp(input, "ls") == 0) {
            // special formatting for 'ls' to match Linux terminal behavior
            // linux puts items on a single line with spaces, not newlines
            char processed_buffer[MAX_OUTPUT_SIZE];
            int j = 0;
            
            // convert newlines to spaces except for the last one
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] == '\n' && i < bytes_read - 1) {
                    processed_buffer[j++] = ' ';
                } else {
                    processed_buffer[j++] = buffer[i];
                }
            }
            
            // make sure we end with a newline for consistent terminal display
            if (j > 0 && processed_buffer[j-1] != '\n') {
                processed_buffer[j++] = '\n';
            }
            
            processed_buffer[j] = '\0';
            
            // log the processed output
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n", 
                  client_id, client_ip, client_port, processed_buffer);
                  
            // send the formatted output to the client
            send(client_socket, processed_buffer, j, 0);
        } 
        else {
            // for all other commands with output, format and send normally
            // create a copy for display that ensures we have proper newlines
            char display_buffer[MAX_OUTPUT_SIZE];
            strncpy(display_buffer, buffer, bytes_read);
            display_buffer[bytes_read] = '\0';
            
            // add a newline for display if needed
            if (bytes_read > 0 && display_buffer[bytes_read-1] != '\n') {
                strcat(display_buffer, "\n");
            }
            
            // log what we're sending to the client
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n", 
                  client_id, client_ip, client_port, display_buffer);
                  
            // check if we need to add a newline to the actual data sent to client
            int needs_newline = (bytes_read > 0 && buffer[bytes_read-1] != '\n');
            
            // send the original output first
            send(client_socket, buffer, bytes_read, 0);
            
            // add a newline if the output didn't end with one
            // this makes client display look cleaner
            if (needs_newline) {
                send(client_socket, "\n", 1, 0);
            }
        }
    } else {
        // no output was produced by the command
        printf("[OUTPUT] [Client #%d - %s:%d] Sending empty response (command had no output)\n\n", 
              client_id, client_ip, client_port);
        // send an empty string - no newline - to avoid extra blank lines in the client
        send(client_socket, "", 0, 0);
    }
}

// this function runs in a separate thread for each connected client
// handling all their commands until they disconnect
void *handle_client(void *arg) {
    // extract the client information from the argument
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    int client_id = info->client_id;
    
    // get a readable form of the client's IP address
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    // buffer for storing client commands as they come in
    char input[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    // main loop - keep receiving and processing commands until client disconnects
    while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
        // null-terminate the received data to treat it as a string
        input[bytes_received] = '\0';
        
        // check if the client wants to exit
        if (strcmp(input, "exit") == 0) {
            // log the exit command
            printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"exit\"\n", 
                   client_id, client_ip, client_port);
            // prepare a goodbye message
            char goodbye[] = "Goodbye!\n";
            printf("[OUTPUT] [Client #%d - %s:%d] Sending response: \"Goodbye!\"\n\n", 
                   client_id, client_ip, client_port);
            // send the goodbye message and break the loop to disconnect
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // for all other commands, execute them and send results back
        execute_shell_command(client_socket, input, client_ip, client_port, client_id);
    }
    
    // check if we exited the loop due to an error
    if (bytes_received < 0) {
        perror("recv");
        printf("[ERROR] [Client #%d - %s:%d] Error receiving data\n", 
              client_id, client_ip, client_port);
    }
    
    // log that the client has disconnected
    printf("[INFO] Client #%d disconnected from %s:%d\n", client_id, client_ip, client_port);
    
    // clean up resources for this client
    close(client_socket);
    free(info);
    
    return NULL;
}