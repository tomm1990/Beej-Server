// server.c :
//

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAXDATA 100
#define PORTNUMBER	"3490"

//--------------------------------------------------------------------------------------
//convert compress file into uncompress file
//
void fnCompressToUncompress(char *fileName) 
{
	pid_t pid;
	if( (pid = fork()) == -1 ) 
		perror("fork");

	if(!pid) 
	{
		execlp(fileName, "gunzip",NULL );
		exit(0);
	} 
	else 
	{
		int status;
		waitpid(pid, &status, 0);
	}
}

//--------------------------------------------------------------------------------------
//load file from server to client
//
int fnLoadfile(int socketDescriptor, char *buffer, char *fileName) 
{
	int file = open(fileName, O_RDONLY);
	if( file == -1 ) 
	{
		perror("File not found");
		if( send(socketDescriptor, "ERROR", MAXDATA, 0 ) == -1) 
		{
			perror("send");
			return 0;
		}
		return 1;
	}

	//if zip file , we uncompress it
	if( strstr(fileName, ".gz") )
	{
		fnCompressToUncompress(fileName);
		char *pos = strstr(fileName, ".gz");
		*pos = 0;
		close(file);
		
		file = open(fileName, O_RDONLY);
		if (!file) 
		{
			perror("file");
			return 1;
		}
	}

	if (send(socketDescriptor, "OK", MAXDATA, 0) == -1) 
		perror("send");

	struct stat info;
	stat(fileName, &info);
	//send the file size
	if (send(socketDescriptor, &info.st_size, sizeof(off_t), 0) == -1) 
	{
		perror("send");
		close(file);
		return 0;
	}

	//read data from file and send data to client
	int flag;
	while( (flag = read(file, buffer, MAXDATA)) ) 
	{
		if( send(socketDescriptor, buffer, MAXDATA, 0) == -1 ) 
		{
			perror("send");
			close(file);
			return 0;
		}
	}

	close(file);
	printf("File is loaded successfully\n");
	return 1;
}

//--------------------------------------------------------------------------------------
//storing zip file on server
//
void fnStoreZipfile(int file, char *fileName) 
{
	int inPipe[2], outPipe[2]; //pipes to communicate

	file = open(fileName, O_RDONLY);
	char newFilename[30];
	sprintf(newFilename, "%s.gz", fileName); //make a new name to .gz
	int newFile = open(newFilename, O_CREAT | O_RDWR | O_TRUNC, 0777);
	
	char buffer[MAXDATA];
	pid_t pid;
	//create 2 pipes
	if ((pipe(inPipe)) == -1 || (pipe(outPipe)) == -1)
		perror("pipe");
	
	if ((pid = fork()) == -1) 
	{
		perror("fork");
	}

	//child process
	if (!pid) 
	{
		close(inPipe[1]);
		close(outPipe[0]);
		
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		dup2(inPipe[0], STDIN_FILENO);	//set stdin
		dup2(outPipe[1], STDOUT_FILENO);//set stdout
		
		execlp("gzip" ,"-c","-k","-f", NULL);	//exec gzio
		exit(0);
	}

	//parent process
	if (pid) 
	{
		close(inPipe[0]);
		close(outPipe[1]);
		
		//read from file and write to inPipe[1]
		while( read(file, buffer, MAXDATA) )
		{
			write(inPipe[1], buffer, MAXDATA);
		}
		close(file);
		close(inPipe[1]);

		//wait child to be terminated
		int status;
		waitpid(pid, &status, 0);
		
		//read from outPipe[0] and write to the new file
		while (read(outPipe[0], buffer, MAXDATA)) 
		{
			write(newFile, buffer, MAXDATA);
		}
		remove(fileName);  //remove uncompressed file
		close(newFile);
	}
}


