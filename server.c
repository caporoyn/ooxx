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
	OFFLINE,
	ONLINE,
	SEARCHING,
	REQUESTING,
	REQUESTED,
	PLAYING
};

char STATUS_STRING[6][16] = {
	"Offline",
	"Online",
	"Searching",
	"Requesting",
	"Requested",
	"Playing"
};

typedef struct _AccountInfo{
	char account[16];
	char password[16];
	int fd;
	int tfd;	// target file descriptor
	int status;
	int gid;	// game id
}AccountInfo;

typedef struct _Game{
	char table[9];
	int playerfd[2];
	int turn;	// 0 or 1
}Game;

void load_account(AccountInfo* ai, int* aiCount){
	char line[64];
	FILE* fp = fopen("account.txt", "r");
	int count = 0;
	while(fgets(line, 64, fp) != NULL){
		char* temp = strstr(line, "\t");
		memset(ai[count].account, 0, 16);
		strncpy(ai[count].account, line, temp - &line[0]);
		memset(ai[count].password, 0, 16);
		temp++;
		char* temp2 = strstr(temp, "\n");
		strncpy(ai[count].password, temp, temp2 - temp);

		count++;
	}
	*aiCount = count;
}

int fd_to_account_id(int fd, AccountInfo* ai, int aiCount){
	int i;
	for(i = 0; i < aiCount; i++){
		if(ai[i].fd == fd)
			return i;
	}
	return -1;
}

int name_to_account_id(char* name, AccountInfo* ai, int aiCount){
	int i;
	for(i = 0; i < aiCount; i++){
		if(strcmp(name, ai[i].account) == 0)
			return i;
	}
	return -1;
}

