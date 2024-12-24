// PA1 - uftp_client

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <netdb.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define SERVER_MAX_PACKET_RETRY 5
#define PACKET_TRANSFER_TIMEOUT_S 10

//error - wrapper for perror
void error(char* msg) 
{
    perror(msg);
    exit(0);
}
void warning(char* msg) 
{
    perror(msg);
}

int sendto_reliable(int sockfd, char* message, int message_size, struct sockaddr_in* serveraddr, int* serverlen)
{
    int n = 0;

    // Only try resending packet a certain number of times
    while (n < SERVER_MAX_PACKET_RETRY)
    {
        // Send the packet
        if (sendto(sockfd, message, message_size, 0, (struct sockaddr *) serveraddr, *serverlen) < 0)
            break;
        
        // Poll the server for an acknowledgment

        char buf[BUFSIZE];
        memset(&buf, 0, BUFSIZE);

        if (recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) serveraddr, (socklen_t *) serverlen) < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("Timeout occurred, trying again...\n");
            }
            else 
                break;
        }
        else if (strcmp(buf, "ACK") == 0)
            return 0;   // Acknowledgment received

        n++;    // Try again if timeout
    }

    return -1;
}

int recvfrom_reliable(int sockfd, char* message, int message_size, struct sockaddr_in* serveraddr, int* serverlen)
{
    int n = 0;

    // Only try receiving packet a certain number of times
    while (n < SERVER_MAX_PACKET_RETRY)
    {
        int received = recvfrom(sockfd, message, message_size, 0, (struct sockaddr *) serveraddr, (socklen_t *) serverlen);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("Timeout occurred, trying again...\n");
                n++;
                continue;   // Try again if timeout
            }
            else
                break;
        }
            
        // Packet received successfully; send ACK
        if (sendto(sockfd, "ACK", 3, 0, (struct sockaddr *) serveraddr, *serverlen) < 0)
            break;
        
        return received;    // Return bytes received
    }

    return -1;
}

// Predefine command functions
void get_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename);
void put_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename);
void delete_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename);
void ls_files(int sockfd, struct sockaddr_in* serveraddr, int* serverlen);

int main(int argc, char **argv) 
{
    int sockfd;                         // socket
    int portnum;                        // port to listen on
    int serverlen;                      // byte size of server's address

    struct sockaddr_in serveraddr;      // server addr
    struct hostent *server;             // server host info

    char *serveraddr_str;               // IP address of server

    char buf[BUFSIZE];                  // message buf
    int n;                              // message byte size

    // check command line arguments
    if (argc != 3) 
    {
        fprintf(stderr,"usage: %s <serveraddr_str> <port>\n", argv[0]);
        exit(0);
    }
    
    serveraddr_str = argv[1];
    portnum = atoi(argv[2]);

    // socket: create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sockfd < 0) 
        error("ERROR opening socket");
    
    // Set timeout value in seconds
    struct timespec timeout;
    timeout.tv_sec = PACKET_TRANSFER_TIMEOUT_S;
    timeout.tv_nsec = 0;

    // Tell socket to timeout after given time
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timespec)) < 0)
    {
        close(sockfd);
        error("ERROR in setsockopt");
    }

    // gethostbyname: get the server's DNS entry
    server = gethostbyname(serveraddr_str);
    
    if (server == NULL)
    {
        fprintf(stderr,"ERROR, no such host as %s\n", serveraddr_str);
        exit(0);
    }

    // build the server's Internet address
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    
    inet_pton(AF_INET, serveraddr_str, &serveraddr.sin_addr.s_addr);
    serveraddr.sin_port = htons(portnum);

    serverlen = sizeof(serveraddr);

    printf("Below are available commands:\n");
    printf("get [filename]      - Requests file from server.\n");
    printf("put [filename]      - Sends file to server.\n");
    printf("delete [filename]   - Deletes fiel from server.\n");
    printf("ls                  - Lists files on server.\n");
    printf("exit                - Close the server.\n");

    // Main command loop
    while (1)
    {
        // get a command from the user
        memset(&buf, 0, BUFSIZE);

        printf("enter a command:\n");
        fgets(buf, BUFSIZE, stdin);

        // send the message to the server
        n = sendto_reliable(sockfd, buf, strlen(buf), &serveraddr, &serverlen);
        
        if (n < 0)
        {
            close(sockfd);
            error("ERROR in sendto");
        }

        // Check which command to execute
        char* command;
        char* filename;

        command = strtok(strtok(buf, "\n"), " ");
        filename = strtok(NULL, "\n");

        if (strcmp(command, "get") == 0)
        {
            get_file(sockfd, &serveraddr, &serverlen, filename);
        }
        else if (strcmp(command, "put") == 0)
        {
            put_file(sockfd, &serveraddr, &serverlen, filename);
        }
        else if (strcmp(command, "delete") == 0)
        {
            delete_file(sockfd, &serveraddr, &serverlen, filename);
        }
        else if (strcmp(command, "ls") == 0)
        {
            ls_files(sockfd, &serveraddr, &serverlen);
        }
        else if (strcmp(command, "exit") == 0)
        {
            break;
        }
        else
        {
            printf("INVALID COMMAND\n");
        }

        printf("\n");
    }

    printf("Closing socket connection...\n");
    close(sockfd);
    return 0;
}

