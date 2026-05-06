#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <ostream>
#include <sys/socket.h>
#include <errno.h>
#include <system_error>
#include <netinet/in.h>
#include <unistd.h>

#define LISTEN_BACKLOG 64

int handleClient(int cfd)
{
    ssize_t n = 0;
    char buf[4096];

    n = read(cfd, buf, sizeof(buf) - 1);

    if(n == -1)
    {
	if (errno == EINTR) return -1;

	throw std::system_error(errno, std::system_category(), "Failed to read");
	return - 1;
    }

    if(n == 0)
    {
	std::cout << "Connection closed by client : " << cfd << std::endl;
	
	close(cfd);

	return 0;
    }

    std::cout << buf;

    const char* hello = "HTTP/1.1 200 OK\r\n\r\nHELLO WORLD!";

    if(write(cfd, hello, strlen(hello)) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to write to clientfd");
    }

    if (close(cfd) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to close client socket");
    }

    return 0;
}

volatile sig_atomic_t running = true;

void handleSIGINT(int signum)
{
    running = false;
}

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to create socket"); //Fine for now but create a centralized function for handling errors later
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
	throw std::system_error(errno, std::system_category(), "setsockopt failed");
    }

    struct sockaddr_in addr = {AF_INET, htons(8080), {0}}; 

    if(bind(sockfd, (const sockaddr*)&addr, sizeof(addr)) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to bind socket");
    }

    if(listen(sockfd, LISTEN_BACKLOG) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to set socket as passive");
    }

    struct sockaddr_in clAddr;
    socklen_t clAddrSize = sizeof(clAddr);

    struct sigaction action = {0};

    action.sa_handler = &handleSIGINT;

    sigaction(SIGINT, &action, NULL);

    while(running)
    {
	int clientfd = accept(sockfd, (struct sockaddr *) &clAddr, &clAddrSize);
	
	clAddrSize = sizeof(clAddr);

	if(clientfd == -1)
	{
	    if (errno == EINTR) break;

	    throw std::system_error(errno, std::system_category(), "Failed to accept connection");
	}

	if(handleClient(clientfd) == -1)
	{
	    std::cerr << "Error handling client " << clientfd << std::endl;

	    close(clientfd);
	}
    }

    if (close(sockfd) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to close socket");
    }

    return 0;
}
