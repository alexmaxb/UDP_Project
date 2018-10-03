// udp server coded by Alex Burnley
// implements a reliable server side using UDP


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <dirent.h>

#define MAXBUFSIZE 100
#define MESSAGE_SIZE 100
#define MESSAGE_HEADER_SIZE 8 // size of int plus size of int. header, then sum of data for error correction
#define TIMEOUT_SEND 200000 // timeout while sending packets in microseconds
#define TIMEOUT_WAIT 5 // timeout while waiting for packets in seconds
#define MAXATTEMPTS_SEND 100
#define MAXATTEMPTS_WAIT 1 

int receiveCommand(void *buffer, int sock, struct sockaddr_in * remote);
int interpret(char *command, char *filename);
int executeGet(char *filename, int sock, struct sockaddr_in * remote);
int sendMessage(void *message, int size, int expectedResponse, int sock, struct sockaddr_in *remote);
int executePut(char * filename, int sock, struct sockaddr_in *remote);
int receiveMessage(void * buffer, int size, int expectedHeader, 
					void *responseToIncorrect, int messageSize, int sock, struct sockaddr_in * remote);
int executels(int sock, struct sockaddr_in *remote);
socklen_t remote_length;


int main (int argc, char * argv[] )
{


	int sock;                           
	struct sockaddr_in sin, remote;    
	char buffer[MAXBUFSIZE];           
	int run; // should the server continue running
	char *filename;
	int commandcode;
	int n;

	if (argc != 2)
	{
		printf ("USAGE:  <port>\n");
		exit(1);
	}

	//set data for the server
	bzero(&sin,sizeof(sin));                 
	sin.sin_family = AF_INET;                  
	sin.sin_port = htons(atoi(argv[1]));       
	sin.sin_addr.s_addr = INADDR_ANY;           


	//create socket
	if ((sock = socket(AF_INET,SOCK_DGRAM,0)) < 0)
	{
		printf("unable to create socket");
		return 0;
	}

	remote_length = sizeof(remote);

	//bind the socket
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("unable to bind socket\n");
		return 0;
	}

	// loop the server while run is true to take commands from the client. exit will set it to false. 
	run = 1;
	while(run){
		//waits for an incoming message
		bzero(buffer,MAXBUFSIZE);
		receiveCommand(buffer,sock,&remote);
		filename = malloc(MAXBUFSIZE);

		// use interpret to figure out which command was sent
		commandcode = interpret((char *)buffer,filename);
		switch(commandcode){
			case 1:
			// get
				executeGet(filename,sock,&remote);
				break;
			case 2:
			//put
				executePut(filename, sock, &remote);
				break;
			case 3:
			// delete
				n = -1;
				sendto(sock,&n,sizeof(int),0,(struct sockaddr *)&remote, 
	 					(socklen_t ) remote_length);
				if (remove(filename) == 0) 
      			{
      				printf("Deleted %s\n",filename);
      			} 
   				else {
   					printf("Couldn't delete %s\n",filename); 
   				}
				break;
			case 4:
			 // ls
				executels(sock, &remote);
				break;
			case 5:
			 // exit
				n = -1;
				sendto(sock,&n,sizeof(int),0,(struct sockaddr *)&remote, 
	 				(socklen_t ) remote_length);
				run = 0;
				break;
			default:
				run = 1;
				sendto(sock,buffer,MAXBUFSIZE,0,(struct sockaddr *)&remote, 
	 				(socklen_t ) remote_length);
				//send message to client about invalid command

		}

	}
	free(filename);
	

	close(sock);
	return 0;
}

// recieve the command from the client, placing it into the buffer
int receiveCommand(void *buffer, int sock, struct sockaddr_in * remote)
{
	
	recvfrom(sock,buffer,MAXBUFSIZE,MSG_WAITALL, 
		(struct sockaddr *)remote, (socklen_t *) &remote_length );
	printf("Received: %s\n",(char*)buffer);
	return 0;
}

// interpret the command sent by the client, return an int that represents the command
// ints representing commands are as follows: 1:get, 2:put, 3:delete, 4:ls, 5:exit, 6:[other]
// if the command provides a filename, the filename is placed into char buffer named filename
int interpret(char *command, char *filename)
{
	int n;
	int found;
	found = 0;
	n = 0;
	char get[] = "get";
	char put[] = "put";
	char delete[] = "delete";
	char ls[] = "ls";
	char exit[] = "exit";
	while(!found)
	{
		if(*(command+n) == ' ')
			*(command+n) = '\0';
		if(*(command+n) == '\0')
			found = 1;
		n++;
	}
	if(!strcmp(command,get))
		{
			strcpy(filename,command+n);
			return 1;
		}
	if(!strcmp(command,put))
		{
			strcpy(filename,command+n);
			return 2;
		}
	if(!strcmp(command,delete))
		{
			strcpy(filename,command+n);
			return 3;
		}
	if(!strcmp(command,ls))
		return 4;
	if(!strcmp(command,exit))
		return 5;
	return 6;

}

