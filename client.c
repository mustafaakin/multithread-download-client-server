#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <netdb.h>

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int downloadCount = 0;
int connect_host(char* address, int port){
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

int dataPort = -1;
int data_list_sock = -1;

void openDataChannel(){
	int list_sock;
	struct sockaddr_in sa;
	list_sock = socket (AF_INET, SOCK_STREAM, 0);	
	bzero (&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = 0;	
	bind (list_sock,(struct sockaddr *) &sa,sizeof(sa));
	listen (list_sock, 25);
	socklen_t len = sizeof(sa);
	if (getsockname(list_sock, (struct sockaddr *)&sa, &len) != -1)
    	dataPort = ntohs(sa.sin_port);    	
    data_list_sock = list_sock;
}

struct param {
	int sock;
};

// Downloads files, procedure:
// 1) Look for file.
// 2) If non existent, don't send anything, just close it.
// 3) If exists, send filename. Send file length. Then the file contents itself.
void *download(void* args){
	struct param *p;
	p = (struct param *) args;
	char msg[4];
	read(p->sock, msg, 4);	 
	int size = ntohl(*((int *)msg));
	if ( size == FILE_NOT_FOUND){
		printf("\t[NOT OK] No such file.\n");
	} else {
		clock_t st_time = clock() / (CLOCKS_PER_SEC / 1000);
		char filename[MAX_FILE_LENGTH];
		read(p->sock, filename, MAX_FILE_LENGTH);
		printf("\t[Downloading] %20s %.2lf kbytes.\n", filename, size / 1024.0);
		char* buff = malloc(size);
		bzero(buff, size);
		int downSize = size;
		int resp = 1;
		while ( resp > 0){
			printf("%d : %p -->", downSize, buff);
			resp = read(p->sock, buff, 65336);	
			printf("read piece: %d\n",resp);
			buff = buff + resp;
			downSize = downSize - resp;
		}
		buff = buff - size - 1;

		FILE *fp = fopen(filename, "wb");
		fwrite(buff,1,size,fp);
		fclose(fp);	
		// free(buff);
		clock_t en_time = clock() / (CLOCKS_PER_SEC / 1000);
		printf("\t[Downloaded] %20s, took %d ms.\n", filename, (int)(en_time-st_time));
	} 
	
	pthread_mutex_lock( &mutex1 );
	downloadCount--;
	pthread_mutex_unlock( &mutex1 );	
	close(p->sock);
	pthread_exit(NULL);
}

// Start listening on a data channel that will be only used to accept the incoming data transfers from servers.
void *listenDataChannel(void *args){
	int conn_sock;
	struct sockaddr_in ca;
	socklen_t ca_len;
	while (1){
		bzero (&ca, sizeof(ca));
		ca_len = sizeof(ca); // important to initialize
		conn_sock = accept (data_list_sock, (struct sockaddr *) &ca,&ca_len);
		
		pthread_t tid;
		struct param* args = malloc(sizeof(struct param));
		args->sock = conn_sock;
		int ret = pthread_create(&(tid), NULL, download, (void *)args);
		if ( ret < -1){
			printf("FATAL ERROR! Thread create failed.");
			exit(-1);
		}		
	}
	pthread_exit(NULL);
}


// This code is taken from: http://www.binarytides.com/blog/get-ip-address-from-hostname-in-c-using-linux-sockets/
int hostname_to_ip(char * hostname , char* ip)
{
	struct hostent *he;
	struct in_addr **addr_list;
	int i;
	if ( (he = gethostbyname( hostname ) ) == NULL)
	{
		// get the host info
		herror("gethostbyname");
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;

	for(i = 0; addr_list[i] != NULL; i++)
	{
		//Return the first one;
		strcpy(ip , inet_ntoa(*addr_list[i]) );
		return 0;
	}
	return 1;
}

int main(int argc, char *argv[]){
	if ( argc != 3){
		printf("Wrong usage!\n");
		exit(-1);
	}
		 
	char *hostname = argv[1];
	char ip[100];
	hostname_to_ip(hostname , ip);
		
	openDataChannel();
	pthread_t tid;
	int ret = pthread_create(&(tid), NULL, listenDataChannel, NULL);
	if ( ret < 0){
		printf("FATAL ERROR! Thread create failed.");
		exit(-1);
	}
	
	int port = atoi(argv[2]);		
	printf("Trying to connect to host\n");
	int controlSocket = connect_host(hostname,port);	
	printf("Connected. Commands available: \n* list\n* get <file1> <file2> .. <fileN>\n* quit\n");
	char initial[4];
	*((int *)initial) = htonl(dataPort);			 
	write(controlSocket, initial, 4);
	
	char buff[MAXLINELEN];	
	printf(">");
	while (fgets(buff, MAXLINELEN, stdin) != NULL) {
		buff[strcspn(buff, "\n")] = '\0';
		if (buff[0] == '\0')
			continue;
			
		char *ch = strtok(buff, " ");
		if ( strcmp(ch,"quit") == 0){
			write(controlSocket, MESSAGE_QUIT, COMMAND_LENGTH);
			printf("Bye!\n");
			return 0;
		}
		else if ( strcmp(ch,"list") == 0){
			write(controlSocket, MESSAGE_LIST, COMMAND_LENGTH);
			char buf[MAX_FILE_LENGTH];
			while ( 1){	 // Recives each file and then FILE_SENDING_ENDED(.) at last, . is chosen because only special files are . & ..
				bzero(buf, MAX_FILE_LENGTH);
				int k = read (controlSocket, buf, MAX_FILE_LENGTH);
				if ( k == 0 || strcmp(buf,FILE_SENDING_ENDED) == 0){
					break;
				}
				printf("%s ", buf);					 
			}
			printf("\n");
		} else if ( strcmp(ch,"get") == 0){
			// For each file, send server: GETT, A, GETT, B, GETT, C, GETT, D (i.e for a file request, GETT is send, after filename is sent)
			while ( (ch = strtok(NULL, " ,")) != NULL) {
				pthread_mutex_lock( &mutex1 );
				downloadCount++;
				pthread_mutex_unlock( &mutex1 );
				write(controlSocket, MESSAGE_GET, COMMAND_LENGTH);
				char tmp[MAX_FILE_LENGTH];
				strcpy(tmp, ch);
				write(controlSocket, tmp, MAX_FILE_LENGTH);
			}
			printf("Starting to download %d files\n" , downloadCount);
			while ( downloadCount > 0); // Wait for downloads to finish, count decreased by threads in synchronization.
		} else {
			printf("No such command as: %s\n", ch);
		}
		bzero(buff, MAXLINELEN);
		printf(">");		
	}		
	close(controlSocket);	
	exit (0);
}
