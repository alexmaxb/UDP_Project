// udp client coded by Alex Burnley
// implements a reliable client side using UDP


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
#include <errno.h>

#define MAXBUFSIZE 100
#define MAX_COMMAND_LENGTH 256
#define MESSAGE_SIZE 100
#define MESSAGE_HEADER_SIZE 8 // size of int plus size of int
#define MAXATTEMPTS_SEND 100
#define MAXATTEMPTS_WAIT 4
#define TIMEOUT_SEND 200000 // timeout while sending packets in microseconds
#define TIMEOUT_WAIT 2 // timeout while waiting for packets in seconds

int getFile(char * filename, int sock, struct sockaddr_in *remote, char * command);
int receiveMessage(void * buffer, int size, int expectedHeader, 
					void * responseToIncorrect,int messageSize, 
					int sock, struct sockaddr_in * remote);
int interpret(char *command, char *filename);
int sendFile(void *filename, int sock, struct sockaddr_in * remote, char * command);
int sendMessage(void *message, int size, int expectedResponse, int sock, struct sockaddr_in *remote);
int ls(int sock, struct sockaddr_in * remote, char * command);
socklen_t remote_length;




int main (int argc, char * argv[])
{

	int sock;                              
	char buffer[MAXBUFSIZE];
	int keepRunning;
	int commandcode;


	char *command;
	char *filename;
	struct sockaddr_in sin,remote; 
	if (argc < 3)
	{
		printf("USAGE:  <server_ip> <server_port>\n");
		exit(1);
	}

	// set data for the server
	bzero(&remote,sizeof(remote));            
	remote.sin_family = AF_INET;              
	remote.sin_port = htons(atoi(argv[2]));      
	remote.sin_addr.s_addr = inet_addr(argv[1]); 

	//set data for client socket and port
	bzero(&sin,sizeof(sin));          
	sin.sin_family = AF_INET;             
	sin.sin_port = htons(atoi("0"));      
	sin.sin_addr.s_addr = INADDR_ANY; 

	remote_length = sizeof(remote);

	// create the socket
	if ((sock = socket(AF_INET,SOCK_DGRAM,0)) < 0)
	{
		printf("unable to create socket");
		return 0;
	}

	// bind the socket
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("unable to bind socket\n");
		return 0;
	}

	

	command = malloc(MAX_COMMAND_LENGTH);
	filename = malloc(MAX_COMMAND_LENGTH);
	keepRunning = 1;


	// loop the client to take commands from the user
	while(keepRunning)
	{
		
		fgets(command,MAX_COMMAND_LENGTH,stdin);
		*(command+strlen(command)-1) = '\0';

		commandcode = interpret((char *)command,filename);


		switch(commandcode){
			case 1:
				getFile(filename,sock,&remote, command);
				break;
			case 2:
				sendFile(filename, sock, &remote, command);
				break;
			case 3:
				sendMessage(command, MAX_COMMAND_LENGTH, -1, sock, &remote);
				break;
			case 4:
				ls(sock,&remote,command);
				break;
			case 5:
				if(sendMessage(command, MAX_COMMAND_LENGTH, -1, sock, &remote) == 0)
					printf("Server has shut down\n");
				else printf("Server not responding. Either ACK was lost and it has shut down, \nor connection to server is not working.\n");
				fflush(stdout);
				
				break;
			default:
				sendto(sock,command,MAX_COMMAND_LENGTH,0,(struct sockaddr *)&remote, 
					(socklen_t ) remote_length);
				recvfrom(sock,command,MAX_COMMAND_LENGTH,MSG_WAITALL,
					(struct sockaddr *)&remote, (socklen_t *) &remote_length);
				printf("Command not understood: %s\n",command);
				//send message to client about invalid command
		}
		

	// Blocks till bytes are received

	
		//int addr_length = sizeof(struct sockaddr);
		bzero(buffer,MAXBUFSIZE);
		
	}
	free(command);
	close(sock);

}



