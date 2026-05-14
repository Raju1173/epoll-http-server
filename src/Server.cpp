#include <__expected/unexpected.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include "Server.h"

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

    struct sockaddr_in clAddr;
    socklen_t clAddrSize = sizeof(clAddr);

    struct sigaction action = {0};
    action.sa_handler = &handleSIGINT;
    sigaction(SIGINT, &action, NULL);

    int totalRequests = 0;
    std::chrono::nanoseconds totalDuration(0);

    while(Running)
    {
        auto clientSock = acceptClient(*serverSock, (struct sockaddr*) &clAddr, &clAddrSize);
        
        if(!clientSock)
	{
            if (clientSock.error().code == EINTR) break;

            fprintf(stderr, "Accept failed: %s\n", clientSock.error().message.data());
        
	    continue; 
        }

	auto start = std::chrono::steady_clock::now();

        auto result = handleClient(*clientSock);

	auto end = std::chrono::steady_clock::now();
        
	totalDuration += (end - start);

	if(!result.has_value())
	{
	    auto sendError = writeSock(*clientSock, result.error().message.data());
	    
	    fprintf(stderr, "Client handling error: %s\n", result.error().message.data());
        }

	totalRequests++;
    }

    double seconds = std::chrono::duration<double>(totalDuration).count();

    std::cout << "Internal req/sec: " << totalRequests / seconds << std::endl;

    return EXIT_SUCCESS;
}
