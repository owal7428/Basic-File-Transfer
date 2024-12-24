// PA1 - uftp_server

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define SERVER_MAX_PACKET_RETRY 5
#define PACKET_TRANSFER_TIMEOUT_S 10

// Wrapper for perror
void error(char* msg) 
{
    perror(msg);
    exit(1);
}
void warning(char* msg) 
{
    perror(msg);
}

int sendto_reliable(int sockfd, const char* message, int message_size, struct sockaddr_in* clientaddr, int* clientlen)
{
    int n = 0;

    // Only try resending packet a certain number of times
    while (n < SERVER_MAX_PACKET_RETRY)
    {
        // Send the packet
        if (sendto(sockfd, message, message_size, 0, (struct sockaddr *) clientaddr, *clientlen) < 0)
            break;
        
        // Poll the client for an acknowledgment

        char buf[BUFSIZE];
        memset(&buf, 0, BUFSIZE);

        if (recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) clientaddr, (socklen_t *) clientlen) < 0)
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

int recvfrom_reliable(int sockfd, char* message, int message_size, struct sockaddr_in* clientaddr, int* clientlen)
{
    int n = 0;

    // Only try receiving packet a certain number of times
    while (n < SERVER_MAX_PACKET_RETRY)
    {
        int received = recvfrom(sockfd, message, message_size, 0, (struct sockaddr *) clientaddr, (socklen_t *) clientlen);
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
        if (sendto(sockfd, "ACK", 3, 0, (struct sockaddr *) clientaddr, *clientlen) < 0)
            break;
        
        return received;
    }

    return -1;
}

// Helper for sending acknowledgments
void send_success_state(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, int err)
{
    // Send acknowledgment/error
    if (err == 0)
    {
        if (sendto_reliable(sockfd, "SUCCESS", 7, clientaddr, clientlen) < 0)
                warning("ERROR sending fail state");
    }
    else
    {
        if (sendto_reliable(sockfd, "FAIL", 4, clientaddr, clientlen) < 0)
                warning("ERROR sending fail state");
    }
}


// Predefine command functions
void get_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename);
void put_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename);
void delete_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename);
void ls_files(int sockfd, struct sockaddr_in* clientaddr, int* clientlen);

int main(int argc, char **argv)
{
    int sockfd;                         // Socket
    int portnum;                        // Port to listen on
    int clientlen;                      // Byte size of client's address

    struct sockaddr_in serveraddr;      // Server addr
    struct sockaddr_in clientaddr;      // Client addr
    struct hostent *client;             // Client host info

    char *clientaddr_str;               // Dotted decimal host addr string
    int optval;                         // Flag value for setsockopt
    
    char buf[BUFSIZE];                  // Message buf
    int n;                              // Message byte size

    // Check command line arguments
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    portnum = atoi(argv[1]);

    // socket: create the parent socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets us rerun the server immediately after we kill it; 
    * otherwise we have to wait about 20 secs. Eliminates "ERROR on binding: Address already in use" error. */
    optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int)) < 0)
    {
        close(sockfd);
        error("ERROR in setsockopt");
    }

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

    // Build the server's Internet address
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short) portnum);

    // Bind socket with a port
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    {
        close(sockfd);
        error("ERROR on binding");
    }

    clientlen = sizeof(clientaddr);
  
    // Receive UDP datagram containing command from client
    while (1)
    {
        memset(&buf, 0, BUFSIZE);

        printf("Listening on port %d...\n", portnum);
        
        n = recvfrom_reliable(sockfd, buf, BUFSIZE, &clientaddr, &clientlen);

        if (n < 0)
        {
            close(sockfd);
            error("ERROR in recvfrom");
        }
        
        // Get information of client who sent datagram
        client = gethostbyaddr((const char *) &clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);

        if (client == NULL)
        {
            warning("ERROR on gethostbyaddr");
            continue;
        }
        
        // Convert sockaddr to IPv4 string
        clientaddr_str = inet_ntoa(clientaddr.sin_addr);

        if (clientaddr_str == NULL)
        {
            warning("ERROR on inet_ntoa");
            continue;
        }
        
        // Print the command given to the server
        printf("server received following command from %s (%s):\n", client->h_name, clientaddr_str);
        printf("%s\n", buf);

        // Check which command to execute
        char* command;
        char* filename;

        command = strtok(strtok(buf, "\n"), " ");
        filename = strtok(NULL, "\n");

        if (strcmp(command, "get") == 0)
        {
            get_file(sockfd, &clientaddr, &clientlen, filename);
        }
        else if (strcmp(command, "put") == 0)
        {
            put_file(sockfd, &clientaddr, &clientlen, filename);
        }
        else if (strcmp(command, "delete") == 0)
        {
            delete_file(sockfd, &clientaddr, &clientlen, filename);
        }
        else if (strcmp(command, "ls") == 0)
        {
            ls_files(sockfd, &clientaddr, &clientlen);
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

void get_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename)
{
    printf("Opening file %s...\n", filename);

    // Open the file for reading binary contents
    FILE* file = fopen(filename, "rb");

    if (file == NULL)
    {
        warning("ERROR opening file");
    }
    else
    {
        char buf[BUFSIZE];
        
        // Send file in chunks
        int n = 0;
        while ((n = fread(buf, 1, BUFSIZE, file)) > 0) 
        {
            if (sendto_reliable(sockfd, buf, n, clientaddr, clientlen) < 0)
            {
                warning("ERROR sending file");
                break;
            }
        }
        
        printf("File transfer complete.\n");
    }

    fclose(file);

    // Send packet stating completion
    if (sendto_reliable(sockfd, "FIN", 3, clientaddr, clientlen) < 0)
    {
        warning("ERROR sending fin");
    }
}

void put_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename)
{
    int err = 0;

    printf("Creating file %s...\n", filename);

    // Create new file and write as binary
    FILE* file = fopen(filename, "wb");

    if (file == NULL)
    {
        warning("ERROR opening file");
        fclose(file);
        err = 1;
    }
    else
    {
        char buf[BUFSIZE];

        // Write file in chunks
        int n = 0;
        while (1)
        {
            memset(&buf, 0, BUFSIZE);

            n = recvfrom_reliable(sockfd, buf, BUFSIZE, clientaddr, clientlen);

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

        printf("File write completed.\n");
    }

    fclose(file);

    send_success_state(sockfd, clientaddr, clientlen, err);
}

void delete_file(int sockfd, struct sockaddr_in* clientaddr, int* clientlen, char* filename)
{
    int err = 0;
    
    if (remove(filename) == 0)
        printf("%s was successfully removed.\n", filename);
    else
    {
        printf("%s does not exist.\n", filename);
        err = 1;
    }

    send_success_state(sockfd, clientaddr, clientlen, err);
}

void ls_files(int sockfd, struct sockaddr_in* clientaddr, int* clientlen)
{
    printf("Sending available files...\n");

    DIR* directory;
    struct dirent *dir;

    directory = opendir(".");

    char buf[BUFSIZE];

    if (!directory)
    {
        warning("ERROR opening directory");
    }
    else
    {
        while ((dir = readdir(directory)) != NULL)
        {
            if (dir->d_type != 0)
            {
                if (sendto_reliable(sockfd, dir->d_name, strlen(dir->d_name), clientaddr, clientlen) < 0)
                {
                    warning("ERROR sending file name");
                    break;
                }
            }
        }

        closedir(directory);
    }
    
    if (sendto_reliable(sockfd, "FIN", 3, clientaddr, clientlen) < 0)
    {
        warning("ERROR sending fin");
    }
}