// interpret the command sent by the client, return an int that represents the command
// ints representing commands are as follows: 1:get, 2:put, 3:delete, 4:ls, 5:exit, 6:[other]
// if the command provides a filename, the filename is placed into char buffer named filename
int interpret(char *command, char *filename)
{
	int n;
	int found;
	printf("interpretting command\n");
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



// this function implements the reliable protocols for receiving a file from the server.
// The command is first sent to the server, which then responds with a packet with header -1.
// this packet contains the total number of packets needed to transfer the file. if packet -1 is not
// received, a timeout occurs and the command is sent again. after packet -1, the server sends each packet
// for the file in order. the client uses receiveMessage to receive these messages. This function implements
// timeouts and resends to guarentee delivery of the packet
int getFile(char * filename, int sock, struct sockaddr_in *remote, char * command)
{
	FILE * file;
	void *buffer;
	int lastPacketReceived;
	int size;
	int i;
	int numPackets;

	

	file = fopen(filename, "wb");
	buffer = malloc(MESSAGE_SIZE);

	lastPacketReceived = -2;

	// send the command to the server
	sendto(sock,command,MAX_COMMAND_LENGTH,0,(struct sockaddr *)remote, 
			(socklen_t ) remote_length);

	// wait for a response from server. this function will resend command after a timeout if needed
	if(receiveMessage(buffer,MESSAGE_HEADER_SIZE,-1, command, MAX_COMMAND_LENGTH, sock, remote) == -1)
	{
		printf("Didn't receive file size info from server. Abandoning operation\n");
		fclose(file);
		free(buffer);
		return 0;
	}
	// interpret data from server. this is the header, which is -1, and the size of the file
	lastPacketReceived = *((int*)buffer);
	size = *((int*)(buffer + sizeof(int)));
	if(size % (MESSAGE_SIZE - MESSAGE_HEADER_SIZE) > 0)
		numPackets = size / (MESSAGE_SIZE - MESSAGE_HEADER_SIZE) + 1;
		else numPackets = size / (MESSAGE_SIZE - MESSAGE_HEADER_SIZE);
	// if the size provided by the server was -1, it means that the file doesn't exist
	if(size == -1)
	{
		printf("File does not exist\n");
		fclose(file);
		remove(filename);
		free(buffer);
		return 0;
	}
	printf("Number of Packets to transfer: %d\n", size);

	// loop receiving packets until all are successfully received. receiveMessage implements a 
	// stop and wait protocol that guarentees deliver of the packets.
	i = 0;
	while(i < numPackets)
	{
		//make call to receive message
		
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

	fclose(file);
	free(buffer);
	printf("\nDone Transfering File\n");
	fflush(stdout);
	return 0;
}

// This function guarentees delivery of a packet with header 'expectedHeader'. it places the data from
// this packet into buffer. it also uses 'responseToIncorrect' as a message to send to the server
// if the wrong packet is received or if it times out receiving the file. Generally, this response packet 
// is the header of the last packet sent in order to account for packet loss of ACKs. However, the size
// needs to be specified so that this function can be used to resend the command if the server does not
// respond to the command that was sent

// this function is an implementation of stop and wait. it sets a timer, and waits for a packet to be received.
// if it is not the packet it is waiting for, it will send an ACK saying what the last message it received
// was. This is responseToIncorrect. if the timer expires, it will resend responseToIncorrect and try again
// a few times, but will give up quickly if nothing is received.
int receiveMessage(void * buffer, int size, int expectedHeader, 
					void *responseToIncorrect, int messageSize, int sock, struct sockaddr_in * remote)
{
	//receive data from server - in while loop to wait until it receives the correct data 
	//needs to send message to the server with the last packet number it recieved
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
	// loop until the correct data is received
	while(!received)
	{
		//set data for timer that watches the socket
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timeout.tv_sec = TIMEOUT_WAIT;
		timeout.tv_usec = 0;
		if(select(FD_SETSIZE,&readfds,(void*)0,(void*)0,&timeout) == 0)
		{
			if(sendCount >= MAXATTEMPTS_WAIT)
			{	
				printf("Timeout: Giving up receiving message\n");
				return -1;
			}
			else {
				sendCount++;
				printf("Timeout: sending confirmation again\n");
				sendto(sock,responseToIncorrect,messageSize,0,(struct sockaddr *)remote, 
						(socklen_t ) remote_length);
			}
		}
		else{
			//received a message, so interpret it and determine if it is the correct one

			recvfrom(sock,buffer,size,MSG_WAITALL,
				(struct sockaddr *)remote, (socklen_t *) &remote_length);
			messageNumberReceived = *((int *) buffer); 

			if(messageNumberReceived == expectedHeader)
			{
				//send ACK to server to confirm delivery
				sendto(sock,&expectedHeader,sizeof(int),0,(struct sockaddr *)remote, 
 						(socklen_t ) remote_length);
				received = 1;
			}
			else {
				// send message that tells ther server what the last packet received was
				sendto(sock,responseToIncorrect,messageSize,0,(struct sockaddr *)remote, 
						(socklen_t ) remote_length);
			}
		}
	}
	return 0;
}



// execute the put command. read file, if it exists, then separate it into lines and send them to server
// this used the sendMessage function to send packets, which is an implementation of stop and wait to 
// guarantee delivery
int sendFile(void *filename, int sock, struct sockaddr_in * remote, char * command)
{
	FILE *file;
	file = fopen(filename,"rb");
	void *line;

//	int size;
	int currentPacket;
	//int sum;
	//int i;
	int size;

	if(file == NULL){
		//bad input file name
		printf("Bad file input: %s\n",filename);

		return 0;
		//TODO
	}
	//printf("executing Put function\n");

	line = malloc(MESSAGE_SIZE);

	// send command to server
	if(sendMessage(command,MAX_COMMAND_LENGTH,-2,sock,remote) == -1)
	{
		printf("Failed to deliver command to server\n");
		fclose(file);
		free(line);
		return 0;
	}

	// send message to server about size of program. the header is -1, then just an int which is the size
	// in bytes
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
	

	currentPacket = 0;

	// read one "line" of data from file, send to client, wait for response confirming, then move to next line
	while(fread(line+MESSAGE_HEADER_SIZE,1, MESSAGE_SIZE - MESSAGE_HEADER_SIZE , file))
	{
		// this is for error detection, however i never implemented it on
		// sum = 0;
		// for(i = 0; i < MESSAGE_SIZE - MESSAGE_HEADER_SIZE; i++)
		// {
		// 	sum += *((char *)(line + MESSAGE_HEADER_SIZE + i));
		// }
		// create a header for the packet being sent
		//*((int *)(line+sizeof(int))) = sum;
		*((int *)(line)) = currentPacket;

		//send the message
		if(sendMessage(line,MESSAGE_SIZE,currentPacket,sock,remote) == -1)
		{
			printf("Failed to confirm delivery to Client. Abandoning Operation\n");
			fclose(file);
			free(line);
			return 0;
		}
		printf("\rSending: %.0f %%", ((float)currentPacket + 1)/size * 100 * (MESSAGE_SIZE - MESSAGE_HEADER_SIZE));
		fflush(stdout);
		currentPacket++;
		
	}
	
	fclose(file);
	free(line);
	printf("\nDone Sending File\n");
	fflush(stdout);
	return 0;
}

// send a message to server, continue sending until it receives a confirmation message from server.
// If server has not responded after many timeouts, returns -1 to tell calling function about failed
// delivery. This implements stop and wait, which guarantees delivery of packets to the server
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
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = TIMEOUT_SEND;
		// select looks for data ready in socket, and has timeout. if timeout, resend data and reset timer
		if(select(FD_SETSIZE,&readfds, (void*)0, (void*)0, &timeout) == 0)
		{
			// give up after so many attempts
			if(packetsSent > MAXATTEMPTS_SEND)
			{
				printf("Server not Responding. Asumming Server is offline\n");
				return -1;
			}
			// send data again, then wait for response again
			sendto(sock,message,size,0,(struct sockaddr *)remote, 
	 			(socklen_t ) remote_length);
			packetsSent++;
		}
		else{
			//read data and interpret it. if it is the ACK for this packet, then end loop
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

// implements receiving the data for the ls command. This uses a stop and wait implementation where
// each filename is in a seperate packet.
int ls(int sock, struct sockaddr_in * remote, char * command)
{
	void * buffer;
	int lastPacketReceived;
	int currentPacket;
	int n; // number of packets to receive

	buffer = malloc(MESSAGE_SIZE);

	//send command to the server
	sendto(sock,command,MAX_COMMAND_LENGTH,0,(struct sockaddr *)remote, 
			(socklen_t ) remote_length);

	//wait for response from server
	if(receiveMessage(buffer,MESSAGE_HEADER_SIZE,-1, command, MAX_COMMAND_LENGTH, sock, remote) == -1)
	{
		printf("Didn't get info from server. Abandoning operation\n");
		free(buffer);
		return 0;
	}

	// interpret response from server. it contains the number of packets that it will send
	lastPacketReceived = *((int*)buffer);
	n = *((int*)(buffer + sizeof(int)));
	

	currentPacket = 0;
	//loop until all the packets have been received. receiveMessage does everything for receiving data,
	// and sends ACKs to server
	while(currentPacket < n)
	{
		if(receiveMessage(buffer,MESSAGE_SIZE,currentPacket,
			 &lastPacketReceived, sizeof(int), sock, remote) == -1)
		{
			printf("Didn't get info from server. Abandoning operation\n");
			free(buffer);
			return 0;
		}
		lastPacketReceived = *((int*)buffer);
		currentPacket++;

		// print data received to the user
		printf("%s ",((char*)buffer+MESSAGE_HEADER_SIZE));
		fflush(stdout);


	}
	printf("\n");
	free(buffer);
	return 0;
}

