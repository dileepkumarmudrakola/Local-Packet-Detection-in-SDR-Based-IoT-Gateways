#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8888
#define RECV_LEN 120*2048
#define PCKSZ 50000

int sfd;
FILE* fp;

int noOfSignals = 0;
char* getFileName(){
	char* filename;

	filename = (char*)malloc(20*sizeof(char));

	sprintf(filename, "signal%d.bin",++noOfSignals );

	return filename;
}

void sig_int() {
	close(sfd);
	exit(0);
}

int main(int argc, char* argv[]) {
	signal(SIGINT, sig_int);
	
	if((sfd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
		printf("Socket creation failed\n");
		exit(1);
	}
	struct sockaddr_in servaddr, cliaddr;
	socklen_t len = sizeof(cliaddr);
	memset((char*)&servaddr, 0, sizeof(servaddr));
	memset((char*)&cliaddr, 0, sizeof(cliaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(sfd, (struct sockaddr*) &servaddr, (socklen_t) sizeof(servaddr)) < 0) {
		printf("Bind failed\n");
		close(sfd);
		fclose(fp);
		exit(1);
	}

	char* buf = (char*) malloc((RECV_LEN) * sizeof(char));
	int tlen, rlen, of = 0;
	while(1) {
		char *sz=(char *)malloc(10 *sizeof(char));
		recvfrom(sfd, sz, 10, 0, (struct sockaddr*) &cliaddr, &len);
		sscanf(sz, "%d", &tlen);
		printf("Recieved Size %d\n", tlen);
		sendto(sfd, NULL, 0, 0, (struct sockaddr*) &cliaddr, len);
		if(tlen < RECV_LEN) {
			if(of == 0) {
				of = 1;
				fp = fopen(getFileName(), "w+");
			} else {
				of = 0;
			}
			for(int i = 0; i < (tlen + PCKSZ - 1) / PCKSZ; i++) {
				rlen = recvfrom(sfd, buf, RECV_LEN, 0, (struct sockaddr*) &cliaddr, &len);
				printf("Recieved -- %d bytes\n", rlen);
				for(int i = 0; i < rlen; i += 2) {
					fprintf(fp, "%f,", (unsigned char)buf[i] - '0' - 127.5);
					fprintf(fp, "%f\n", (unsigned char)buf[i + 1] - '0' - 127.5);
				}
				sendto(sfd, NULL, 0, 0, (struct sockaddr*) &cliaddr, len);
			}
			if(of == 0) {
				fclose(fp);
			}
		} else {
			fp = fopen(getFileName(), "w+");
			for(int i = 0; i < (tlen + PCKSZ - 1) / PCKSZ; i++) {
				rlen = recvfrom(sfd, buf, RECV_LEN, 0, (struct sockaddr*) &cliaddr, &len);
				printf("Recieved -- %d bytes\n", rlen);
				for(int i = 0; i < rlen; i += 2) {
					fprintf(fp, "%f,", (unsigned int)buf[i] - '0' - 127.5);
					fprintf(fp, "%f\n", (unsigned int)buf[i + 1] - '0' - 127.5);
				}
				sendto(sfd, NULL, 0, 0, (struct sockaddr*) &cliaddr, len);
			}
			fclose(fp);
		}
		tlen=0;
	}

	close(sfd);
	return 0;
}
