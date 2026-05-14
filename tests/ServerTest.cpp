#include "Client.h"
#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

using TestFunc = std::function<std::string()>;
using TestSuite = std::vector<TestFunc>;


std::string requestTest(std::string testName, std::string request, std::string expected)
{
    Client client;
    
    auto init = client.init();
    
    if (!init.has_value())
    {
        return testName + " test failed : " + init.error().message;
    }

    auto send = client.send(request);
    
    if (!send.has_value())
    {
	return testName + " test failed : " + send.error().message;
    }

    auto recvCont = client.recieve();
    
    if (!recvCont.has_value())
    {
	if(testName == "Missing CRLF" && recvCont.error().message == "Closed prematurely")
	{
	    return testName + " test passed";
	}

	return testName + " test failed : " + recvCont.error().message;
    }

    if (*recvCont != expected)
    {
	return testName + " test failed : Content mismatch - " + *recvCont;
    }

    return testName + " test passed";
}

std::string sequentialClientsTest(int iterations)
{
    Client client;
    
    for (int i = 0; i < iterations; ++i) 
    {
        auto init = client.init();
        
	if (!init.has_value())
	{
            return "Sequential clients test - Iteration " + std::to_string(i) + " failed : " + init.error().message;
        }

        auto send = client.send("GET / HTTP/1.1\r\n\r\n");
        
	if (!send.has_value())
	{
            return "Sequential clients test - Iteration " + std::to_string(i) + " failed : " + send.error().message;
        }

        auto recv = client.recieve();
        
	if (!recv.has_value())
	{
            return "Sequential clients test - Iteration " + std::to_string(i) + " Recieve failed : " + recv.error().message;
        }
    }

    return "Sequential Clients Test passed";
}

void runTests(const std::string& suiteName, const TestSuite& suite)
{
    std::cout << "==== Running " << suiteName << " ====" << std::endl;

    for (const auto& test : suite)
    {
        std::cout << test() << std::endl;
    }

    std::cout << std::endl;
}

int main()
{
    std::ifstream file("./static/index.html");
    std::stringstream buffer;

    buffer << file.rdbuf();

    std::string indexContent = buffer.str();

    TestSuite requestSuite = 
    {
        [&indexContent]() { return requestTest("GET", "GET / HTTP/1.1\r\n\r\n", std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n{}", indexContent.length(), indexContent)); },        
        []() { return requestTest("Missing CRLF", "GET / HTTP/1.1", "Closed prematurely"); },

        []() { return requestTest("Unsupported Method", "POST / HTTP/1.1\r\n\r\n", "HTTP/1.1 405 Method Not Supported\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"); },

        []() { return requestTest("Path Traversal", "GET /../../etc/passwd HTTP/1.1\r\n\r\n", "HTTP/1.1 403 Directory Traversal Denied\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"); },

        []() { return requestTest("Missing File", "GET /NeverGonnaGiveYouUp.html HTTP/1.1\r\n\r\n", "HTTP/1.1 404 File Not Found\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"); },
    };

    TestSuite connectionSuite = 
    {
	[]() { return sequentialClientsTest(20000); }
    };

    runTests("Request Tests", requestSuite);
    runTests("Connection Tests", connectionSuite);

    return 0;
}
