#include "Client.h"
#include <chrono>
#include <iostream>
#include <iterator>
#include <ostream>
#include <ratio>

int main()
{
    int requests = 10000;

    int successful = 0;

    auto start = std::chrono::steady_clock::now();

    for(int i = 0; i < requests; i++)
    {
        Client client;
	
	auto init = client.init();

	if(!init.has_value())
	{
	    continue;
	}
	
	auto send = client.send();
	
	if(!send.has_value())
	{
	    continue;
	}

	auto flush = client.flush();

	if(!flush.has_value())
	{
	    continue;
	}

	successful++;
    }

    auto end = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double, std::ratio<1, 1>>(end - start).count();
    
    std::cout << "Successful requests : " << successful << std::endl;
    std::cout << "End to end req/sec : " << requests / seconds << std::endl;

    return 0;
}
