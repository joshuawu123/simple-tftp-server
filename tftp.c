#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAXBUFFER 516 //The maximum packet size
#define tries 10      //number of attempts to send before giving up

//Gets the filename provided in a WRQ or RRQ packet by the client
//Arg: buffer - Thebuffer containing the RRQ or WRQ packet in its' entirety
//Return: A pointer to the start of the filename in 'buffer'
char *getFileName(char *buffer)
{
    char *filename = buffer + 2;
    return filename;
}

int main(int argc, char **argv)
{
    //Tracks if we are in the parent or child process
    int pid = 1;
    unsigned short int opcode = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    //Variables to hold the start and end of our range of usable ports
    int start_port = 0;
    int end_port = 0;

    //Points to filenames to be sent to/receieved from users
    char *filename = NULL;
    FILE *file = NULL;

    //Variables for use in creating packets to send to clients
    int num, attempts = 0;

    //Get the user provided start and end port numbers from the command line
    if (argv[1] != NULL)
    {
        start_port = atoi(argv[1]);
    }
    if (argv[2] != NULL)
    {
        end_port = atoi(argv[2]);
    }

    //Start on the user-provided start port
    int current_port = start_port;

    //Print startup output
    printf("MAIN: Started server\n");
    printf("MAIN: Listening for UDP datagrams on port: %d\n", start_port);

    //Create socket for incoming UDP datagrams
    int udp_sd = socket(PF_INET, SOCK_DGRAM, 0);
    if (udp_sd == -1)
    {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    //Setup for the socket
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(start_port);
    int len = sizeof(server);

    //Bind the socket to the given port number
    if (bind(udp_sd, (struct sockaddr *)&server, len) == -1)
    {
        perror("bind() failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in udp_Client;
    int udp_fromlen = sizeof(udp_Client);
    int n = 0;
    //Prepare our buffers for received packets and packets to be sent
    char buffer[MAXBUFFER];
    char *sendBuffer;
    while (1)
    {
        //server.sin_port = htons(start_port);
        opcode = 0;
        if (current_port >= (end_port + 1))
        {
            perror("no more ports");
            return EXIT_FAILURE;
        }
        //Make sure our read-in buffer is clear before receiving a new packet
        bzero(buffer, MAXBUFFER);

        //Read in a packet from the client
        n = recvfrom(udp_sd, buffer, MAXBUFFER, 0, (struct sockaddr *)&udp_Client, (socklen_t *)&udp_fromlen);
        if (n == -1)
        {
            perror("recvfrom() failed");
        }
        //Bit-mask to get the opcode of the received packet
        opcode = ((buffer[0] << 8) | buffer[1]);
        printf("opcode: %d\n", opcode);
        //What the parent (server) receives
        if (opcode == 1)
        {
            //Move on to the next port in our range, then create a child process to handle the latest client
            current_port++;
            pid = fork();
            if (pid == 0) //handle everything in the child
            {
                printf("Received RRQ request on port %d\n", current_port);
                //RRQ
                //Send a DATA or ERROR packet
                //Do setup to shift the client's connection to this port
                int new_tid = current_port;
                udp_sd = socket(PF_INET, SOCK_DGRAM, 0);
                struct sockaddr_in server;
                server.sin_family = AF_INET;
                server.sin_addr.s_addr = htonl(INADDR_ANY);
                server.sin_port = htons(new_tid);
                len = sizeof(server);
                bind(udp_sd, (struct sockaddr *)&server, len);

                //Get the name of the file being requested by the client
                filename = getFileName(buffer);
                printf("filename: %s\n", filename);
                file = fopen(filename, "r");
                if (file == NULL)
                { //File not found, or error in opening file. Send ERROR packet
                    printf("Not found\n");
                    sendBuffer = (char *)malloc(5 + (sizeof(char) * strlen("Error: File not found\n")));
                    bzero(sendBuffer, MAXBUFFER);
                    sendBuffer[0] = (((5) >> 8));
                    sendBuffer[1] = (5);
                    sendBuffer[2] = (((1) >> 8));
                    sendBuffer[3] = (1);
                    strcpy(sendBuffer + 4, "Error: File not found\n\0");
                    n = sendto(udp_sd, sendBuffer, 5 + (strlen("Error: File not found\n")), 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                    if (n == 0)
                    {
                        printf("Error in sending ERROR packet in response to RRQ\n");
                    }
                    exit(1);
                }
                else
                { //File found, send first DATA packet
                    printf("Found\n");
                    //Buffer to hold the data to be sent in this packet
                    char data[512];
                    bzero(data, 512);
                    //Holds the block number of the latest ACK packet we've received
                    unsigned short int ack_block;
                    sendBuffer = (char *)malloc(516);
                    unsigned short int block = 0;

                    while (1)
                    {
                        int bytes_read = fread(data, 1, sizeof(data), file);
                        block += 1;

                        //Attempt to send the packet 10 times. If it is not received, the connection times out
                        for (int i = 0; i < tries; i++)
                        {
                            //Prepare the opcode and block number of the DATA packet we are about to send
                            bzero(sendBuffer, MAXBUFFER);
                            sendBuffer[0] = (((3) >> 8));
                            sendBuffer[1] = (3);
                            sendBuffer[2] = (((block) >> 8));
                            sendBuffer[3] = (block);
                            //Put the current chunk of data from the file into our packet
                            strcpy(sendBuffer + 4, data);
                            //Send the DATA packet
                            num = sendto(udp_sd, sendBuffer, 4 + bytes_read, 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                            if (num < 0) //error: send failed
                            {
                                perror("send failed");
                                exit(1);
                            }
                            printf("sent\n");
                            bzero(buffer, MAXBUFFER);
                            num = recvfrom(udp_sd, buffer, MAXBUFFER, 0, (struct sockaddr *)&udp_Client, (socklen_t *)&udp_fromlen);
                            printf("received\n");
                            //should receive an ACK

                            if (num < 4 && num >= 0) //error: wrong size
                            {
                                perror("wrong size");
                                exit(1);
                            }
                            else if (num == 4) //found
                            {
                                printf("4 size packet received\n");
                                break;
                            }
                            attempts++;
                            sleep(1);
                        }
                        //Get the opcode and block number of the ACK packet we (presumably) just read in
                        opcode = ((buffer[0] << 8 | buffer[1]));
                        ack_block = ((buffer[2] << 8 | buffer[3]));
                        printf("%d, %d\n", opcode, ack_block);
                        if (attempts >= tries) //if it failed everytime
                        {
                            perror("timed out\n");
                            exit(1);
                        }
                        else if (opcode != 4) //not an ACK
                        {
                            //sendBuffer = (char *)realloc(5 + (sizeof(char) * strlen("Error: Illegal TFTP operation\n")));
                            bzero(sendBuffer, MAXBUFFER);
                            sendBuffer[0] = (((5) >> 8));
                            sendBuffer[1] = (5);
                            sendBuffer[2] = (((4) >> 8));
                            sendBuffer[3] = (4);
                            strcpy(sendBuffer + 4, "Error: Illegal TFTP operation\n\0");
                            n = sendto(udp_sd, sendBuffer, 5 + (strlen("Error: Illegal TFTP operation\n")), 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                            perror("wrong type\n");
                            exit(1);
                        }
                        else if (opcode == 5) //error
                        {
                            perror("error received\n");
                            exit(1);
                        }
                        else if (ack_block != block) //wrong block #
                        {
                            perror("Incorrect block number\n");
                            exit(1);
                        }
                        if (bytes_read < 512) //end of file
                        {
                            printf("DONE\n");
                            exit(1);
                        }
                    }
                }
            }
        }
        else if (opcode == 2)
        {
            current_port++;
            pid = fork();
            if (pid == 0)
            {
                int new_tid = current_port;
                udp_sd = socket(PF_INET, SOCK_DGRAM, 0);
                struct sockaddr_in server;
                server.sin_family = AF_INET;
                server.sin_addr.s_addr = htonl(INADDR_ANY);
                server.sin_port = htons(new_tid);
                len = sizeof(server);
                bind(udp_sd, (struct sockaddr *)&server, len);

                //WRQ
                //Check if file already exists
                printf("WRQ received on port %d\n", current_port);
                filename = getFileName(buffer);
                file = fopen(filename, "r");
                if (file != NULL)
                {
                    //File already exists, send an ERROR packet
                    sendBuffer = (char *)malloc(5 + (sizeof(char) * strlen("Error: File already exists\n")));
                    bzero(sendBuffer, MAXBUFFER);
                    sendBuffer[0] = (((5) >> 8));
                    sendBuffer[1] = (5);
                    sendBuffer[2] = (((6) >> 8));
                    sendBuffer[3] = (6);
                    strcpy(sendBuffer + 4, "Error: File already exists\n\0");
                    n = sendto(udp_sd, sendBuffer, 5 + (strlen("Error: File already exists\n")), 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                    if (n == 0)
                    {
                        perror("Error in sending ERROR packet to client\n");
                    }
                    exit(1);
                }

                //Create a new file with the provided name
                file = fopen(filename, "w");
                //Begin writing to file
                if (file == NULL)
                {
                    printf("Attempting to write to an unknown file\n");
                    break;
                }
                //Send an ACK with a block number of 0
                sendBuffer = (char *)malloc(516);
                bzero(sendBuffer, MAXBUFFER);
                sendBuffer[0] = (((4) >> 8));
                sendBuffer[1] = (4);
                sendBuffer[2] = (((0) >> 8));
                sendBuffer[3] = (0);
                for (int i = 0; i < tries; i++)
                {
                    n = sendto(udp_sd, sendBuffer, 4, 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                    if (n < 0)
                    {
                        printf("Send of ACK packet failed\n");
                    }
                    bzero(buffer, MAXBUFFER);
                    //Looking to receive a DATA pakcet here
                    n = recvfrom(udp_sd, buffer, MAXBUFFER, 0, (struct sockaddr *)&udp_Client, (socklen_t *)&udp_fromlen);
                    if (n == 0)
                    {
                        //No DATA packet received, send the ACK again
                        attempts++;
                        sleep(1);
                        continue;
                    }
                    if (((buffer[0] << 8) | buffer[1]) == 3)
                    {
                        //DATA packet received, exit loop and begin writing to file
                        //printf("starting to write\n");
                        break;
                    }
                }
                if (attempts >= tries)
                {
                    perror("Connection Timed Out\n");
                    exit(1);
                }
                unsigned short int block = 0;
                while (1)
                {
                    //Write the new data to the file
                    printf("buffer: %s\n", buffer + 4);
                    fputs(buffer + 4, file);
                    if (strlen(buffer + 4) < 512)
                    {
                        printf("done\n");
                        block = ((buffer[2] << 8) | buffer[3]);
                        sendBuffer[0] = (((4) >> 8));
                        sendBuffer[1] = (4);
                        sendBuffer[2] = (((block) >> 8));
                        sendBuffer[3] = (block);
                        n = sendto(udp_sd, sendBuffer, 4, 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                        fclose(file);
                        exit(1); //finished
                    }

                    for (int i = 0; i < tries; i++)
                    {
                        //Send an ACK with the DATA packet's block number + 1
                        //opcode_ptr = (unsigned short int *)buffer;
                        block = ((buffer[2] << 8) | buffer[3]);
                        sendBuffer[0] = (((4) >> 8));
                        sendBuffer[1] = (4);
                        sendBuffer[2] = (((block) >> 8));
                        sendBuffer[3] = (block);
                        n = sendto(udp_sd, sendBuffer, 4, 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                        if (n < 0)
                        {
                            printf("Send of ACK packet failed\n");
                        }
                        //Looking to receive a DATA packet here
                        bzero(buffer, MAXBUFFER);
                        n = recvfrom(udp_sd, buffer, MAXBUFFER, 0, (struct sockaddr *)&udp_Client, (socklen_t *)&udp_fromlen);
                        if (n == 0)
                        {
                            //No DATA packet received, send the ACK again
                            attempts++;
                            sleep(1);
                            continue;
                        }
                        if (((buffer[0] << 8) | buffer[1]) == 3)
                        {
                            //DATA packet received, exit loop and write to file
                            break;
                        }
                        else
                        {
                            //Wrong type of packet received, show an error
                            bzero(sendBuffer, MAXBUFFER);
                            sendBuffer[0] = (((5) >> 8));
                            sendBuffer[1] = (5);
                            sendBuffer[2] = (((4) >> 8));
                            sendBuffer[3] = (4);
                            strcpy(sendBuffer + 4, "Error: Illegal TFTP operation\n\0");
                            n = sendto(udp_sd, sendBuffer, 5 + (strlen("Error: Illegal TFTP operation\n")), 0, (struct sockaddr *)&udp_Client, udp_fromlen);
                            perror("Wrong packet type received during WRQ\n");
                            exit(1);
                        }
                    }
                }
            }
        }
        if (pid == 0)
        {
            exit(1);
        }
    }
    return EXIT_SUCCESS;
}
