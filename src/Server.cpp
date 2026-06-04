#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/epoll.h>
#include "Server.h"
#include "Logger.h"

std::atomic<bool> Running = true;

void handleSIGINT(int signum)
{
    Running = false;
}

int main()
{
    LoggerGuard logger;

    unsigned int threadCount = 4;

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (unsigned int i = 0; i < threadCount; ++i)
    {
        workers.emplace_back(worker); 
    }

    struct sigaction action = {0};
    action.sa_handler = &handleSIGINT;
    sigaction(SIGINT, &action, NULL);

    for (auto& t : workers)
    {
        if (t.joinable())
		{
            t.join();
        }
    }

    return EXIT_SUCCESS;
}