void handle(int fd, char* buffer, int len, AccountInfo* ai, int aiCount, fd_set* fdset, Game* games, int* gameId){
	if(strncmp(buffer, "LOGIN ", 6) == 0){	// handle login(this command is generate by client program, not user typing)
		/* parse from buffer */
		char account[32];
		char password[32];
		memset(account, 0, 32);
		memset(password, 0, 32);
		char* temp = strstr(&buffer[6], " ");
		strncpy(account, &buffer[6], temp - &buffer[6]);
		account[temp - &buffer[6]] = '\0';
		temp++;
		strncpy(password, temp, &buffer[len] - 1 - temp);	// ignore newline
		password[&buffer[len] - 1 - temp] = '\0';
		/* check in ai table */
		int i;
		for(i = 0; i < aiCount; i++){
			if(strcmp(ai[i].account, account) == 0 && strcmp(ai[i].password, password) == 0 && ai[i].status == OFFLINE){		// prevent login twice
				write(fd, "SUCCESS\0", 8);
				ai[i].status = ONLINE;
				ai[i].fd = fd;
				printf("server: fd %d login as %s\n", fd, ai[i].account);
				break;
			}
		}
		if(i == aiCount)
			write(fd, "FAIL\0", 5);
	}
	else{
		if(ai[fd_to_account_id(fd, ai, aiCount)].status == ONLINE){
			if(strncmp(buffer, "q\n", 2) == 0){	// handle logout
				ai[fd_to_account_id(fd, ai, aiCount)].status = OFFLINE;
				printf("server: %s logout\n", ai[fd_to_account_id(fd, ai, aiCount)].account);
				FD_CLR(fd, fdset);
				close(fd);
			}
			else if(strncmp(buffer, "l\n", 2) == 0){		// handle list users
				char wbuffer[2048];
				strcpy(wbuffer, "Accout\tStatus\n");
				int i;
				for(i = 0; i < aiCount; i++){
					strcat(wbuffer, ai[i].account);
					strcat(wbuffer, "\t");
					strcat(wbuffer, STATUS_STRING[ai[i].status]);
					strcat(wbuffer, "\n");
				}
				write(fd, wbuffer, strlen(wbuffer));
			}
			else if(strncmp(buffer, "p\n", 2) == 0){		// change user status
				ai[fd_to_account_id(fd, ai, aiCount)].status = SEARCHING;
			}
			else
				write(fd, "Can't parse command :(\n", 23);
		}
		else if(ai[fd_to_account_id(fd, ai, aiCount)].status == SEARCHING){
			char* temp = strstr(buffer, "\n");
			*temp = '\0';
			int rid = name_to_account_id(buffer, ai, aiCount);		// the account id request to
			if(rid != -1 && fd != ai[rid].fd && ai[rid].status == ONLINE){	// target account must be LOGIN status and not client themselves
				char temp[64];
				sprintf(temp, "Wait for %s...\n", buffer);
				write(fd, temp, strlen(temp));
				ai[fd_to_account_id(fd, ai, aiCount)].status = REQUESTING;
				ai[fd_to_account_id(fd, ai, aiCount)].tfd = ai[rid].fd;

				ai[rid].status = REQUESTED;		// write invite message to target
				ai[rid].tfd = ai[fd_to_account_id(fd, ai, aiCount)].fd;
				sprintf(temp, "\nInvitation from %s...\n", ai[fd_to_account_id(fd, ai, aiCount)].account);
				write(ai[rid].fd, temp, strlen(temp));
			}
			else{
				write(fd, "Request failed :(\n", 18);
				ai[fd_to_account_id(fd, ai, aiCount)].status = ONLINE;
			}
		}
		else if(ai[fd_to_account_id(fd, ai, aiCount)].status == REQUESTING){
			if(strncmp(buffer, "c\n", 2) == 0){
				ai[fd_to_account_id(fd, ai, aiCount)].status = ONLINE;

				int tfd = ai[fd_to_account_id(fd, ai, aiCount)].tfd;		// send cancel message to target
				ai[fd_to_account_id(tfd, ai, aiCount)].status = ONLINE;
				write(tfd, "\nRequest canceled...\n", 21); 
			}
			else
				write(fd, "Can't parse command :(\n", 23);
		}
		else if(ai[fd_to_account_id(fd, ai, aiCount)].status == REQUESTED){
			if(strncmp(buffer, "y\n", 2) == 0){
				ai[fd_to_account_id(fd, ai, aiCount)].status = PLAYING;
				int tfd = ai[fd_to_account_id(fd, ai, aiCount)].tfd;
				ai[fd_to_account_id(tfd, ai, aiCount)].status = PLAYING;

				(*gameId)++;
				ai[fd_to_account_id(fd, ai, aiCount)].gid = *gameId;
				ai[fd_to_account_id(tfd, ai, aiCount)].gid = *gameId;
				games[*gameId].playerfd[0] = fd;
				games[*gameId].playerfd[1] = tfd;
				int i;
				for(i = 0; i < 9; i++)
					games[*gameId].table[i] = '0' + 1 + i;
				games[*gameId].turn = 0;

				printf("server: fd %d and %d join game %d\n", fd, tfd, *gameId);

				char temp[1000];
				sprintf(temp, "Join game %d!\nYou are %c\n--------------\n| 1 | 2 | 3 |\n--------------\n| 4 | 5 | 6 |\n--------------\n| 7 | 8 | 9 |\n--------------\nIt's your turn!\n", *gameId, 'X');
				write(fd, temp, strlen(temp));
				sprintf(temp, "Join game %d!\nYou are %c\n--------------\n| 1 | 2 | 3 |\n--------------\n| 4 | 5 | 6 |\n--------------\n| 7 | 8 | 9 |\n--------------\n", *gameId, 'o');
				write(tfd, temp, strlen(temp));
			}
			else if(strncmp(buffer, "n\n", 2) == 0){
				ai[fd_to_account_id(fd, ai, aiCount)].status = ONLINE;
				write(fd, "Reject invitation\n", 18);

				int tfd = ai[fd_to_account_id(fd, ai, aiCount)].tfd;		// send reject message to target
				ai[fd_to_account_id(tfd, ai, aiCount)].status = ONLINE;
				write(tfd, "\nInvitation been rejected...\n", 29);
			}
			else
				write(fd, "Can't parse command :(\n", 23);
		}
		else if(ai[fd_to_account_id(fd, ai, aiCount)].status == PLAYING){
			int gid = ai[fd_to_account_id(fd, ai, aiCount)].gid;
			if(strncmp(buffer, "p\n", 2) == 0){
				char temp[1000];
				sprintf(temp, "You are %c\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\n", games[gid].playerfd[0] == fd ? 'X' : 'O', 
						games[gid].table[0], games[gid].table[1], games[gid].table[2], games[gid].table[3], games[gid].table[4], games[gid].table[5], 
						games[gid].table[6], games[gid].table[7], games[gid].table[8]);
				write(fd, temp, strlen(temp));
			}
			else{
				int i;
				for(i = 0; i < 9; i++){
					char temp[3];
					sprintf(temp, "%d\n", i + 1);
					if(strncmp(temp, buffer, 2) == 0){
						if(fd == games[gid].playerfd[games[gid].turn]){
							if(games[gid].table[i] == '0' + i + 1){						// check if empty
								games[gid].table[i] = games[gid].turn ? 'O' : 'X';		// insert to table
								games[gid].turn = !games[gid].turn;						// change turn
								char str[1000];
								sprintf(str, "\nYou are %c\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\n", games[gid].playerfd[0] == fd ? 'X' : 'O', 
										games[gid].table[0], games[gid].table[1], games[gid].table[2], games[gid].table[3], games[gid].table[4], games[gid].table[5], 
										games[gid].table[6], games[gid].table[7], games[gid].table[8]);
								write(fd, str, strlen(str));
								sprintf(str, "\nYou are %c\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\n| %c | %c | %c |\n--------------\nIt's your turn\n", games[gid].playerfd[0] == fd ? 'O' : 'X', 
										games[gid].table[0], games[gid].table[1], games[gid].table[2], games[gid].table[3], games[gid].table[4], games[gid].table[5], 
										games[gid].table[6], games[gid].table[7], games[gid].table[8]);
								write(ai[fd_to_account_id(fd, ai, aiCount)].tfd, str, strlen(str));
								/* check if the game should end*/
								char winner = ' ';		// ' ' mean not end, 't' mean tie
								if(games[gid].table[0] == games[gid].table[1] && games[gid].table[1] == games[gid].table[2])
									winner = games[gid].table[0];
								else if(games[gid].table[3] == games[gid].table[4] && games[gid].table[4] == games[gid].table[5])
									winner = games[gid].table[3];
								else if(games[gid].table[6] == games[gid].table[7] && games[gid].table[7] == games[gid].table[8])
									winner = games[gid].table[6];
								else if(games[gid].table[0] == games[gid].table[3] && games[gid].table[3] == games[gid].table[6])
									winner = games[gid].table[0];
								else if(games[gid].table[1] == games[gid].table[4] && games[gid].table[4] == games[gid].table[7])
									winner = games[gid].table[1];
								else if(games[gid].table[2] == games[gid].table[5] && games[gid].table[5] == games[gid].table[8])
									winner = games[gid].table[2];
								else if(games[gid].table[0] == games[gid].table[4] && games[gid].table[4] == games[gid].table[8])
									winner = games[gid].table[0];
								else if(games[gid].table[2] == games[gid].table[4] && games[gid].table[4] == games[gid].table[6])
									winner = games[gid].table[2];
								else if(games[gid].table[0] != '1' && games[gid].table[1] != '2' && games[gid].table[2] != '3' && games[gid].table[3] != '4' && 
										games[gid].table[4] != '5' && games[gid].table[5] != '6' && games[gid].table[6] != '7' && games[gid].table[7] != '8' && 
										games[gid].table[8] != '9')
									winner = 't';
								else
									winner = ' ';

								if(winner == 'O'){
									write(games[gid].playerfd[1], "\n--- You win ---\n", 17);
									write(games[gid].playerfd[0], "\n--- You lose ---\n", 18);
								}
								else if(winner == 'X'){
									write(games[gid].playerfd[0], "\n--- You win ---\n", 17);
									write(games[gid].playerfd[1], "\n--- You lose ---\n", 18);
								}
								else if(winner == 't'){
									write(games[gid].playerfd[0], "\n--- Tie ---\n", 13);
									write(games[gid].playerfd[1], "\n--- Tie ---\n", 13);
								}

								if(winner != ' '){
									ai[fd_to_account_id(fd, ai, aiCount)].status = ONLINE;
									ai[fd_to_account_id(ai[fd_to_account_id(fd, ai, aiCount)].tfd, ai, aiCount)].status = ONLINE;
								}
							}
							else
								write(fd, "Can't put there!\n", 17);
						}
						else
							write(fd, "It's not your turn!\n", 20);
						break;
					}
				}
				if(i == 9)
					write(fd, "Can't parse command :(\n", 23);
			}
		}
	}
}

