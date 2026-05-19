#pragma once

#include <sys/epoll.h>
#include <sys/socket.h>
#include <string>
#include <unistd.h>
#include <expected>
#include <fcntl.h>
#include <errno.h>

struct ErrorInfo
{
    int code;
    std::string message;
};

class Socket
{
public :
    int fd;
    
    Socket() : fd(-1) {}
    Socket(int fileDesc) : fd(fileDesc) {}
    
    Socket(const Socket&) = delete;
    Socket& operator = (const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd(other.fd)
    {
	other.fd = -1;
    }

    Socket& operator = (Socket&& other) noexcept
    {
	if (this != &other) 
	{
	    if (fd != -1) 
	    {
		close(fd);
	    }
	    
	    fd = other.fd;
	    
	    other.fd = -1;
	}

	return *this;
    }

    ~Socket()
    {
	if (fd != -1)
	    close(fd);
    }
};

std::expected<void, ErrorInfo> setNonBlocking(const Socket& sock)
{
    int flags = fcntl(sock.fd, F_GETFL, 0);

    if(flags == -1)
    {
        int err = errno;

        return std::unexpected(ErrorInfo{err, "F_GETFL failed: " + std::string(strerror(err))});
    }

    if(fcntl(sock.fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        int err = errno;

        return std::unexpected(ErrorInfo{err, "F_SETFL failed: " + std::string(strerror(err))});
    }

    return {};
}
