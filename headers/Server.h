#include "Socket.h"
#include "Client.h"
#include <cerrno>
#include <cstring>
#include <errno.h>
#include <expected>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include "Logger.h"

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
	    else if (err == EAGAIN || err == EWOULDBLOCK) break;

	    return std::unexpected(ErrorInfo{err, "Failed to read request : " + std::string(strerror(err))});
	}

	if(n == 0)
	{
	    return std::unexpected(ErrorInfo(499, "Client closed request"));
	}
	
	curSize += n;
    }

    clientState.readBuffer.resize(curSize);

    return {};
}

std::expected<Response, ErrorInfo> parse(std::string request)
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

    File file(open(path.c_str(), O_RDONLY));

    if(file.fd == -1)
    {
	int err = errno;

        return std::unexpected(ErrorInfo{-1, std::string("Failed to open file : ") + strerror(err)});
    }

    struct stat st;

    if(fstat(file.fd, &st) == -1)
    {
	int err = errno;

	return std::unexpected(ErrorInfo{-1, std::string("Failed to get file stats : ") + strerror(err)});
    }

    size_t fileSize = st.st_size;

    return Response{std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/html\r\nConnection: keep-alive\r\n\r\n", fileSize), 0, std::move(file), 0, fileSize};
}

std::expected<void, ErrorInfo> writeSock(ClientState& clientState)
{
    while(!clientState.responses.empty())
    {
	Response& response = clientState.responses.front();

	if(response.headerOffset != response.header.length())
	{
	    size_t totalSize = response.header.length();
	    ssize_t n = 0;

	    while(response.headerOffset < totalSize)
	    {
		n = write(clientState.sock.fd, response.header.data() + response.headerOffset, totalSize - response.headerOffset);

		if(n == -1)
		{
		    int err = errno;

		    if(err == EINTR) continue;
		    if(err == EAGAIN || err == EWOULDBLOCK) return {};

		    log({messageType::ERROR, std::string("Failed to write response : ") + strerror(err)});

		    clientState.responses.pop_front();

		    continue;
		}

		response.headerOffset += n;
	    }
	}

	ssize_t n = 0;

	while(response.remainingBytes != 0)
	{
	    n = sendfile(clientState.sock.fd, response.fileFd.fd, &response.fileOffset, response.remainingBytes);

	    if(n == -1)
	    {
		int err = errno;

		if(err == EINTR) continue;
		if(err == EAGAIN || err == EWOULDBLOCK) return {};

		log({messageType::ERROR, std::string("Failed to write response : ") + strerror(err)});

		clientState.responses.pop_front();

		continue;
	    }

	    response.remainingBytes -= n;
	}

	clientState.responses.pop_front();
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