//--------------------------------------------------------------------------------------
//Storing normal file on server
//
int fnStoreNormalFile(int socketDescriptor, char *buffer, char *fileName) 
{	
	char* temp = strrchr(fileName, '/');
	if (temp) 
		fileName = temp + 1;
	
	//Gegtting zip flag
	if( recv(socketDescriptor, buffer, MAXDATA, 0) == -1 )
	{
		perror("recv");
		return 0;
	}
	char zip[20];
	strcpy(zip, buffer);

	off_t fileSize;
	if (recv(socketDescriptor, &fileSize, sizeof(off_t), 0) == -1) 
	{		
		perror("recv");
		return 0;
	}

	//open the file
	int file = open(fileName, O_CREAT | O_RDWR | O_TRUNC, 0777);
	if( file== -1) {
		perror("write");
		return 0;
	}

	//dummy byte marking
	if(lseek(file, fileSize - 1, SEEK_SET) == -1 )
	{
		perror("lseek");
		return 0;
	}
	
	if( (write(file, "", 1)) == -1 )
	{
		perror("write");
		return 0;
	}

	//Mapping file into memory
	char* fileBuffer = mmap(0, fileSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED,file, 0);
	if (fileBuffer == MAP_FAILED) 
	{
		perror("mmap");
		return 0;
	}

	char nByte;
	int offset = 0;
	//receive dara byte and write to memory
	while (fileSize > 0) 
	{
		nByte = recv(socketDescriptor, buffer, MAXDATA, 0);
		memcpy(fileBuffer + offset, buffer, nByte);
		offset += nByte;
		fileSize -= nByte;
	}
	munmap(fileBuffer, fileSize);

	//close file
	close(file);


	if (!strcmp("zip", zip)) 
		fnStoreZipfile(file, fileName);
	
	printf("File is stored successfully\n");
	return 1;
}

//--------------------------------------------------------------------------------------
//list server files
//
int fnListServerFiles(int socketDescriptor, char *buffer) 
{
	char filePath[100];
	getcwd(filePath, 100);
	DIR* directory = opendir(filePath);

	struct dirent *file;
	while( (file = readdir(directory)) != NULL ) 
	{
		if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, "..")) 
			continue;

		send(socketDescriptor, file->d_name, MAXDATA, 0);
	}
	send(socketDescriptor, "EOF", MAXDATA, 0);
	closedir(directory);
	printf("Request for listing server files is finished successfully\n");
	return 1;
}


//--------------------------------------------------------------------------------------
//
//
int fnActionOnClientData(char *buffer, char *fileName, int i)
{
	pid_t pid;
	if((pid=fork())==-1)
        perror("fork");

	if(!pid)
	{
		char *pos ;
		if( (pos = strstr(buffer, "l-") ) != NULL ) 
		{
			strcpy(fileName, pos + 2);
			if (!fnLoadfile(i, buffer, fileName)) 
			{
				perror("loadfile");
				return 0;
			}
			return 1;
		} 
		else if( (pos = strstr(buffer, "s-")) != NULL) 
		{
			strcpy(fileName, pos + 2);
			if (!fnStoreNormalFile(i, buffer, fileName)) 
			{
				perror("storefile");
				return 0;
			}
			return 1;
		} 
		else if( (pos = strstr(buffer, "ls")) != NULL) 
		{
			if (!fnListServerFiles(i, buffer)) 
			{
				perror("ls");
				return 0;
			}
			return 1;
		}
	}


	if(pid)
	{
		int status;
		waitpid(pid, &status, 0);
	}
	return 1;
}

//--------------------------------------------------------------------------------------
// Get socket address( in IPv4 or IPv6 ) as per family
//
void* fnGetSocketAddress( struct sockaddr* socketAddress ) 
{
	void* vp = NULL ;
	if( socketAddress->sa_family == AF_INET) 
		vp = &(((struct sockaddr_in*)socketAddress)->sin_addr);
	else
		vp = &(((struct sockaddr_in6*)socketAddress)->sin6_addr);
	return vp ;
}


