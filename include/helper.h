#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "debug.h"

#define LISTENQ 8
// Code reworked from Computer Systems: A Programmer's Perspective 3rd Edition, Section 11.4, Page 981
int open_listenfd(char*port){
	struct addrinfo hints,*listp,*p;
	int listenfd,optval=1;

	memset(&hints,0,sizeof(struct addrinfo));
	hints.ai_socktype=SOCK_STREAM; 
	hints.ai_flags=AI_PASSIVE|AI_ADDRCONFIG;
	hints.ai_flags|=AI_NUMERICSERV; 
	if (getaddrinfo(NULL,port,&hints,&listp) != 0){
		debug("hello2");
		fprintf(stderr, "Could not get port address information: %s", port);
		return -1;
	}

	for(p=listp; p; p=p->ai_next){
		if((listenfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0)
			continue; 

		setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR, (const void*)&optval,sizeof(int));

		if(bind(listenfd,p->ai_addr,p->ai_addrlen)==0){
		 	break;
		}
		close(listenfd);
	}


	freeaddrinfo(listp);
	if(!p){
		debug("hello1");
		return -1;
	}

	if(listen(listenfd,LISTENQ)<0){
		debug("hello");
		close(listenfd);
		return -1;
	}
	return listenfd;
}
