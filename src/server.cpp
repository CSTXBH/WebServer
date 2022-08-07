#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

int port = 9609;

int main(){
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if(lfd == -1){
		perror("socket");
		exit(0);
	}
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if(ret == -1){
		perror("bind");
		exit(0);
	}
	
	ret = listen(lfd, 128);
	if(ret == -1){
			perror("listen");
			exit(0);
	}
	
	struct sockaddr_in cliaddr;
	unsigned int clilen = sizeof(cliaddr);
	int cfd = accept(lfd, (struct sockaddr*)&cliaddr, &clilen);
	if(ret == -1){
			perror("accept");
			exit(0);
	}
	
	char ip[24] = {0};
	printf("IP ADDR: %s, PORT: %d\n",
	           inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, sizeof(ip)),
	           ntohs(cliaddr.sin_port));
	           
	while(1){
		char buf[1024];
		memset(buf, 0, sizeof(buf));
		int len = read(cfd, buf, sizeof(buf));
		if(len > 0){
			printf("say: %s\n", buf);
			write(cfd, buf, len);
		}else if(len == 0){
			printf("close connect...\n");
			break;
		}else{
			perror("read");
			exit(0);
		}
	}
	
	close(lfd);
	close(cfd);
	return 0;
}
