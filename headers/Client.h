#pragma once

#include "Socket.h"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <deque>
#include <expected>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <list>

struct ClientState
{
    Socket sock;

    std::vector<char> readBuffer;
    size_t parseOffset = 0;
    
    std::deque<std::string> responses;
    size_t bytesSent = 0;

    std::list<ClientState>::iterator selfIt;
};

class testClient
{
public:
    Socket sock;

    std::expected<void, ErrorInfo> init()
    {
	sock = Socket(socket(AF_INET, SOCK_STREAM, 0));

	if (sock.fd == -1)
	{
	    int err = errno;
	    return std::unexpected(ErrorInfo{err, "Failed to create socket : " + std::string(strerror(err))});
	}

	struct sockaddr_in addr = {AF_INET, htons(8080), {0}};
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(sock.fd, (const sockaddr *)&addr, sizeof(addr)) == -1)
	{
	    int err = errno;
	    return std::unexpected(ErrorInfo{err, "Failed to connect to server : " + std::string(strerror(err))});
	}

	return {};
    }

    std::expected<void, ErrorInfo> send(const std::string &request)
    {
	size_t totalSize = request.length();
	size_t totalSent = 0;
	ssize_t n = 0;

	while (totalSent < totalSize)
	{
	    n = write(sock.fd, request.data() + totalSent, totalSize - totalSent);

	    if (n == -1) 
	    {
		int err = errno;

		if (errno == EINTR)
		    continue;

		return std::unexpected(ErrorInfo{err, "Failed to write response : " + std::string(strerror(err))});
	    }

	    totalSent += n;
	}

	return {};
    }

    std::expected<std::string, ErrorInfo> recieve()
    {
	std::vector<char> buffer;
	buffer.resize(1024);
	size_t curSize = 0;

	while (true)
	{
	    if (curSize >= buffer.size())
		buffer.resize(buffer.size() * 2);

	    ssize_t n = read(sock.fd, buffer.data() + curSize, buffer.size() - curSize);

	    if (n < 0)
	    {
		if (errno == EINTR)
		    continue;
		return std::unexpected(ErrorInfo{errno, "Read error"});
	    }

	    if (n == 0)
		return std::unexpected(ErrorInfo{599, "Closed prematurely"});

	    curSize += n;
	    std::string current(buffer.data(), curSize);
	    size_t headerEnd = current.find("\r\n\r\n");

	    if (headerEnd != std::string::npos)
	    {
		size_t totalHeaderLen = headerEnd + 4;

		size_t pos = current.find("Content-Length: ");

		if (pos == std::string::npos)
		    return std::unexpected(ErrorInfo{-1, "No Content-Length"});

		size_t start = pos + 16;
		size_t end = current.find("\r\n", start);
		long contLen = std::stol(std::string(current.substr(start, end - start)));

		std::string body;
		body.reserve(contLen);

		size_t bytesAlreadyRead = curSize - totalHeaderLen;

		if (bytesAlreadyRead > 0)
		{
		    body.append(buffer.data() + totalHeaderLen, bytesAlreadyRead);
		}

		while (body.size() < (size_t)contLen)
		{
		    char temp[4096];

		    n = read(sock.fd, temp, std::min(sizeof(temp), static_cast<size_t>(contLen - body.size())));

		    if (n <= 0)
			break;

		    body.append(temp, n);
		}

		return std::string(current.substr(0, totalHeaderLen)) + body;
	    }
	}
    }
};
