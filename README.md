# database
Multithreaded Database Server and Client


## Overview:
This is a web-based TCP database server and client suite. TCP is used because multiple clients will be working on the dataset at the same time, 
and the order of push and pull requests must be known to the database server.

This database employs hand-over-hand fine-grain locking of BST based database hosted on web server. It is 
able to handle read, write, query requests from arbitrary number of clients and handles individual client
signals without terminating server listener thread. Tested with upwards of 300 clients running simultaneously.

## What each file contains:
server.c creates a signal handling thread, a listener thread that listens for incoming connections, and reads user input. 
the server must be started with a port number as the first argument, and that port number will be bound to a socket that 
clients will connect to. Using the user input, actions will be made depending on what the user wants. s g p are the valid
commands which will be read. s will stop client activity, g will resume, and p will print, if p has an argument, it’ll 
print to that file.

db.c contains the functionality for a multithread safe database that implements a binary tree structure to maintain data. Fine grain locking is implemented with hand over hand locking to ensure that data does not get clobbered when different threads come in to edit. db add, remove, and search are the functions that were edited, and they all use hand over hand.

## Q&A about the project


###### 1. How many threads with this functionality should be running on the server at any given time?
	(1) Just one. You should only call start_listener one time--when you start up your server--so there's no reason for there to be multiple threads with that functionality. 
	(2) As many as there are clients to listen to. Since start_listener is the only thing that calls listener, each time you want to connect to a new client's port, you need to call start_listener, which in turn makes a new thread. 


###### 2. In the listener function, what do each of the following fields of the sockaddr_in struct represent: sin_family, sin_port, and sin_addr.s_addr?
	(1) sin_family contains a code for the address family, and it is always set to AF_INET, as per the man pages for the struct sockaddr_in. 
	(2) The second field of serv_addr is unsigned short sin_port, which contains the port number. However, instead of simply copying the port number to this field, it is necessary to convert this to network byte order using the function htons() which converts a port number in host byte order to a port number in network byte order.
	(3) sin_addr.s_addr field contains the IP address of the host. For server code, this will always be the IP address of the machine on which the server is running, and there is a symbolic constant INADDR_ANY which gets this address.

###### 3. What is the purpose of each of the following function calls in listener: socket, bind, listen, and accept? You should explain what each call does and what the effect would be if it were not called.
	[SOCKET] The socket() system call creates a new socket. It takes three arguments. The first is the address domain of the socket. The second argument is the type of socket. The two symbolic constants are SOCK_STREAM and SOCK_DGRAM. The third argument is the protocol. It is set to zero for our purposes. The socket system call returns an entry into the file descriptor table. This value is used for all subsequent references to this socket. If this call is not made, then lsock will not be set, making bind unable to bind a socket to an address, which is the address of the current host and port number on which the server will run.


	[BIND] The bind() system call binds a socket to an address, in this case the address of the current host and port number on which the server will run. It takes three arguments, the socket file descriptor, the address to which is bound, and the size of the address to which it is bound. The second argument is a pointer to a structure of type sockaddr. If bind is not called, the incoming socket will not be bound to the address of the server, listen will not direct the incoming data to the correct address, as the socket will not be properly bound.

	[LISTEN] The listen system call allows the process to listen on the socket for connections. The first argument is the socket file descriptor, and the second is the size of the backlog queue, i.e., the number of connections that can be waiting while the process is handling a particular connection. If listen were not called, a backlog of connections will not be generated, so when accept is called later, it will not be accepting anything at all. 

	[ACCEPT] The accept() system call causes the process to do nothing until a client connects to the server. Thus, it wakes up the process when a connection from a client has been successfully established. It relies on listen. It returns a new file descriptor, and all communication on this connection should be done using the new file descriptor. The second argument is a reference pointer to the address of the client on the other end of the connection, and the third argument is the size of this structure. If accept is not called, you cannot connect multiple clients onto the server. 

###### 4. Which protocol (TCP or UDP) is used for communication? Why is this protocol used? (Hint: see line 35 in comm.c)
	TCP is used because reliability is very important in a database. If connection is interrupted, the server is expected let the client know that there was some sort of error. Also, if datagrams are used instead, the order in which instructions are sent will not be guarenteed, which means that adding a node, then deleting it, may result in undefined behavior beacuse you can't delete a node before it's been created. 

###### 5. Describe what comm_serve does. How are the response and command parameters used? What would happen if the stream pointed to by cxstr were closed?
	If there is a response given as the input: comm_serve writes the input response string into the input file, and does error checking on the response. Then it reads from the command pointer exactly BUFLEN characters, and puts all of that to the file as well.
	If there is no response to write, it just does the second step. 

###### 6. Describe, in detail, what happens during each iteration of the while loop in the listener function. Be sure to include explanations of all significant function calls.
	The listen call from before the while loop generates a backlog of clients that are trying to connect. The purpose of this while loop is to handle each of these clients. First, a new sockaddr_in struct is created, representing the client's address. Next, the client's socket ID (file descriptor) is obtained through a call to accept. It needs input lsock because it checks to see the queue of pending connections for the listening socket. Now that we've obtained the client socket and done the necessary error checks, we've successfully connected to the client, so a success message is printed. Next, we set cxstr to equal to the file that we've just created (using the file descriptor of the client we obtained from accept.) If the opening of this file goes through without error, server is called on the file that has just been opened. 
