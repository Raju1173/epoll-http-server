#include "Socket.h"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <expected>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

class Client
{
public :
    Socket sock;

    std::expected<void, ErrorInfo> init()
    {
	sock = Socket(socket(AF_INET, SOCK_STREAM, 0));

	if(sock.fd == -1)
	{
	    int err = errno;
	    return std::unexpected(ErrorInfo{err, "Failed to create socket : " + std::string(strerror(err))});
	}
	
	//only targeting localhost to avoid measuring network overhead in benchmarks...
	struct sockaddr_in addr = {AF_INET, htons(8080), {0}};
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if(connect(sock.fd, (const sockaddr*) &addr, sizeof(addr)) == -1)
	{
	    int err = errno;
	    return std::unexpected(ErrorInfo{err, "Failed to connect to server : " + std::string(strerror(err))});
	}

	return {};
    }

    std::expected<void, ErrorInfo> send(std::string fileName = "")
    {
	std::string request = "GET /" + fileName + " HTTP/1.1\r\n\r\n";

	size_t totalSize = request.length();
	size_t totalSent = 0;
	ssize_t n = 0;

	while(totalSent < totalSize)
	{
	    n = write(sock.fd, request.data() + totalSent, totalSize - totalSent);

	    if(n == -1)
	    {
		int err = errno;

		if(errno == EINTR) continue;

		return std::unexpected(ErrorInfo{err, "Failed to write response : " + std::string(strerror(err))});
	    }

	    totalSent += n;
	}

	return {};
    }

    std::expected<void, ErrorInfo> flush()
    {
	char buf[4096];

	while(true)
	{
	    ssize_t n = read(sock.fd, buf, sizeof(buf));

	    if(n == -1)
	    {
		int err = errno;

		if(err == EINTR)
		    continue;

		return std::unexpected(ErrorInfo{err, "Failed to read response : " + std::string(strerror(err))});
	    }

	    if(n == 0)
	    {
		break;
	    }
	}

	return {};
    }
};
