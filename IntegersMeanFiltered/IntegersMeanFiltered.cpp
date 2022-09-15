#include "IntegersMeanFiltered.h"
#include "threadpool.h"
#include "miniselect/floyd_rivest_select.h"
#include <vector>
//#include <c:/dev/go/highway/hwy/contrib/sort/vqsort.h>
//#include "benchmark/benchmark.h"
#include<array>
#include<map>
#include<thread>
#include<future>
#include<latch>
#include <iterator>
#include <random>
#include <algorithm>
#include <limits>

using namespace std;

#define NUMBERS_COUNT 10000000
#define TEST_ITERATIONS_COUNT 10
#define SINGLE_VS_MULTI_AVG_DIFF_IN_NS 10000

// logical cores apparently
// it needs to be checked if hardware_concurrency returned 0 and therefore divide work using another algorithm
unsigned long const hardware_threads = thread::hardware_concurrency();

//#ifdef THREAD_POOL
thread_local LockFreeWorkStealingQueue* ThreadPool::local_queue;
thread_local unsigned int ThreadPool::idx;

ThreadPool threadpool(hardware_threads);
//#endif

template<class Iterator>
int FilterByMediansInPlace(Iterator first, Iterator last, float median, float bottomLimit, float upperLimit)
{
	int result = 0;

	if (bottomLimit < upperLimit)
	{
		auto length = distance(first, last);
		auto start = bottomLimit >= median ? first + length / 2 : first;
		auto end = upperLimit <= median ? first + length / 2 : prev(last);
		auto count = distance(start, end);

		for (auto i = 0; i < count; ++i)
		{
			if (*start > bottomLimit && *start < upperLimit)
			{
				start = next(start);
				++result;
			}
			else
			{
				swap(*start, *end);
				end = prev(end);
			}
		}
	}

	return result;
}

//TODO: support iterator template
list<int> FilterByMedians(vector<int>::iterator first, vector<int>::iterator last,
	float median, float bottomLimit, float upperLimit)
{
	list<int> result;

	if (bottomLimit < upperLimit)
	{
		for (; first != last; first = next(first))
		{
			if (*first > bottomLimit && *first < upperLimit)
				result.insert(result.end(), *first);
		}
	}

	return result;
}

template<class Iterator>
void FindAndStoreMedian(Iterator first, Iterator last, const unsigned long part_idx,
	map<unsigned long, unique_ptr<atomic<float>>>& medians)
{
	unsigned long const length = distance(first, last);

	if (length % 2 != 0)
	{
		miniselect::floyd_rivest_select(first, first + length / 2, last);
		medians[part_idx]->store(*(first + length / 2), memory_order_relaxed);
	}
	else
	{
		miniselect::floyd_rivest_select(first, first + length / 2, last);
		miniselect::floyd_rivest_select(first, first + (length - 1) / 2, last);
		medians[part_idx]->store(static_cast<float>(*(first + length / 2) + *(first + (length - 1) / 2)) / 2, memory_order_relaxed);
	}
}

template<class Iterator>
void FindAndStoreMedian(Iterator first, Iterator last, const unsigned long part_idx,
	map<unsigned long, float>& medians)
{
	unsigned long const length = distance(first, last);

	if (length % 2 != 0)
	{
		miniselect::floyd_rivest_select(first, first + length / 2, last);
		medians[part_idx] = *(first + length / 2);
	}
	else
	{
		miniselect::floyd_rivest_select(first, first + length / 2, last);
		miniselect::floyd_rivest_select(first, first + (length - 1) / 2, last);
		medians[part_idx] = static_cast<float>(*(first + length / 2) + *(first + (length - 1) / 2));
	}
}

template<typename Iterator>
struct MeanFilterInPlace
{
	int operator()(Iterator first, Iterator last,
		const unsigned long part_idx, map<unsigned long, unique_ptr<latch>>& jobIsReadyToContinueSignals,
		map<unsigned long, unique_ptr<atomic<float>>>& medians)
	{
		FindAndStoreMedian(first, last, part_idx, medians);

		if (part_idx > 0)
			jobIsReadyToContinueSignals[part_idx - 1]->count_down();

		if (part_idx < jobIsReadyToContinueSignals.size() - 1)
			jobIsReadyToContinueSignals[part_idx + 1]->count_down();

		jobIsReadyToContinueSignals[part_idx]->wait();

		float bottomLimit = part_idx > 0 ?
			medians[part_idx - 1]->load(memory_order_relaxed) :
			numeric_limits<float>::lowest();
		auto nextMedianIt = medians.find(part_idx + 1);
		float upperLimit = nextMedianIt != medians.end() ?
			nextMedianIt->second->load(memory_order_relaxed) :
			numeric_limits<float>::max();
		auto median = medians[part_idx]->load(memory_order_relaxed);

		return FilterByMediansInPlace(first, last, median, bottomLimit, upperLimit);
	}
};

