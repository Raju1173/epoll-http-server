#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <sys/socket.h>
#include <errno.h>
#include <system_error>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>

#define LISTEN_BACKLOG 64

int initializeServer()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd == -1)
    {
	return -1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
	return -1;
    }

    struct sockaddr_in addr = {AF_INET, htons(8080), {0}}; 

    if(bind(sockfd, (const sockaddr*)&addr, sizeof(addr)) == -1)
    {
	return -1;
    }

    if(listen(sockfd, LISTEN_BACKLOG) == -1)
    {
	return -1;
    }

    return sockfd;
}

std::vector<char> readSock(int fd)
{
    std::vector<char> buf(4096);
    size_t curSize = 0;
    ssize_t n = 0;

    while(true)
    {
	if(curSize == buf.size())
	{
	    buf.resize(buf.size() * 2);
	}

	n = read(fd, buf.data() + curSize, buf.size() - curSize);
    
	if(n == -1)
	{
	    if (errno == EINTR) continue;

	    return {};
	}

	if(n == 0)
	{
	    std::cout << "Connection closed by client : " << fd << std::endl;
	    
	    break;
	}
	
	curSize += n;
	
	//This works because the server only supports GET requests...
	if(curSize >= 4 && memcmp(buf.data() + curSize - 4, "\r\n\r\n", 4) == 0)
	{
	    break;
	}
    }

    buf.resize(curSize);

    return buf;
}

const char* parse(const char* request)
{
    return "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHELLO WORLD!";
}

int writeSock(int fd, const char* response)
{
    size_t totalSize = strlen(response);
    size_t totalSent = 0;
    ssize_t n = 0;

    while(totalSent < totalSize)
    {
	n = write(fd, response + totalSent, totalSize - totalSent);

	if(n == -1)
	{
	    if(errno == EINTR) continue;
	    
	    return -1;
	}

	totalSent += n;
    }

    return 0;
}

int handleClient(int cfd)
{
    std::vector<char> request = readSock(cfd);

    std::cout << request.data();

    const char* response = parse(request.data());
    
    writeSock(cfd, response);

    if (close(cfd) == -1)
    {
	throw std::system_error(errno, std::system_category(), "Failed to close client socket");
    }

    return 0;
}

volatile sig_atomic_t Running = true;

void handleSIGINT(int signum)
{
    Running = false;
}

int main()
{
    int sockfd = initializeServer();

    struct sockaddr_in clAddr;
    socklen_t clAddrSize = sizeof(clAddr);

    struct sigaction action = {0};

    action.sa_handler = &handleSIGINT;

    sigaction(SIGINT, &action, NULL);

    while(Running)
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
