#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

    while(Running)
    {
        auto clientSock = acceptClient(*serverSock, (struct sockaddr*) &clAddr, &clAddrSize);
        
        if(!clientSock)
	{
            if (clientSock.error().code == EINTR) break;

            fprintf(stderr, "Accept failed: %s\n", clientSock.error().message.data());
        
	    continue; 
        }

        auto result = handleClient(*clientSock);
        
	if(!result) {
            fprintf(stderr, "Client handling error: %s\n", result.error().message.data());
        }
    }

    return 0;
}
