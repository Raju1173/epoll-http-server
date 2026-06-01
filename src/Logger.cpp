#include "Logger.h"
#include <condition_variable>
#include <mutex>
#include <print>
#include <csignal>
#include <format>

static std::queue<LogEntry> logQueue;

static std::mutex logMutex;
static std::condition_variable logCV;

extern std::atomic<bool> Running;

template <> struct std::formatter<messageType>
{
    std::formatter<std::string_view> underlying;

    constexpr auto parse(std::format_parse_context& ctx)
    {
        return underlying.parse(ctx);
    }

    auto format(messageType type, auto& ctx) const
    {
        std::string_view name = "UNKNOWN";
        
        switch (type)
	{
            case messageType::DEBUG:
		name = "DEBUG";
		break;
            case messageType::INFO:
		name = "INFO";
		break;
            case messageType::WARNING:
		name = "WARNING";
		break;
            case messageType::ERROR:
		name = "ERROR";
		break;
        }
        
        return underlying.format(name, ctx);
    }
};

void log(LogEntry log)
{
    {
	std::lock_guard lock(logMutex);

	logQueue.push(std::move(log));
    }

    logCV.notify_one();
}

void logger()
{
    while(Running)
    {
	std::unique_lock lock(logMutex);

	logCV.wait(lock, []{ return !logQueue.empty() || !Running; });

	while(!logQueue.empty())
	{
	    LogEntry front = std::move(logQueue.front());

	    logQueue.pop();

	    lock.unlock();

	    std::print("[{}] - {}\n", front.type, front.message);

	    lock.lock();
	}
    }
}

LoggerGuard::LoggerGuard() : logThread(logger) {}

LoggerGuard::~LoggerGuard() 
{
    Running = false;
    logCV.notify_one();

    if (logThread.joinable()) 
    {
	logThread.join();
    }
}
