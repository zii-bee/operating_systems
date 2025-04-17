CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS = -lpthread

# Common source files
COMMON_SRC = src/parser.c src/executor.c src/redirection.c src/pipes.c src/error_handling.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

# server source files
SERVER_SRC = src/server_main.c src/server.c src/thread_handler.c
SERVER_OBJ = $(SERVER_SRC:.c=.o)

# client source files
CLIENT_SRC = src/client_main.c src/client.c
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# targets
SERVER_TARGET = server
CLIENT_TARGET = client

all: $(SERVER_TARGET) $(CLIENT_TARGET)

# server build
$(SERVER_TARGET): $(COMMON_OBJ) $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# client build
$(CLIENT_TARGET): $(COMMON_OBJ) $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# compile sources to object files
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(SERVER_TARGET) $(CLIENT_TARGET)