#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <expected>

#define LISTEN_BACKLOG 64

struct ErrorInfo {
    int code;
    std::string message;
};

class Socket {
public :
    int fd;

    Socket(int fileDesc) : fd(fileDesc) {}

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd(other.fd)
    {
	other.fd = -1;
    }

    ~Socket()
    {
	if (fd != -1)
	    close(fd);
    }
};

std::expected<Socket, ErrorInfo> initializeServer()
{
    Socket sock = Socket(socket(AF_INET, SOCK_STREAM, 0));

    if(sock.fd == -1)
    {
	int err = errno;
	return std::unexpected(ErrorInfo{err, "Failed to create socket : " + std::string(strerror(err))});
    }

    int opt = 1;
    if (setsockopt(sock.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
	int err = errno;
	return std::unexpected(ErrorInfo{err, "Failed to set socket options : " + std::string(strerror(err))});
    }

    struct sockaddr_in addr = {AF_INET, htons(8080), {0}}; 

    if(bind(sock.fd, (const sockaddr*)&addr, sizeof(addr)) == -1)
    {
	int err = errno;
	return std::unexpected(ErrorInfo{err, "Failed to bind socket : " + std::string(strerror(err))});
    }

    if(listen(sock.fd, LISTEN_BACKLOG) == -1)
    {
	int err = errno;
	return std::unexpected(ErrorInfo{err, "Failed to set socket as passive : " + std::string(strerror(err))});
    }

    return sock;
}

std::expected<std::vector<char>, ErrorInfo> readSock(const Socket& sock)
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

	n = read(sock.fd, buf.data() + curSize, buf.size() - curSize);
    
	if(n == -1)
	{
	    int err = errno;

	    if (err == EINTR) continue;

	    return std::unexpected(ErrorInfo{err, "Failed to read request : " + std::string(strerror(err))});
	}

	if(n == 0)
	{
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

std::expected<std::string, ErrorInfo> parse(const char* request)
{
    return "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHELLO WORLD!";
}

std::expected<size_t, ErrorInfo> writeSock(const Socket& sock, std::string response)
{
    size_t totalSize = response.length();
    size_t totalSent = 0;
    ssize_t n = 0;

    while(totalSent < totalSize)
    {
	n = write(sock.fd, response.data() + totalSent, totalSize - totalSent);

	if(n == -1)
	{
	    int err = errno;

	    if(errno == EINTR) continue;
	    
	    return std::unexpected(ErrorInfo{err, "Failed to write response : " + std::string(strerror(err))});
	}

	totalSent += n;
    }

    return totalSent;
}

std::expected<Socket, ErrorInfo> acceptClient(const Socket& serverSock, struct sockaddr* clAddr, socklen_t* clAddrSize)
{
    Socket clientSock = Socket(accept(serverSock.fd, clAddr, clAddrSize));

    if(clientSock.fd == -1)
    {
	int err = errno;

	return std::unexpected(ErrorInfo{err, "Failed to accept client : " + std::string(strerror(err))});
    }

    return clientSock;
}

std::expected<void, ErrorInfo> handleClient(const Socket& sock)
{
    auto requestResult = readSock(sock);
    
    if(!requestResult)
    {
        return std::unexpected(requestResult.error());
    }

    auto responseResult = parse((*requestResult).data());
    
    if(!responseResult)
    {
        return std::unexpected(responseResult.error());
    }
    
    auto writeResult = writeSock(sock, *responseResult);

    if(!writeResult)
    {
        return std::unexpected(writeResult.error());
    }

    return {};
}

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
