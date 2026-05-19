#include "Socket.h"
#include "Client.h"
#include <cerrno>
#include <errno.h>
#include <expected>
#include <netinet/in.h>
#include <fstream>
#include <sstream>
#include <sys/epoll.h>
#include <vector>

#define LISTEN_BACKLOG 10000

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
	return std::unexpected(ErrorInfo{err, "Failed to set reuse address option : " + std::string(strerror(err))});
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

std::expected<void, ErrorInfo> readSock(ClientState& clientState)
{
    size_t curSize = clientState.readBuffer.size();
    ssize_t n = 0;
    
    while(true)
    {
	if(curSize == clientState.readBuffer.size())
	{
	    if(clientState.readBuffer.size() == 0)
		clientState.readBuffer.resize(4096);
	    
	    else
		clientState.readBuffer.resize(clientState.readBuffer.size() * 2);
	}

	n = read(clientState.sock.fd, clientState.readBuffer.data() + curSize, clientState.readBuffer.size() - curSize);
    
	if(n == -1)
	{
	    int err = errno;

	    if (err == EINTR) continue;
	    else if (err == EAGAIN || err == EWOULDBLOCK) return {};

	    return std::unexpected(ErrorInfo{err, "Failed to read request : " + std::string(strerror(err))});
	}

	if(n == 0)
	{
	    return std::unexpected(ErrorInfo(499, "Client closed request"));
	}
	
	curSize += n;
	
	//This works because the server only supports GET requests...
	if(curSize >= 4 && memcmp(clientState.readBuffer.data() + curSize - 4, "\r\n\r\n", 4) == 0)
	{
	    clientState.requestReady = true;
	    break;
	}
    }

    clientState.readBuffer.resize(curSize);

    return {};
}

std::expected<std::string, ErrorInfo> parse(std::string request)
{
    if(!request.starts_with("GET "))
    {
        return std::unexpected(ErrorInfo{405, "HTTP/1.1 405 Method Not Supported\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"});
    }

    size_t start = 4; 
    size_t end = request.find(' ', start);

    if(end == SIZE_MAX)
    {
        return std::unexpected(ErrorInfo{400, "HTTP/1.1 400 Bad Request : File Name Not Specified\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"});
    }

    std::string path = "./static" + request.substr(start, end - start);

    if(path.find("..") != SIZE_MAX)
    {
        return std::unexpected(ErrorInfo{403, "HTTP/1.1 403 Directory Traversal Denied\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"});
    }

    if(path == "./static/")
    {
	path = "./static/index.html";
    }

    std::string content;

    std::ifstream file(path);

    if(!file.is_open())
    {
        return std::unexpected(ErrorInfo{404, "HTTP/1.1 404 File Not Found\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"});
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();

    std::string response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n{}", content.size(), content);

    return response;
}

std::expected<void, ErrorInfo> writeSock(ClientState& clientState)
{
    size_t totalSize = clientState.writeBuffer.length();
    ssize_t n = 0;

    while(clientState.bytesSent < totalSize)
    {
	n = write(clientState.sock.fd, clientState.writeBuffer.data() + clientState.bytesSent, totalSize - clientState.bytesSent);

	if(n == -1)
	{
	    int err = errno;

	    if(err == EINTR) continue;
	    if(err == EAGAIN || err == EWOULDBLOCK) return {};
	    
	    return std::unexpected(ErrorInfo{err, "Failed to write response : " + std::string(strerror(err))});
	}

	clientState.bytesSent += n;
    }

    return {};
}

std::expected<ClientState, ErrorInfo> acceptClient(const Socket& serverSock, struct sockaddr* clAddr, socklen_t* clAddrSize)
{
    ClientState clientState = {Socket(accept(serverSock.fd, clAddr, clAddrSize))};

    if(clientState.sock.fd == -1)
    {
	int err = errno;

	return std::unexpected(ErrorInfo{err, "Failed to accept client : " + std::string(strerror(err))});
    }

    return clientState;
}
