#include <sys/socket.h>
#include <string>
#include <unistd.h>

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
