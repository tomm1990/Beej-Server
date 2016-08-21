all: project

project:server client
	mv server ServerDir
	mv client ClientDir

server: server.o
	gcc server.o -o server
	mv server.o ServerDir

client: client.o
	gcc client.o -o client
	mv client.o ClientDir

server.o: 
	  mkdir ServerDir
	  gcc -c -std=gnu99 server.c
	  mv server.c ServerDir

client.o: 
	  mkdir ClientDir
	  gcc -c -std=gnu99 client.c
	  mv client.c ClientDir

clean:
	 mv ServerDir/server.c ServerDir/..
	 mv ClientDir/client.c ClientDir/..
	 rm -rf ClientDir
	 rm -rf ServerDir
