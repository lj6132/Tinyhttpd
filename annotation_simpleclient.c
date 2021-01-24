#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int sockfd;
    int len;
    struct sockaddr_in address;         //socket
    int result;
    char ch = 'A';

    sockfd = socket(AF_INET, SOCK_STREAM, 0);       //client socket

    //server socket information
    address.sin_family = AF_INET;                   //protocol family
    address.sin_addr.s_addr = inet_addr("192.168.84.128");  //store target IP address(by Little-endian mode[network mode])
    address.sin_port = htons(4000);                         //store target port number(by Little-endian mode[network mode]), "s" means length is 16.
    len = sizeof(address);
    result = connect(sockfd, (struct sockaddr *)&address, len);     //connect to server. sockaddr and sockaddr_in are parellel, so we can explicitly convert sockaddr_in to sockaddr.

    if (result == -1)
    {
        perror("oops: client1");
        exit(1);
    }
    write(sockfd, &ch, 1);      //sent "A" to server
    read(sockfd, &ch, 1);       //read something from server
    printf("char from server = %c\n", ch);
    close(sockfd);              //close
    exit(0);
}
