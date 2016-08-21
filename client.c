// client.c :
//

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

#define IPADDRESS	"127.0.0.1"
#define PORTNUMBER	"3490" // Port to which client will connect
#define MAXDATA		100

//--------------------------------------------------------------------------------------
//loading file from sever to client
//
int fnLoadfile( int socketDescriptor, char *buffer ) 
{
	//File name that will be loaded from server to client
	printf("Enter the file name : \n");
	char fileName[20];
	scanf("%s", fileName);

	//Add command before file name so that server will get to know action
	sprintf(buffer, "l-%s", fileName);

	//send request to server for loading file
	if( send(socketDescriptor, buffer, MAXDATA, 0) == -1 ) 
	{
		perror("send");
		return 0;
	}

	if( read(socketDescriptor, buffer, MAXDATA) == -1 ) 
	{
		perror("read");
		return 0;
	}

	//if file not found on server side
	if( strcmp("ERROR", buffer) == 0 ) 
	{
		printf("File not found on server.\n");
		return 1;
	}

	off_t fileSize;
	if( recv(socketDescriptor, &fileSize, sizeof(off_t), 0) == -1 ) 
	{
		perror("recv");
		return 0;
	}
	
	//Removing .gz
	if( strstr( fileName, ".gz" ) )
	{
		char* pos = strstr(fileName, ".gz");
		*pos = 0;
	}

	//open file , if file is not there then it will be created
	int file = open(fileName, O_CREAT | O_RDWR | O_TRUNC, 0777);
	if( file == -1 )
	{
		perror("write");
		return 0;
	}

	//Dummy byte marking
	if( lseek(file, fileSize - 1, SEEK_SET) == -1 )
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
	//recive and write data
	while( fileSize > 0 ) 
	{
		nByte = recv(socketDescriptor, buffer, MAXDATA, 0);
		memcpy(fileBuffer + offset, buffer, nByte);
		offset += nByte;
		fileSize -= nByte;
	}
	//unmap
	munmap(fileBuffer, fileSize);

    //close file
	close(file);
	printf("File is loaded successfully\n");
	return 1;
}

//--------------------------------------------------------------------------------------
//Store file in server from client
//
int fnStoreFile( int socketDescriptor, char *buffer )
{
	printf("Enter the file name : \n");
	char fileName[20];
	scanf("%s", fileName);

	//open the file to be store
	int file = open(fileName, O_RDONLY);
	if (file == -1) 
	{
		printf("File not found.\n");
		return 1;
	}

	printf("Do you want to compress the file? Press ( Y / N ).\n");
	char zipCompress[2];
	scanf("%s", zipCompress);
	
	//Add command before file name so that server will get to know action
	sprintf(buffer, "s-%s", fileName);
	
	//stat info
	struct stat statInfo;
	stat(fileName, &statInfo);
	
	//send the request to the server to store
	if (send(socketDescriptor, buffer, MAXDATA, 0) == -1) 
	{
		perror("send");
		return 0;
	}

	if ( strncmp( zipCompress , "y" , 1 ) == 0 || strncmp( zipCompress , "Y" , 1 ) == 0 ) 
	{
		if (send(socketDescriptor, "zip", MAXDATA, 0) == -1) 
		{
			perror("send");
			return 0;
		}
	} 
	else 
	{
		if (send(socketDescriptor, "normal", MAXDATA, 0) == -1) 
		{
			perror("send");
			return 0;
		}
	}

	//send the file size
	if (send(socketDescriptor, &statInfo.st_size, sizeof(off_t), 0) == -1) 
	{
		perror("write");
		return 0;
	}

	//read data from file and write to server socket
	int retFlag;
	while( (retFlag = read(file, buffer, MAXDATA)) ) 
	{
		if (retFlag == -1) 
		{
			perror("read");
			return 0;
		}

		if (send(socketDescriptor, buffer, MAXDATA, 0) == -1) 
		{
			perror("send");
			return 0;
		};
	}
	printf("File is stored successfully\n");
	return 1;
}


//--------------------------------------------------------------------------------------
//List files that are in client directory
//
int fnListClientFiles() 
{
	char filePath[100];
	getcwd(filePath, 100);
	DIR* directory = opendir(filePath);

	printf("Client Files :\n");
	struct dirent *file;
	while( (file = readdir(directory)) != NULL ) 
	{
		if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, "..")) 
			continue;

		printf(" %s\n", file->d_name);
	}
	closedir(directory);
	return 1;
}