// execute the get command. read file, if it exists, then separate it into lines and send them to client
// this implements stop and wait to reliably send packets to client
int executeGet(char *filename, int sock, struct sockaddr_in * remote)
{
	FILE *file;
	file = fopen(filename,"rb");
	void *line;

	int currentPacket;
	//int i;
	int size;

	if(file == NULL){
		//bad input file name
		printf("Bad file input: %s\n",filename);
		line = malloc(MESSAGE_SIZE);
		*((int *)line) = -1;
		*((int *)(line+sizeof(int))) = -1;
		sendMessage(line,MESSAGE_HEADER_SIZE,-1,sock,remote);
		fclose(file);
		free(line);
		return 0;

	}

	line = malloc(MESSAGE_SIZE);

	// send message to client about size of program. use format -1size, that is two ints one after the other
	fseek(file,0L,SEEK_END);
	size = ftell(file);
	rewind(file);
	*((int *)line) = -1;
	*((int *)(line+sizeof(int))) = size;
	if(sendMessage(line,MESSAGE_HEADER_SIZE,-1,sock,remote) == -1)
	{
		fclose(file);
		free(line);
		return 0;
	}
	


	//
	currentPacket = 0;
	// read one "line" of data from file, send to client, wait for response confirming, then move to next line
	while(fread(line+MESSAGE_HEADER_SIZE,1, MESSAGE_SIZE - MESSAGE_HEADER_SIZE , file))
	{
		*((int *)(line)) = currentPacket;

		if(sendMessage(line,MESSAGE_SIZE,currentPacket,sock,remote) == -1)
		{
			printf("Failed to confirm delivery to Client. Abandoning Operation\n");
			fclose(file);
			free(line);
			return 0;
		}
		
		printf("\rSending: %.0f %%", 
			((float)currentPacket + 1)/size * 100 * (MESSAGE_SIZE - MESSAGE_HEADER_SIZE));
		fflush(stdout);
		currentPacket++;
	}

	printf("\nDone Sending File\n");
	fflush(stdout);
	fclose(file);
	free(line);
	return 0;
}

// send a message to client, continue sending until it receives a confirmation message from client.
// If client has not responded after 200 timeouts, returns -1 to tell calling function about failed
// delivery
// this implements most of the stop and wait
int sendMessage(void *message, int size, int expectedResponse, int sock, struct sockaddr_in *remote)
{
	fd_set readfds;
	struct timeval timeout;
	int confirmed;
	int response;
	int packetsSent;

	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = TIMEOUT_SEND;

	//send data
	sendto(sock,message,size,0,(struct sockaddr *)remote, 
	 			(socklen_t ) remote_length);

	packetsSent = 0;
	confirmed = 0;
	while(!confirmed)
	{
		// set data for the timer
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = TIMEOUT_SEND;
		// select looks for data ready in socket, and has timeout. if timeout, resend data and reset timer
		if(select(FD_SETSIZE,&readfds, (void*)0, (void*)0, &timeout) == 0)
		{
			
			if(packetsSent > MAXATTEMPTS_SEND)
			{
				printf("Client not Responding. Asumming Client is offline\n");
				return -1;
			}
			sendto(sock,message,size,0,(struct sockaddr *)remote, 
	 			(socklen_t ) remote_length);
			packetsSent++;
		}
		else{
			// get data and check if it is an ACK for this message
			recvfrom(sock,&response,sizeof(int),MSG_WAITALL,
				(struct sockaddr *)remote, (socklen_t *) &remote_length);
			if(response == expectedResponse)
				{
					confirmed = 1;
				}
		}
	}
	return 0;

}




// this function implements the reliable protocols for receiving a file from the client.
// The command is first received from the client, then the server responds with a packet with header -2.
// the client then sends packet -1, which contains the number of packets for the file
// after this, the client sends 
int executePut(char * filename, int sock, struct sockaddr_in *remote)
{
	FILE * file;
	void *buffer;
	int lastPacketReceived;
	int size;
	int numPackets;
	int i;

	

	file = fopen(filename, "wb");
	buffer = malloc(MESSAGE_SIZE);

	lastPacketReceived = -2;

	// send a packet confirming delivery of command
	sendto(sock,&lastPacketReceived,sizeof(int),0,(struct sockaddr *)remote, 
			(socklen_t ) remote_length);
	// wait for data from client about size of the file
	if(receiveMessage(buffer,MESSAGE_HEADER_SIZE,-1, &lastPacketReceived,sizeof(int), sock, remote) == -1)
	{
		printf("Didn't receive file size info from server. Abandoning operation\n");
		fclose(file);
		free(buffer);
		return 0;
	}
	lastPacketReceived = *((int*)buffer);
	size = *((int*)(buffer + sizeof(int)));
	if(size % (MESSAGE_SIZE - MESSAGE_HEADER_SIZE) > 0)
		numPackets = size / (MESSAGE_SIZE - MESSAGE_HEADER_SIZE) + 1;
		else numPackets = size / (MESSAGE_SIZE - MESSAGE_HEADER_SIZE);
	printf("File of size: %d\n", size);

	i = 0;
	// loop until all the correct packets are received
	while(i < numPackets)
	{
		//make call to receive message, which uses stop and wait
		
		if(receiveMessage(buffer,MESSAGE_SIZE,i,&lastPacketReceived,sizeof(int),sock,remote) == -1)
		{
			printf("Failed to retrieve file from server\n");
			fclose(file);
			free(buffer);
			return 0;
		}
		lastPacketReceived = *((int*)buffer);
		printf("\rTransfering: %.0f %%", ((float)lastPacketReceived + 1)/numPackets * 100);
		fflush(stdout);
		i++;


		//write received data to file
		if(i == numPackets)
			fwrite(buffer+MESSAGE_HEADER_SIZE, 1, size % (MESSAGE_SIZE - MESSAGE_HEADER_SIZE), file);
			else fwrite(buffer+MESSAGE_HEADER_SIZE, 1, MESSAGE_SIZE - MESSAGE_HEADER_SIZE, file);

	}
	printf("\nDone Receiving File\n");
	fflush(stdout);

	fclose(file);
	free(buffer);
	return 0;
}

