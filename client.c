#include	<sys/socket.h>
#include	<sys/types.h>
#include	<sys/wait.h>
#include	<sys/stat.h>
#include	<fcntl.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<unistd.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

enum{
	ONLINE,
	SEARCHING,
	REQUESTING,
	REQUESTED,
	PLAYING
};

int main(){
	char account[16];
	char password[16];

	printf("Account: ");
	fgets(account, 16, stdin);
	account[strlen(account) - 1] = '\0';
	printf("Password: ");
	fgets(password, 16, stdin);
	password[strlen(password) - 1] = '\0';

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int maxfd = sockfd;

	struct sockaddr_in servaddr;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(8869);

	if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0){
		fprintf(stderr, "connect() failed\n");
		close(sockfd);
		return -1;
	}

	char line[1024];

	/* test login */
	sprintf(line, "LOGIN %s %s\n", account, password);
	write(sockfd, line, strlen(line));
	char buf[2048];		// read buffer from server
	read(sockfd, buf, 2048);
	if(strcmp(buf, "FAIL") == 0){
		fprintf(stderr, "login failed!\n");
		close(sockfd);
		return -1;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(sockfd, &fdset);
	FD_SET(0, &fdset);

	char prompt[5][64] = {
		"(p)lay\t(l)ist users\t(q)uit> ",
		"Search user account> ",
		"Waiting for accept... (c)ancel> ",
		"Accept? (y)es\t(n)o> ",
		"(1~9)\t(p)rint> "
	};
	int curPrompt = ONLINE;

	printf("%s", prompt[curPrompt]);
	fflush(stdout);

	while(1){	
		fd_set readset = fdset;
		struct timeval tv = {0, 0};		// let select() return immediately
		if(select(maxfd + 1, &readset, NULL, NULL, &tv) == -1){
			fprintf(stderr, "select() failed\n");
			break;
		}

		if(FD_ISSET(sockfd, &readset)){		// if get something from server
			int len = read(sockfd, buf, 2048);
			write(1, buf, len);

			if(curPrompt == SEARCHING){
				if(strncmp(buf, "Request failed :(\n", 18) == 0)
					curPrompt = ONLINE;
				else
					curPrompt = REQUESTING;
			}
			else if(strncmp(buf, "\nInvitation from", 16) == 0)
				curPrompt = REQUESTED;
			else if(strncmp(buf, "\nRequest canceled...\n", 21) == 0)
				curPrompt = ONLINE;
			else if(strncmp(buf, "\nInvitation been rejected...\n", 29) == 0){
				curPrompt = ONLINE;
			}
			else if(strncmp(buf, "Join", 4) == 0 || strncmp(buf, "\nJoin", 5) == 0)
				curPrompt = PLAYING;
			else if(curPrompt == PLAYING && (strstr(buf, "--- You") != NULL || strstr(buf, "--- Tie") != NULL))
				curPrompt = ONLINE;
			else if(strstr(buf, " logout :(\n") != NULL)
				curPrompt = ONLINE;

			printf("%s", prompt[curPrompt]);		// prompt again
			fflush(stdout);
		}

		if(FD_ISSET(0, &readset)){			// if get something from user input
			fgets(line, 1024, stdin);

			write(sockfd, line, strlen(line));	// write user input to server

			if(curPrompt == ONLINE && strncmp(line, "p\n", 2) == 0){
				curPrompt = SEARCHING;
				printf("%s", prompt[curPrompt]);		// prompt again
				fflush(stdout);
			}
			else if(curPrompt == ONLINE && strncmp(line, "q\n", 2) == 0){
				printf("logout\n");
				break;
			}
			else if(curPrompt == REQUESTING && strncmp(line, "c\n", 2) == 0){
				printf("Request canceled\n");
				curPrompt = ONLINE;
				printf("%s", prompt[curPrompt]);		// prompt again
				fflush(stdout);
			}
			else if(curPrompt == REQUESTED && strncmp(line, "n\n", 2) == 0)
				curPrompt = ONLINE;
		}
	}

	close(sockfd);

	return 0;
}
