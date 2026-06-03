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

class File
{
public:
    int fd;

    explicit File(int fd) : fd(fd) {}

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&& other) noexcept : fd(other.fd)
    {
        other.fd = -1;
    }

    File& operator=(File&& other) noexcept
    {
        if(this != &other)
        {
	    if(fd != -1)
		close(fd);

            fd = other.fd;
            other.fd = -1;
        }

        return *this;
    }

    ~File()
    {
        if(fd != -1)
            close(fd);
    }
};

struct Response
{
    std::string header;

    size_t headerOffset;

    File fileFd;

    off_t fileOffset;

    size_t remainingBytes;
};

struct ClientState
{
    Socket sock;

    std::vector<char> readBuffer;
    size_t parseOffset = 0;
    
    std::deque<Response> responses;

    std::list<ClientState>::iterator selfIt;
};
