#include <iostream>
#include <string>

/* block comment: NotePadLX Phase 0 sample */
namespace nplx
{
	// Returns a greeting
	constexpr int answer = 42;

	std::string greet(const std::string& who)
	{
		return "hello" + std::string(", ") + who;
	}
}

int main()
{
	for (int i = 0; i < 3; ++i)
		std::cout << nplx::greet("Linux") << ' ' << nplx::answer + i << '\n';
	return 0;
}