//--------------------------------------------------------------------------------------
//
//
int fnGetSocketDescriptor( int* socketDescriptor )
{
	struct addrinfo releventInfo ;
	memset(&releventInfo, 0, sizeof releventInfo);
	releventInfo.ai_family = AF_UNSPEC;
	releventInfo.ai_socktype = SOCK_STREAM;
	releventInfo.ai_flags = AI_PASSIVE;

	struct addrinfo *result, *pointer;
	int status;
	if( (status = getaddrinfo(NULL, PORTNUMBER, &releventInfo, &result)) != 0 )
	{
		perror("getaddrinfo");
		return -1;
	}

	// traverse all results
	for( pointer = result ; pointer != NULL ; pointer = pointer->ai_next) 
	{
		if( (*socketDescriptor = socket(pointer->ai_family, pointer->ai_socktype, pointer->ai_protocol)) == -1 )
		{
			perror("socket");
			continue;
		}

		int yes = 1;
		setsockopt(* socketDescriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(*socketDescriptor, pointer->ai_addr, pointer->ai_addrlen) < 0) 
		{
			close(*socketDescriptor);
			continue;
		}

		break;
	}

	//Free list of address
	freeaddrinfo(result);

	//can't bind
	if(pointer == NULL) 
	{
		perror("server: Failed to bind\n");
		return -1;
	}
	return 1 ;
}

//--------------------------------------------------------------------------------------
//main entry function
//
int main() 
{
	int socketDescriptor ;
	fnGetSocketDescriptor( &socketDescriptor ) ;

	printf("-----------------------------------------------------------\n");
	printf("-------------------Welcome to the server-------------------\n");
	printf("-----------------------------------------------------------\n");
	printf("Waiting connection from client...\n");

	// listen
	if (listen(socketDescriptor, 10) == -1) 
	{
		perror("listen");
		return -1;
	}

	struct epoll_event ev , events[10] ;
	int nDescriptor , epollDescriptor ;
	
	epollDescriptor = epoll_create1(0);
	if( epollDescriptor == -1 )
	{
		perror("epoll_create1");
		return -1 ;
	}

	ev.events = EPOLLIN ;
	ev.data.fd = socketDescriptor ;
	if( epoll_ctl( epollDescriptor , EPOLL_CTL_ADD , socketDescriptor , &ev ) == -1 )
	{
		perror("epoll_ctl") ;
		return -1 ;
	}

	struct sockaddr_storage clientAddress ;
	char clientIP[INET6_ADDRSTRLEN];

	for(;;)
	{
		nDescriptor = epoll_wait( epollDescriptor , events , 10 , -1 );
		if( nDescriptor == -1 )
		{
			perror( "epoll wait") ;
			return -1 ;
		}

		for( int n = 0; n < nDescriptor ; n++ )
		{
			if( events[n].data.fd == socketDescriptor )
			{
				socklen_t addressLength = sizeof clientAddress;
				int newSocketDesc = accept(socketDescriptor, (struct sockaddr *) &clientAddress, &addressLength);

				if (newSocketDesc == -1) 
				{
					perror("accept");
					return -1 ;
				}
				
				const char* ipStr = inet_ntop(clientAddress.ss_family,fnGetSocketAddress((struct sockaddr*) &clientAddress),clientIP, INET6_ADDRSTRLEN) ;
				printf("New connection from client %s on socket %d\n", ipStr, newSocketDesc);

				ev.events = EPOLLIN | EPOLLET ;
				ev.data.fd = newSocketDesc ;
				if( epoll_ctl( epollDescriptor , EPOLL_CTL_ADD, newSocketDesc , &ev ) == -1 )
				{
					perror( "epoll_ctl" ) ;
					return -1 ;
				}
					
			}
			else
			{
				int  bytesRecieved;
				char buffer[256];
				memset(buffer,0,256);
				char fileName[20];

				int i = events[n].data.fd ;
				// make action as per data received from client
				if( (bytesRecieved = recv(i, buffer, MAXDATA, 0)) <= 0 ) 
				{
					if ( bytesRecieved == 0)// connection closed
						printf("Connection closed on socket %d\n", i);
					else 
					{
						perror("recv");
					}
					close(i);
				} 
				else 
				{
					//action done as per client data
					if( !fnActionOnClientData(buffer, fileName, i) ) 
					{
						perror("Client request error\n");
						return -1;
					}
				}

			}
		}
	}
	return 0;
}