//--------------------------------------------------------------------------------------
//List files that are in server directory
//
int fnListServerFiles(int socketDescriptor, char *buffer) 
{
	//sprintf(buffer, "ls");
	if (send(socketDescriptor, "ls", MAXDATA, 0) == -1) 
	{
		perror("send");
	}

	printf("Server Files :\n");
	while( recv(socketDescriptor, buffer, MAXDATA, 0) )
	{
		if (!strcmp(buffer, "EOF")) 
			break;

		printf(" %s\n", buffer);
	}
	return 1;
}

//--------------------------------------------------------------------------------------
//Taking action as per user input
//
int fnGetInputFromUser(int socketDescriptor, char *buffer) 
{
	printf("\n****************************************************\n");
	printf("*                       Menu                       *\n");
	printf("****************************************************\n");
	printf("*          1 - Get list of client files            *\n");
	printf("*          2 - Get list of server files            *\n");
	printf("*          3 - Load file from server               *\n");
	printf("*          4 - Store file in server                *\n");
	printf("*          5 - Disconnect                          *\n");
	printf("****************************************************\n");
	
	int selOption;
	scanf("%d", &selOption );

	switch( selOption )
	{
	case 1:
		{
			// list all client files
			if( !fnListClientFiles() ) 
			{
				perror("listClientFiles");
				return -1;
			}
			return 1;
		}
		break ;
	case 2 :
		{
			bzero(buffer, MAXDATA);
			if (!fnListServerFiles(socketDescriptor, buffer)) 
			{
				perror("listServerFiles");
				return -1;
			}
			return 2;
		}
		break ;
	case 3 :
		{
			//Load file from server
			if( !fnLoadfile(socketDescriptor, buffer) ) 
			{
				perror("Loadfile");
				return -1;
			}
			return 3;
		}
		break ;
	case 4 :
		{
			//Store file in server
			if( !fnStoreFile(socketDescriptor, buffer) ) 
			{
				perror("StoreFile");
				return -1;
			}
			return 4;
		}
		break ;
	case 5 :
		{
			printf("Exit\n");
			return 5;
		}
	}
	
	return -1;
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
// connection to server
//
int fnConnectToServer( int* socketDescriptor, char* ipString )
{
	struct addrinfo releventInfo ;
	memset(&releventInfo, 0, sizeof releventInfo);
	releventInfo.ai_family = AF_UNSPEC;
	releventInfo.ai_socktype = SOCK_STREAM;

	struct addrinfo *result, *pointer;
	int status;
	if( (status = getaddrinfo(IPADDRESS, PORTNUMBER, &releventInfo, &result)) != 0 )
	{
		perror("getaddrinfo");
		return -1;
	}

	// traverse all results
	//int socketDescriptor;
	for( pointer = result ; pointer != NULL ; pointer = pointer->ai_next) 
	{
		if( (*socketDescriptor = socket(pointer->ai_family, pointer->ai_socktype, pointer->ai_protocol)) == -1) 
		{
			perror("socket");
			continue;
		}

		if( connect(*socketDescriptor, pointer->ai_addr, pointer->ai_addrlen) == -1 )
		{
			close(*socketDescriptor);
			perror("connect");
			continue;
		}

		break;
	}

	//Free list of address
	freeaddrinfo(result);

	if (pointer == NULL) 
	{
		perror("Failed to connect\n");
		return -1;
	}

	char ipStr[INET6_ADDRSTRLEN];
	inet_ntop( pointer->ai_family, fnGetSocketAddress( (struct sockaddr *) pointer->ai_addr ), ipStr, sizeof ipStr);
	memcpy( &ipString[0] , ipStr , INET6_ADDRSTRLEN ) ;
	return 1 ;
}

//--------------------------------------------------------------------------------------
//main entry function
//
int main() 
{
	char ipString[INET6_ADDRSTRLEN];
	int socketDescriptor ;
	if( fnConnectToServer( &socketDescriptor, ipString ) == -1 )
		return -1 ;

	char buffer[MAXDATA];
	bzero(buffer, MAXDATA);

	printf("----------------------------------------------------------\n");
	printf("-------------------Welcome to %s-------------------\n", ipString);
	printf("----------------------------------------------------------\n");
	
	//Action taken as per selected input
	int selOption = 1 ;
	while( selOption >= 1 && selOption < 5 )
	{
		selOption = fnGetInputFromUser(socketDescriptor, buffer);
	} 

	close(socketDescriptor);
	return 0;
}
