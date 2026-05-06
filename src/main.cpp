#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <errno.h>
#include <system_error>
#include <netinet/in.h>
#include <unistd.h>

#define LISTEN_BACKLOG 64

int main()
{
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if(sockfd == -1)
  {
    throw std::system_error(errno, std::system_category(), "Failed to create socket"); //Fine for now but create a centralized function for handling errors later
  }

  struct sockaddr_in addr = {AF_INET, htons(8080), {0}};

  if(bind(sockfd, (const sockaddr*)&addr, sizeof(addr)) == -1)
  {
    throw std::system_error(errno, std::system_category(), "Failed to bind socket");
  }

  if(listen(sockfd, LISTEN_BACKLOG) == -1)
  {
    throw std::system_error(errno, std::system_category(), "Failed to set socket as passive");
  }
  
  struct sockaddr_in clAddr;
  socklen_t clAddrSize = sizeof(clAddr);

  int clientfd = accept(sockfd, (struct sockaddr *) &clAddr, &clAddrSize);

  if(clientfd == -1)
  {
    throw std::system_error(errno, std::system_category(), "Failed to accept connection");
  }

  if (close(sockfd) == -1)
  {
    throw std::system_error(errno, std::system_category(), "Failed to close socket");
  }

  return 0;
}
