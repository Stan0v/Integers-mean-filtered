#include <thread>
#include <vector>

using namespace std;

class ThreadWrapper
{
private:
	vector<thread>& threads;
public:
	explicit ThreadWrapper(vector<thread>& ths) : threads(ths) {}
	~ThreadWrapper()
	{
		for (auto& it : threads)
		{
			if (it.joinable())
				it.join();
		}
	}
};
