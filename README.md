Coded by Alex Burnley

To compile the code, 'make' can be used in both the client and server directories. 
the server can then be run with './server <port number>'. The client can be run with 
'./client <IP address> <port number>'. Make clean can be used to remove executables.

My code implements stop and wait to ensure delivery of packets between the client and 
server. When sending packets, a timer is sent and if an ACK is not received, the packet 
will be sent again. The receiver also sets a timer, but it is much longer and mainly used 
to give up after a time if nothing has been received. The commands get file, put file, ls, 
exit, and delete are all implemented. The reliable protocols work for all of these commands,
and should guarantee delivery for all packets.