//TODO: support iterator template
list<int> MeanFilter(vector<int>::iterator first, vector<int>::iterator last,
	const unsigned long part_idx, map<unsigned long, unique_ptr<latch>>& jobIsReadyToContinueSignals,
	map<unsigned long, unique_ptr<atomic<float>>>& medians)
{
	FindAndStoreMedian(first, last, part_idx, medians);

	if (part_idx > 0)
		jobIsReadyToContinueSignals[part_idx - 1]->count_down();

	if (part_idx < jobIsReadyToContinueSignals.size() - 1)
		jobIsReadyToContinueSignals[part_idx + 1]->count_down();

	jobIsReadyToContinueSignals[part_idx]->wait();

	float bottomLimit = part_idx > 0 ?
		medians[part_idx - 1]->load(memory_order_relaxed) :
		numeric_limits<float>::lowest();
	auto nextMedianIt = medians.find(part_idx + 1);
	float upperLimit = nextMedianIt != medians.end() ?
		nextMedianIt->second->load(memory_order_relaxed) :
		numeric_limits<float>::max();
	auto median = medians[part_idx]->load(memory_order_relaxed);
	unsigned long const length = distance(first, last);

	auto start = bottomLimit >= median ? first + length / 2 : first;
	auto end = upperLimit <= median ? first + length / 2 : last;
	auto count = distance(start, end);

	auto mid = start + count / 2;
	auto lastPartResult = threadpool.submit(FilterByMedians, mid, end, median, bottomLimit, upperLimit);
	auto result = FilterByMedians(start, mid, median, bottomLimit, upperLimit);

	while (lastPartResult.wait_for(chrono::seconds(0)) == future_status::timeout)
		threadpool.run_pending_task();

	result.splice(result.end(), lastPartResult.get());
	return result;
}

template<class Iterator>
void FillRandomInt(Iterator first, Iterator last, int min, int max)
{
	static random_device rd;
	static mt19937 mte(rd());
	uniform_int_distribution<int> dist(min, max);

	generate(first, last, [&]() { return dist(mte); });
}

