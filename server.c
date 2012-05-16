#include "shared.h"
#include <sys/socket.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>


int connect_host(const char* address, int port){
	int sock;
	struct sockaddr_in sa;
	int ret;
	sock = socket (AF_INET, SOCK_STREAM, 0);
	bzero (&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	inet_pton (AF_INET, address, &sa.sin_addr);
	ret = connect (sock,(const struct sockaddr *) &sa,sizeof (sa));	
	if (ret != 0) {
		printf ("Connect Failed :(\n");
		exit (0);
	}
	return sock;
}

// Send each file and FILE_SENDING_ENDED to indicate finished set.
void sendFileNames(int sock){
	DIR *dir = opendir ("./");
	struct dirent *ent;
	if (dir != NULL) {
		while ((ent = readdir (dir)) != NULL) {
			if ( ent->d_type == DT_REG) {
				write(sock, ent->d_name, MAX_FILE_LENGTH);
			}
	  	}
	 	closedir (dir);
	}
	write(sock, FILE_SENDING_ENDED, sizeof(char)*1);
}

struct send_params {
	const char* host;
	int port;
	char* filename;
};


void* sendFile(void* args){
	struct send_params *p;
	p = (struct send_params *) args;
	int sock = connect_host(p->host, p->port);
	printf("\t[T]Started Sending %s \n", p->filename);

	FILE *file = fopen(p->filename, "rb");
	if (!file){
		char msg[4];
		*((int *)msg) = htonl(FILE_NOT_FOUND);
		write(sock, msg, 4);	
		pthread_exit(NULL);
	}	
	fseek(file, 0, SEEK_END);
	unsigned long fileLen =ftell(file);
		
	// Sending File Length
	char msg[4];
	*((int *)msg) = htonl(fileLen);			 
	write(sock, msg, 4);		
	
	// Sending File Name
	write(sock, p->filename, MAX_FILE_LENGTH);
		
	rewind(file);
	//Allocate memory
	char *buffer=(char *)malloc(fileLen+1);
	fread(buffer, fileLen, 1, file);
	fclose(file);
	write(sock, buffer, fileLen + 1);
	close(sock);
	printf("\t[T] Sent %d %s\n", (int)fileLen, p->filename);
	free(buffer);
	pthread_exit(NULL);
}


int main(int argc, char *argv[]){
	if ( argc != 2){
		printf("Wrong usage!\n");
		exit(-1);
	}
	
	int list_sock;
	int conn_sock;
	struct sockaddr_in sa, ca;
	socklen_t ca_len;
	char ipaddrstr[IPSTRLEN];

	list_sock = socket (AF_INET, SOCK_STREAM, 0);	
	
	int optval = 1;
	setsockopt(list_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval); // Letting the port to be reused after unexpected termination (being unable to close port)
	
	bzero (&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(atoi(argv[1]));

	bind (list_sock,(struct sockaddr *) &sa,sizeof(sa));
	listen (list_sock, 10);
	printf("Server is Online!\n");
	while (1)
	{
		bzero (&ca, sizeof(ca));
		ca_len = sizeof(ca); // important to initialize
		conn_sock = accept (list_sock, (struct sockaddr *) &ca,&ca_len);
		pid_t child = fork();
		if ( child == 0){					
			const char* client_ip = inet_ntop(AF_INET, &(ca.sin_addr),ipaddrstr, IPSTRLEN);
			int client_port = ntohs(ca.sin_port);
			printf ("** New connection from: ip=%s port=%d \n", client_ip, client_port);		
			char initial[4];
			read (conn_sock, initial , 4);
			int downloadPort = ntohl(*((int *)initial));			
			while ( 1){
				char buff[COMMAND_LENGTH + 1];
				bzero(&buff, COMMAND_LENGTH + 1);
				int k = read (conn_sock, buff, COMMAND_LENGTH);
				if ( k <= 0){
					break;
				}
				if ( strcmp(buff,"LIST") == 0){
					printf("LIST OPERATION, SENDING FILE NAMES\n");
					sendFileNames(conn_sock);
					bzero(buff,COMMAND_LENGTH);
				} else if ( strcmp(buff,MESSAGE_QUIT) == 0){
					printf("Quiting..");
					break;
				} else if ( strcmp(buff,MESSAGE_GET) == 0){
					// If get is recieved, expecting file name now.
					char b[MAX_FILE_LENGTH];
					bzero(b, MAX_FILE_LENGTH);
					read (conn_sock, b, MAX_FILE_LENGTH);
					struct send_params* args = malloc(sizeof(struct send_params));
					args->host = client_ip;
					args->port = downloadPort;					
					char* mychar = malloc(sizeof(char)*MAX_FILE_LENGTH);
					strcpy(mychar, b);
					args->filename = mychar;
					pthread_t tid;
					// File sending handler thread creation					
					int ret = pthread_create(&(tid), NULL, sendFile, (void *)args);
					if ( ret < 0){
						exit(-1);
					}
				}				
			}
			close (conn_sock);
			printf ("Server closed connection to client\n");
			exit(1);
		}
		wait(NULL); 
	}
}
