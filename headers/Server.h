#include "Socket.h"
#include "Client.h"
#include <asm-generic/socket.h>
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
#define MAX_EVENTS 1024

extern std::atomic<bool> Running;

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

    if (setsockopt(sock.fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1)
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
            return std::unexpected(ErrorInfo{499, "Client closed request"});
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

void worker()
{
    auto serverSock = initializeServer();

    if(!serverSock)
    {
        log({messageType::ERROR, serverSock.error().message});
        
        return;
    }

    auto serverNonBlock = setNonBlocking((*serverSock));

    if(!serverNonBlock.has_value())
    {
        log({messageType::ERROR, serverNonBlock.error().message});
        
        return;
    }

    int epollfd = epoll_create1(0);

    if(epollfd == -1)
    {
        int err = errno;

        log({messageType::ERROR, std::string("Failed to create epoll instance : ") + strerror(err)});
        
        return;
    }

    struct epoll_event epevent;

    epevent.events = EPOLLIN;
    epevent.data.fd = (*serverSock).fd;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, (*serverSock).fd, &epevent) == -1)
    {
        int err = errno;

        log({messageType::ERROR, std::string("Failed to register server socket to epoll : ") + strerror(err)});

        return;
    }

    struct epoll_event events[MAX_EVENTS];

    struct sockaddr_in clAddr;
    socklen_t clAddrSize = sizeof(clAddr);

    //will replace this with arenas later
    std::list<ClientState> clientStates;

    while(Running)
    {
        int revents = epoll_wait(epollfd, events, MAX_EVENTS, -1);

        for(int i = 0; i < revents; i++)
        {
            if(events[i].data.fd == (*serverSock).fd)
            {
                if(events[i].events & EPOLLERR)
                {
                    Running = false;

                    continue;
                }

                if(events[i].events & EPOLLIN)
                {
                    while(true)
                    {
                        auto clientState = acceptClient(*serverSock, (struct sockaddr*) &clAddr, &clAddrSize);

                        if(!clientState.has_value())
                        {
                            if (clientState.error().code == EINTR) Running = false;
                            if(clientState.error().code == EAGAIN || clientState.error().code == EWOULDBLOCK) break;

                            log({messageType::ERROR, "Accept failed : " + clientState.error().message});

                            continue;
                        }

                        auto clientNonBlock = setNonBlocking((*clientState).sock);

                        if(!clientNonBlock.has_value())
                        {
                            log({messageType::ERROR, clientNonBlock.error().message});

                            continue;
                        }

                        clientStates.push_back(std::move(*clientState));

                        clientStates.back().selfIt = std::prev(clientStates.end());

                        struct epoll_event epevent;

                        epevent.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
                        epevent.data.ptr = &clientStates.back();

                        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, clientStates.back().sock.fd, &epevent) == -1)
                        {
                            int err = errno;

                            log({messageType::ERROR, std::string("Failed to register client socket to epoll : ") + strerror(err)});

                            clientStates.erase(clientStates.back().selfIt);

                            continue;
                        }
                    }
                }

                continue;
            }

            ClientState* eventClientState = ((ClientState*)(events[i].data.ptr));

            if(events[i].events & (EPOLLHUP | EPOLLERR))
            {
                clientStates.erase(eventClientState->selfIt);

                continue;
            }

            if(events[i].events & EPOLLIN)
            {
                auto read = readSock(*eventClientState);

                if(!read.has_value())
                {
                    clientStates.erase(eventClientState->selfIt);

                    log({messageType::ERROR, read.error().message});

                    continue;
                }

                if(eventClientState->readBuffer.size() >= 4)
                {
                    size_t searchStart = (eventClientState->parseOffset >= 3) ? eventClientState->parseOffset - 3 : 0;
                    size_t searchLen = eventClientState->readBuffer.size() - searchStart;
                    
                    //This works because the server only supports GET requests...
                    char *requestEnd = (char*)memmem(eventClientState->readBuffer.data() + searchStart, searchLen, "\r\n\r\n", 4);

                    while(requestEnd != nullptr)
                    {
                        auto start = eventClientState->readBuffer.begin();
                        auto end = eventClientState->readBuffer.begin() + ((requestEnd + 4) - eventClientState->readBuffer.data());

                        auto response = parse(std::string(start, end));

                        if(!response.has_value())
                        {
                            log({messageType::ERROR, response.error().message});
                        }
                        else
                        {
                            eventClientState->responses.push_back(std::move(*response));
                        }

                        eventClientState->readBuffer.erase(start, end);
                        eventClientState->parseOffset = 0;

                        if(eventClientState->readBuffer.size() >= 4)
                        {
                            requestEnd = (char*)memmem(eventClientState->readBuffer.data(), eventClientState->readBuffer.size(), "\r\n\r\n", 4);
                        }

                        else
                        {
                            requestEnd = nullptr; 
                        }
                    }

                    eventClientState->parseOffset = eventClientState->readBuffer.size();
                }
            }

            if (events[i].events & EPOLLOUT)
            {
                auto write = writeSock(*eventClientState);

                if(!write.has_value())
                {
                    log({messageType::ERROR, write.error().message});

                    continue;
                }
            }
        }
    }
}
