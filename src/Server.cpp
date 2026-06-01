#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <string>
#include <sys/epoll.h>
#include "Server.h"

#define MAX_EVENTS 64

std::atomic<bool> Running = true;

void handleSIGINT(int signum)
{
    Running = false;
}

int main()
{
    LoggerGuard logger;

    auto serverSock = initializeServer();

    if(!serverSock)
    {
	log({messageType::ERROR, serverSock.error().message});
    
	return EXIT_FAILURE;
    }

    auto serverNonBlock = setNonBlocking((*serverSock));

    if(!serverNonBlock.has_value())
    {
	log({messageType::ERROR, serverNonBlock.error().message});
    
	return EXIT_FAILURE;
    }

    int epollfd = epoll_create1(0);

    if(epollfd == -1)
    {
	int err = errno;

	log({messageType::ERROR, std::string("Failed to create epoll instance : ") + strerror(err)});
    
	return EXIT_FAILURE;
    }

    struct epoll_event epevent;

    epevent.events = EPOLLIN;
    epevent.data.fd = (*serverSock).fd;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, (*serverSock).fd, &epevent) == -1)
    {
	int err = errno;

	log({messageType::ERROR, std::string("Failed to register server socket to epoll : ") + strerror(err)});

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

			    log({messageType::ERROR, "Accept failed : " + clientState.error().message});

			    continue;
			}

			auto clientNonBlock = setNonBlocking((*clientState).sock);

			if(!clientNonBlock.has_value())
			{
			    log({messageType::ERROR, clientNonBlock.error().message});

			    continue;
			}

			clientStates.push_back(std::move(*clientState));

			clientStates.back().selfIt = std::prev(clientStates.end());

			struct epoll_event epevent;

			epevent.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
			epevent.data.ptr = &clientStates.back();

			if(epoll_ctl(epollfd, EPOLL_CTL_ADD, clientStates.back().sock.fd, &epevent) == -1)
			{
			    int err = errno;

			    log({messageType::ERROR, std::string("Failed to register client socket to epoll : ") + strerror(err)});

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

		    log({messageType::ERROR, read.error().message});

		    continue;
		}

		if(eventClientState->readBuffer.size() >= 4)
		{
		    size_t searchStart = (eventClientState->parseOffset >= 3) ? eventClientState->parseOffset - 3 : 0;
		    size_t searchLen = eventClientState->readBuffer.size() - searchStart;
		    
		    //This works because the server only supports GET requests...
		    char *requestEnd = (char*)memmem(eventClientState->readBuffer.data() + searchStart, searchLen, "\r\n\r\n", 4);

		    while(requestEnd != nullptr)
		    {
			auto start = eventClientState->readBuffer.begin();
			auto end = eventClientState->readBuffer.begin() + ((requestEnd + 4) - eventClientState->readBuffer.data());

			auto response = parse(std::string(start, end));

			if(!response.has_value())
			{
			    log({messageType::ERROR, response.error().message});
			}
			else
			{
			    eventClientState->responses.push_back(*response);
			}

			eventClientState->readBuffer.erase(start, end);
			eventClientState->parseOffset = 0;

			if(eventClientState->readBuffer.size() >= 4)
			{
			    requestEnd = (char*)memmem(eventClientState->readBuffer.data(), eventClientState->readBuffer.size(), "\r\n\r\n", 4);
			}

			else
			{
			    requestEnd = nullptr; 
			}
		    }

		    eventClientState->parseOffset = eventClientState->readBuffer.size();
		}
	    }

	    if (events[i].events & EPOLLOUT)
	    {
		auto write = writeSock(*eventClientState);

		if(!write.has_value())
		{
		    log({messageType::ERROR, write.error().message});

		    continue;
		}
	    }
	}
    }

    return EXIT_SUCCESS;
}