void get_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename)
{
    printf("Creating file %s...\n", filename);

    // Create new file and write as binary
    FILE* file = fopen(filename, "wb");

    if (file == NULL)
    {
        warning("ERROR opening file");
        fclose(file);
        return;
    }
    
    int err = 0;
    char buf[BUFSIZE];

    // Write file in chunks
    int n = 0;
    while (1)
    {
        memset(&buf, 0, BUFSIZE);

        n = recvfrom_reliable(sockfd, buf, BUFSIZE, serveraddr, serverlen);

        if (n < 0)
        {
            warning("ERROR receiving file");
            err = 1;
            break;
        }

        if (strcmp(buf, "FIN") == 0)
            break;
        
        if((n = fwrite(buf, 1, n, file)) < 0)
        {
            warning("ERROR writing file");
            break;
        }
    }

    fclose(file);

    printf("File write completed.\n");
}

void put_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename)
{
    printf("Opening file %s...\n", filename);

    // Open the file for reading binary contents
    FILE* file = fopen(filename, "rb");

    if (file == NULL)
    {
        warning("ERROR opening file");
        fclose(file);
        return;
    }

    char buf[BUFSIZE];
    
    // Send file in chunks
    int n = 0;
    while ((n = fread(buf, 1, BUFSIZE, file)) > 0) 
    {
        if (sendto_reliable(sockfd, buf, n, serveraddr, serverlen) < 0)
        {
            warning("ERROR sending file");
            break;
        }
    }

    // Send packet stating completion
    if (sendto_reliable(sockfd, "FIN", 3, serveraddr, serverlen) < 0)
    {
        warning("ERROR sending fin");
    }

    fclose(file);

    memset(&buf, 0, BUFSIZE);

    if (recvfrom_reliable(sockfd, buf, BUFSIZE, serveraddr, serverlen) < 0)
    {
        warning("ERROR receiving fail state");
        return;
    }

    if (strcmp(buf, "SUCCESS") == 0)
        printf("File transfer complete.\n");
    else
        printf("File transfer failed.\n");

}

void delete_file(int sockfd, struct sockaddr_in* serveraddr, int* serverlen, char* filename)
{
    char buf[BUFSIZE];

    memset(&buf, 0, BUFSIZE);

    if (recvfrom_reliable(sockfd, buf, BUFSIZE, serveraddr, serverlen) < 0)
    {
        warning("ERROR receiving fail state");
        return;
    }

    if (strcmp(buf, "SUCCESS") == 0)
        printf("File deletion complete.\n");
    else
        printf("File deletion failed.\n");
}

void ls_files(int sockfd, struct sockaddr_in* serveraddr, int* serverlen)
{
    char buf[BUFSIZE];

    printf("Available files from server:\n");

    while (1)
    {
        memset(&buf, 0, BUFSIZE);

        if (recvfrom_reliable(sockfd, buf, BUFSIZE, serveraddr, serverlen) < 0)
        {
            warning("ERROR receiving file name");
            continue;
        }

        if (strcmp(buf, "FIN") == 0)
            break;

        printf("%s\n", buf);
    }
}
