#pragma once

#include <string>
#include <thread>

enum class messageType
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

struct LogEntry
{
    messageType type;
    std::string message;
};

void log(LogEntry log);

void logger();

class LoggerGuard 
{
    std::thread logThread;

public :
    LoggerGuard();

    ~LoggerGuard();
};
