#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <ostream>
#include <string>
#include <sys/epoll.h>
#include "Server.h"
#include "Client.h"

#define MAX_EVENTS 64

volatile sig_atomic_t Running = true;

void handleSIGINT(int signum)
{
    Running = false;
}

int main()
{
    auto serverSock = initializeServer();

    if(!serverSock)
    {
        fprintf(stderr, "Fatal Server Error: %s (Code: %d)\n", serverSock.error().message.data(), serverSock.error().code);
    
	return EXIT_FAILURE;
    }

    auto serverNonBlock = setNonBlocking((*serverSock));

    if(!serverNonBlock.has_value())
    {
	fprintf(stderr, "Failed to set server socket as non blocking : %s (Code: %d)\n", serverNonBlock.error().message.data(), serverNonBlock.error().code);
    
	return EXIT_FAILURE;
    }

    int epollfd = epoll_create1(0);

    if(epollfd == -1)
    {
	int err = errno;

	fprintf(stderr, "Failed to create epoll instance : %s\n", strerror(err));
    
	return EXIT_FAILURE;
    }

    struct epoll_event epevent;

    epevent.events = EPOLLIN;
    epevent.data.fd = (*serverSock).fd;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, (*serverSock).fd, &epevent) == -1)
    {
	int err = errno;

	fprintf(stderr, "Failed to register server socket to epoll : %s\n", strerror(err));

	return  EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];

    struct sockaddr_in clAddr;
    socklen_t clAddrSize = sizeof(clAddr);

    struct sigaction action = {0};
    action.sa_handler = &handleSIGINT;
    sigaction(SIGINT, &action, NULL);
    
    //will replace this with arenas later
    std::list<ClientState> clientStates;

    while(Running)
    {
	int revents = epoll_wait(epollfd, events, MAX_EVENTS, -1);

	for(int i = 0; i < revents; i++)
	{
	    if(events[i].data.fd == (*serverSock).fd)
	    {
		if(events[i].events & EPOLLERR)
		{
		    Running = false;

		    continue;
		}

		if(events[i].events & EPOLLIN)
		{
		    while(true)
		    {
			auto clientState = acceptClient(*serverSock, (struct sockaddr*) &clAddr, &clAddrSize);

			if(!clientState.has_value())
			{
			    if (clientState.error().code == EINTR) Running = false;
			    if(clientState.error().code == EAGAIN || clientState.error().code == EWOULDBLOCK) break;

			    fprintf(stderr, "Accept failed: %s\n", clientState.error().message.data());

			    continue;
			}

			auto clientNonBlock = setNonBlocking((*clientState).sock);

			if(!clientNonBlock.has_value())
			{
			    fprintf(stderr, "Failed to set client socket as non blocking : %s (Code: %d)\n", clientNonBlock.error().message.data(), clientNonBlock.error().code);

			    continue;
			}

			clientStates.push_back(std::move(*clientState));

			clientStates.back().selfIt = std::prev(clientStates.end());

			struct epoll_event epevent;

			epevent.events = EPOLLIN | EPOLLHUP;
			epevent.data.ptr = &clientStates.back();

			if(epoll_ctl(epollfd, EPOLL_CTL_ADD, clientStates.back().sock.fd, &epevent) == -1)
			{
			    int err = errno;

			    fprintf(stderr, "Failed to register client socket to epoll : %s\n", strerror(err));

			    clientStates.erase(clientStates.back().selfIt);

			    continue;
			}
		    }
		}

		continue;
	    }

	    ClientState* eventClientState = ((ClientState*)(events[i].data.ptr));

	    if(events[i].events & (EPOLLHUP | EPOLLERR))
	    {
		clientStates.erase(eventClientState->selfIt);

		continue;
	    }

	    if(events[i].events & EPOLLIN)
	    {
		auto read = readSock(*eventClientState);

		if(!read.has_value())
		{
		    clientStates.erase(eventClientState->selfIt);

		    fprintf(stderr, "%s", read.error().message.data());

		    continue;
		}

		if(eventClientState->requestReady == true)
		{
		    auto response = parse(std::string(eventClientState->readBuffer.data(), eventClientState->readBuffer.size()));

		    if(!response.has_value())
		    {
			fprintf(stderr, "%s", response.error().message.data());

			clientStates.erase(eventClientState->selfIt);

			continue;
		    }

		    eventClientState->writeBuffer = *response;

		    eventClientState->responseReady = true;

		    epevent.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
		    epevent.data.ptr = eventClientState;

		    if(epoll_ctl(epollfd, EPOLL_CTL_MOD, eventClientState->sock.fd, &epevent) == -1)
		    {
			int err = errno;

			fprintf(stderr, "Failed to modify client epoll events : %s\n", strerror(err));

			clientStates.erase(eventClientState->selfIt);

			continue;
		    }
		}	
	    }

	    if (events[i].events & EPOLLOUT)
		{
		    auto write = writeSock(*eventClientState);

		    if(!write.has_value())
		    {
			fprintf(stderr, "%s", write.error().message.data());

			continue;
		    }

		    if(eventClientState->bytesSent == eventClientState->writeBuffer.size())
		    {
			clientStates.erase(eventClientState->selfIt);
		    }
		}
	}
    }

    return EXIT_SUCCESS;
}
