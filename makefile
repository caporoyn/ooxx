all: server client
	gcc server.c -o server
	gcc client.c -o client
