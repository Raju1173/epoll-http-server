#include<iostream>
#include<expected>

std::expected<int, Error> hello()
{
	return 1;
}

int main()
{
	std::cout << hello();
	return 0;
}