// This function guarentees delivery of a packet with header 'expectedHeader'. it places the data from
// this packet into buffer. it also uses 'responseToIncorrect' as a message to send to the server
// if the wrong packet is received or if it times out receiving the file. Generally, this response packet 
// is the header of the last packet sent in order to account for packet loss of ACKs. However, the size
// needs to be specified so that this function can be used to resend the command if the server does not
// respond to the command that was sent
int receiveMessage(void * buffer, int size, int expectedHeader, 
					void *responseToIncorrect, int messageSize, int sock, struct sockaddr_in * remote)
{
	//receive data from server - in while loop to wait until it receives the correct data 
	//needs to send message to the server with the last packet number it recieved
	// in this would be the error correction if it needs to be implemented - TODO
	int received;
	int messageNumberReceived; 
	int sendCount;
	fd_set readfds;
	struct timeval timeout;


	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	timeout.tv_sec = TIMEOUT_WAIT;
	timeout.tv_usec = 0;

	sendCount = 0;
	received = 0;
	while(!received)
	{
		//set data for the timer
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timeout.tv_sec = TIMEOUT_WAIT;
		timeout.tv_usec = 0;
		// wait for data in socket
		if(select(FD_SETSIZE,&readfds,(void*)0,(void*)0,&timeout) == 0)
		{
			if(sendCount >= MAXATTEMPTS_WAIT)
			{	
				printf("Timeout: Giving up receiving message\n");
				return -1;
			}
			else {
				sendCount++;
				sendto(sock,responseToIncorrect,messageSize,0,(struct sockaddr *)remote, 
						(socklen_t ) remote_length);
			}
		}
		else{
			// get data from socket and check if it is the packet it is waiting for
			recvfrom(sock,buffer,size,MSG_WAITALL,
				(struct sockaddr *)remote, (socklen_t *) &remote_length);
			messageNumberReceived = *((int *) buffer);
			if(messageNumberReceived == expectedHeader)
			{
				
				sendto(sock,&expectedHeader,sizeof(int),0,(struct sockaddr *)remote, 
 						(socklen_t ) remote_length);
				received = 1;
			}
			else {

				sendto(sock,responseToIncorrect,messageSize,0,(struct sockaddr *)remote, 
						(socklen_t ) remote_length);
			}
		}
	}
	return 0;
}

// implements the ls command. sends a packet for each file that it finds. it tells the client how many
// packets it will send before sending any filenames.
int executels(int sock, struct sockaddr_in *remote)
{
	void * buffer;
	int size;
	int currentPacket;
	struct dirent *de;
	int loc;
	DIR *dir = opendir(".");


	//count the number of files in the directory

	size = 0;
	while((de = readdir(dir)) != 0)
	{
		size++;
	}

	closedir(dir);

	buffer = malloc(MESSAGE_SIZE);
	loc = 0;
	*((int *) buffer) = -1;
	*((int *)(buffer+sizeof(int))) = size;

	// send message to client about number of packets that will be sent
	if(sendMessage(buffer,MESSAGE_HEADER_SIZE,-1,sock,remote) == -1)
	{
		free(buffer);
		return 0;
	}

	// reopen directory, send each name individually in a packet

	currentPacket = 0;
	dir = opendir(".");
	while((de = readdir(dir)) != 0)
	{
		*((int *) buffer) = currentPacket;
		strcpy(buffer+MESSAGE_HEADER_SIZE,de->d_name);
		if(sendMessage(buffer,MESSAGE_SIZE,currentPacket,sock,remote) == -1)
		{
			printf("Failed to confirm delivery to Client. Abandoning Operation\n");
			free(buffer);
			return 0;
		}
		currentPacket++;
	}
	closedir(dir);

	return 0;
}

