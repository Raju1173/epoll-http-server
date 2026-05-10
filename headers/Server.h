#include "Socket.h"
#include <errno.h>
#include <expected>
#include <netinet/in.h>
#include <fstream>
#include <sstream>

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

std::expected<std::string, ErrorInfo> parse(std::string request)
{
    if(!request.starts_with("GET "))
    {
        return std::unexpected(ErrorInfo{405, "Method not supported"});
    }

    size_t start = 4; 
    size_t end = request.find(' ', start);

    if(end == SIZE_MAX)
    {
        return std::unexpected(ErrorInfo{400, "Bad Request: No trailing space after path"});
    }

    std::string path = "./static" + request.substr(start, end - start);

    if(path.find("..") != SIZE_MAX)
    {
        return std::unexpected(ErrorInfo{403, "Forbidden: Path traversal detected"});
    }

    if(path == "./static/")
    {
	path = "./static/index.html";
    }

    std::string content;

    std::ifstream file(path);

    if(!file.is_open())
    {
        return std::unexpected(ErrorInfo{404, "Not Found"});
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();

    std::string response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n{}", content.size(), content);

    return response;
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