long long single_thread(int numbersCount)
{
	static vector<int> numbers(numbersCount);
	FillRandomInt(numbers.begin(), numbers.end(), INT_MIN, INT_MAX);
	typedef vector<int>::iterator iteratorType;
	unsigned long block_size = numbers.size() / hardware_threads;
	// make block_size value odd to improve process of median calculation, only last part could be even then
	block_size = block_size % 2 == 0 ? block_size - 1 : block_size;

	try
	{
		// while it is not required to use atomics here, it is left here to simplify current development
		// besides it influences on performance very slightly
		map<unsigned long, float> medians;
		vector<int> filteredShifts(hardware_threads);

		auto start = chrono::steady_clock::now();
		auto block_start = numbers.begin();
		unsigned long i;

		for (i = 0; i < (hardware_threads - 1); ++i)
		{
			auto block_end = block_start;
			advance(block_end, block_size);
			FindAndStoreMedian(block_start, block_end, i, medians);
			block_start = block_end;
		}

		FindAndStoreMedian(block_start, numbers.end(), i, medians);
		block_start = numbers.begin();
		float bottomLimit = numeric_limits<float>::lowest();
		float median;

		for (i = 0; i < (hardware_threads - 1); ++i)
		{
			auto block_end = block_start;
			median = medians[i];
			advance(block_end, block_size);
			filteredShifts[i] = FilterByMediansInPlace(block_start, block_end,
				median, bottomLimit, medians[i + 1]);
			block_start = block_end;
			bottomLimit = median;
		}

		filteredShifts[i] = FilterByMediansInPlace(block_start, numbers.end(),
			medians[i], bottomLimit, numeric_limits<float>::max());

		block_start = numbers.begin();
		auto block_end = block_start;
		advance(block_start, filteredShifts[0]);

		for (int j = 0, i = 1; i < hardware_threads; ++i)
		{
			advance(block_end, block_size);

			if (filteredShifts[i] == 0)
				continue;

			rotate(block_start, block_end, block_end + filteredShifts[i]);
			advance(block_start, filteredShifts[i]);
			++j;
		}

		auto end = chrono::steady_clock::now();
		return chrono::duration_cast<chrono::nanoseconds>(end - start).count();
		// here we have final result in-place of the input array ranged from the start till start + filteredShifts sum
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
}

long long static_threads(int numbersCount)
{
	static vector<int> numbers(numbersCount);
	FillRandomInt(numbers.begin(), numbers.end(), INT_MIN, INT_MAX);
	typedef vector<int>::iterator iteratorType;
	unsigned long block_size = numbers.size() / hardware_threads;
	// make block_size value odd to improve process of median calculation, only last part could be even then
	block_size = block_size % 2 == 0 ? block_size - 1 : block_size;

	try
	{
		map<unsigned long, unique_ptr<latch>> jobIsReadyToContinueSignals;
		map<unsigned long, unique_ptr<atomic<float>>> medians;
		vector<int> filteredShifts(hardware_threads);

		medians[0] = make_unique<atomic<float>>(0);
		jobIsReadyToContinueSignals[0] = make_unique<latch>(1);

		for (unsigned long i = 1; i < hardware_threads - 1; ++i)
		{
			jobIsReadyToContinueSignals[i] = make_unique<latch>(2);
			medians[i] = make_unique<atomic<float>>(0);
		}

		jobIsReadyToContinueSignals[hardware_threads - 1] = make_unique<latch>(1);
		medians[hardware_threads - 1] = make_unique<atomic<float>>(0);

		vector<future<int>> futures(hardware_threads - 1);
		vector<thread> threads(hardware_threads - 1);
		ThreadWrapper joiner(threads);

		auto start = chrono::steady_clock::now();
		auto block_start = numbers.begin();
		unsigned long i;

		for (i = 0; i < (hardware_threads - 1); ++i)
		{
			auto block_end = block_start;
			advance(block_end, block_size);

			packaged_task<int(iteratorType, iteratorType,
				const unsigned long&, map<unsigned long, unique_ptr<latch>>&,
				map<unsigned long, unique_ptr<atomic<float>>>&)> workload((MeanFilterInPlace<iteratorType>()));
			futures[i] = workload.get_future();
			threads[i] = thread(move(workload), block_start, block_end, i, ref(jobIsReadyToContinueSignals), ref(medians));

			block_start = block_end;
		}

		filteredShifts[i] =
			MeanFilterInPlace<iteratorType>()(block_start, numbers.end(), i, ref(jobIsReadyToContinueSignals), ref(medians));

		for (i = 0; i < (hardware_threads - 1); ++i)
		{
			filteredShifts[i] = futures[i].get();
		}

		block_start = numbers.begin();
		auto block_end = block_start;
		advance(block_start, filteredShifts[0]);

		for (int j = 0, i = 1; i < hardware_threads; ++i)
		{
			advance(block_end, block_size);

			if (filteredShifts[i] == 0)
				continue;

			rotate(block_start, block_end, block_end + filteredShifts[i]);
			advance(block_start, filteredShifts[i]);
			++j;
		}

		auto end = chrono::steady_clock::now();
		return chrono::duration_cast<chrono::nanoseconds>(end - start).count();
		// here we have final result in-place of the input array ranged from the start till start + filteredShifts sum
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
}

long long thread_pool(int numbersCount)
{
	static vector<int> numbers(numbersCount);
	FillRandomInt(numbers.begin(), numbers.end(), INT_MIN, INT_MAX);
	typedef vector<int>::iterator iteratorType;
	unsigned long block_size = numbers.size() / hardware_threads;
	// make block_size value odd to improve process of median calculation, only last part could be even then
	block_size = block_size % 2 == 0 ? block_size - 1 : block_size;

	try
	{
		map<unsigned long, unique_ptr<latch>> jobIsReadyToContinueSignals;
		map<unsigned long, unique_ptr<atomic<float>>> medians;

		medians[0] = make_unique<atomic<float>>(0);
		jobIsReadyToContinueSignals[0] = make_unique<latch>(1);

		for (unsigned long i = 1; i < hardware_threads - 1; ++i)
		{
			jobIsReadyToContinueSignals[i] = make_unique<latch>(2);
			medians[i] = make_unique<atomic<float>>(0);
		}

		jobIsReadyToContinueSignals[hardware_threads - 1] = make_unique<latch>(1);
		medians[hardware_threads - 1] = make_unique<atomic<float>>(0);
		vector<future<list<int>>> futures(hardware_threads);

		auto start = chrono::steady_clock::now();
		auto block_start = numbers.begin();
		unsigned long i;

		for (i = 0; i < (hardware_threads - 1); ++i)
		{
			auto block_end = block_start;
			advance(block_end, block_size);
			futures[i] = threadpool.submit(MeanFilter, block_start, block_end, i, ref(jobIsReadyToContinueSignals), ref(medians));
			block_start = block_end;
		}

		futures[i] = threadpool.submit(MeanFilter, block_start, numbers.end(), i, ref(jobIsReadyToContinueSignals), ref(medians));

		for (i = 0; i < hardware_threads; ++i)
			futures[i].wait();

		list<int> result;

		for (i = 0; i < hardware_threads; ++i)
			result.splice(result.end(), futures[i].get());

		// return result;
		auto end = chrono::steady_clock::now();
		return chrono::duration_cast<chrono::nanoseconds>(end - start).count();
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2 || (argv[1][0] != '0' && argv[1][0] != '1' && argv[1][0] != '2' && argv[1][0] != '3'))
	{
		cout << "\nPlease choose an option of processing: \n";
		cout << "0 - single threaded; 1 - static threads; 2 - thread pool; 3 - find single thread advantage \n";

		return 0;
	}

	long long (*choice) (int);
	char option = argv[1][0];
	cout << "Array size: " << NUMBERS_COUNT << " Iteration count: " << TEST_ITERATIONS_COUNT << "\n";

	if (option == '3')
	{
		auto currentTimeDiff = INT_MAX;
		auto numbersCount = NUMBERS_COUNT;
		auto numbersCountDiff = NUMBERS_COUNT;

		for (auto numbersCountDiff = -numbersCount / 2; numbersCountDiff != 0; numbersCountDiff = numbersCountDiff / 2)
		{
			long long totalSingleTimeElapsed = 0;

			for (auto i = 0; i < TEST_ITERATIONS_COUNT; ++i)
				totalSingleTimeElapsed += single_thread(numbersCount);

			cout << "Single time: " << totalSingleTimeElapsed << "\n";
			long long totalMultiTimeElapsed = 0;

			for (auto i = 0; i < TEST_ITERATIONS_COUNT; ++i)
				totalMultiTimeElapsed += thread_pool(numbersCount);

			cout << "Multi time: " << totalMultiTimeElapsed << "\n";

			if (totalSingleTimeElapsed > totalMultiTimeElapsed)
			{
				numbersCountDiff *= numbersCountDiff < 0 ? 1 : -1;
				currentTimeDiff = (totalSingleTimeElapsed - totalMultiTimeElapsed) / TEST_ITERATIONS_COUNT;
			}
			else
			{
				numbersCountDiff *= numbersCountDiff > 0 ? 4 : -1;
				currentTimeDiff = (totalMultiTimeElapsed - totalSingleTimeElapsed) / TEST_ITERATIONS_COUNT;
			}

			cout << "Numbers count: " << numbersCount << " Count diff: " << numbersCountDiff << " Time difference: " << currentTimeDiff << "\n";

			if (currentTimeDiff > SINGLE_VS_MULTI_AVG_DIFF_IN_NS)
				numbersCount += numbersCountDiff;
			else
				break;
		}

		cout << "Numbers count: " << numbersCount << " Time difference: " << currentTimeDiff << "\n";
	}
	else
	{
		switch (option)
		{
		case '0':
			choice = single_thread;
			cout << "SINGLE THREAD execution\n";
			break;
		case '1':
			choice = static_threads;
			cout << "STATIC THREADS execution\n";
			break;
		case '2':
			choice = thread_pool;
			cout << "THREAD POOL execution\n";
			break;
		default:
			break;
		}

		long long totalTimeElapsed = 0;

		for (auto i = 0; i < TEST_ITERATIONS_COUNT; ++i)
			totalTimeElapsed += choice(NUMBERS_COUNT);

		cout << "Average time, ns: " << totalTimeElapsed / TEST_ITERATIONS_COUNT;
	}
	//static array<int, NUMBERS_COUNT> numbers = {55,4,10,12,33,2,15,99,4,12,44,23};

	return 1;
}