CLIENT = posix_ipc-client
SERVER = posix_ipc-server

CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -pthread -lrt

.PHONY: all clean

all: $(CLIENT) $(SERVER)

$(CLIENT): posix_ipc-client.c
	$(CC) $(CFLAGS) posix_ipc-client.c -o $(CLIENT) $(LIBS)

$(SERVER): posix_ipc-server.c
	$(CC) $(CFLAGS) posix_ipc-server.c -o $(SERVER) $(LIBS)

clean:
	rm -f $(CLIENT) $(SERVER) out.txt