int main(){
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;	// the address bind to server socket
	fd_set fdset;	// main fd set, contain listener and clients' fd
	int maxfd = sockfd;

	/* set addr's info */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(8869);

	if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) != 0){
		fprintf(stderr, "bind() failed\n");
		close(sockfd);
		return -1;
	}
	listen(sockfd, 10);

	AccountInfo ai[10];
	memset(ai, 0, sizeof(ai));
	int aiCount;
	load_account(ai, &aiCount);

	Game games[16];
	int gameId = -1;		// newest game id

	FD_ZERO(&fdset);
	FD_SET(sockfd, &fdset);		// add listener's fd into fdset
	char buffer[1024];			// buffer read from client

	while(1){
		fd_set readset = fdset;		// file destriptor that can read
		if(select(maxfd + 1, &readset, NULL, NULL, NULL) == -1){
			fprintf(stderr, "select() failed\n");
			break;
		}

		int i = 0;
		for(i = 0; i <= maxfd; i++){
			if(FD_ISSET(i, &fdset) && FD_ISSET(i, &readset)){	// traverse all fd but only catch fd that is in main fdset
				if(i == sockfd){	// new connection happend
					int clisockfd = accept(sockfd, NULL, NULL);
					printf("server: fd %d connect\n", clisockfd);
					FD_SET(clisockfd, &fdset);
					if(clisockfd > maxfd)
						maxfd = clisockfd;
				}
				else{	// message from client
					int len = read(i, buffer, 1024);
					if(len > 0){
						printf("fd %d: ", i);
						fwrite(buffer, 1, len, stdout);
						handle(i, buffer, len, ai, aiCount, &fdset, games, &gameId);
					}
					else{	// len = 0 means client disconnect
						FD_CLR(i, &fdset);
						close(i);
						printf("server: fd %d disconnect\n", i);
						if(fd_to_account_id(i, ai, aiCount) != -1){		// disconnect after login
							if(ai[fd_to_account_id(i, ai, aiCount)].status == REQUESTING || ai[fd_to_account_id(i, ai, aiCount)].status == REQUESTED || 
								ai[fd_to_account_id(i, ai, aiCount)].status == PLAYING){
								int tfd = ai[fd_to_account_id(i, ai, aiCount)].tfd;
								ai[fd_to_account_id(tfd, ai, aiCount)].status = ONLINE;
								char temp[32];
								sprintf(temp, "\n%s logout :(\n", ai[fd_to_account_id(i, ai, aiCount)].account);
								write(tfd, temp, strlen(temp));
							}
							ai[fd_to_account_id(i, ai, aiCount)].status = OFFLINE;
							printf("server: %s auto logout\n", ai[fd_to_account_id(i, ai, aiCount)].account);
						}
					}
				}
			}
		}
	}


	close(sockfd);

	return 0;
}
