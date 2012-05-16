all:	fileserver fileclient
fileserver:	server.c shared.h
	gcc server.c -o fileserver -lpthread -Wall -g
fileclient:	client.c shared.h
	gcc client.c -o fileclient -lpthread -Wall -g


