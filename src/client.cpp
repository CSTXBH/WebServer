#include <sys/epoll.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int port = 9609;

int main(){
	int cfd = socket(AF_INET, SOCK_STREAM, 0);
	if(cfd == -1){
		perror("socket error");
		exit(1);
	}
//	int opt = 1;
//	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	inet_pton(AF_INET, "192.168.50.128", &serv_addr.sin_addr.s_addr);
//	inet_pton(AF_INET, "10.106.248.115", &addr.sin_addr.s_addr);
//	serv_addr.sin_addr.s_addr = htonl("10.106.255.240");
	
//	int opt = 1;
//	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	
	int ret = connect(cfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if(ret == -1){
		perror("connect");
		exit(0);
	}
	int number = 0;
	while(1){
		char buf[1024];
		memset(buf, 0, sizeof(buf));
		scanf("%s",&buf);
//		sprintf(buf,"Hello,server...%d\n",number++);
		send(cfd, buf, sizeof(buf), 0);
		
		char rbuf[1024];
		memset(rbuf, 0, sizeof(buf));
		int len = recv(cfd, rbuf, sizeof(buf), 0);
		if(len > 0){
			printf("sever say: %s\n", rbuf);
		}else if(len == 0){
			printf("close connect...\n");
			break;
		}else{
			perror("read");
			exit(0);
		}
		sleep(1);
	}
	char buf[1024];
	memset(buf, 0, sizeof(buf));
	send(cfd, buf, sizeof(buf), 0);
	close(cfd);
	
	return 0;
}
