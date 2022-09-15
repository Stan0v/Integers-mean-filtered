#include <atomic>
#include <array>

#include "functionwrapper.h"

using namespace std;

// push and pop are accessed by only one single thread
// thread local queue
// push/pop by this single thread could compelete with steal with multiple other threads
class LockFreeWorkStealingQueue
{
private:
	using DataType = FunctionWrapper;
	// change to be template argument in the future
	static constexpr auto DEFAULT_COUNT = 2048u;
	static constexpr auto MASK = DEFAULT_COUNT - 1u;
	array<DataType, DEFAULT_COUNT> q;
	atomic<unsigned int> lock_front{ 0 };
	atomic<unsigned int> lock_back{ 0 };
public:
	LockFreeWorkStealingQueue() {}
	LockFreeWorkStealingQueue(const LockFreeWorkStealingQueue&) = delete;
	LockFreeWorkStealingQueue& operator=(const LockFreeWorkStealingQueue&) = delete;

	/**
	 * add a new item to the back of the queue
	 * runs sequentially with try_pop_back
	 * runs parallel with multiple threads' try_steal_front
	 *
	 * NOTE: If this makes the queue's size 2048 or more, the behavior is undefined or
	 * the contents are overwritten which will never get popped or stolen.
	 * If this is the 2^32th call to push, the behavior is undefined.
	 */
	void push_back(DataType data)
	{
		auto bk = lock_back.load(memory_order_acquire);
		// try resetting the lock_front and lock_back to prevent
		// they being too large - commented out since it causes race conditions
		//if (bk == lock_front.load(memory_order_acquire)) {
		//    lock_front.store(0, memory_order_release);
		//    lock_back.store(0, memory_order_release);
		//}
		//cout << "bk: " << (bk&MASK) << "&q[bk]: " << &(q[bk &MASK]) << endl;
		q[bk & MASK] = move(data);
		lock_back.fetch_add(1, memory_order_release);
	}

	/**
	 * tries to pop an existing item from the back of the queue
	 * runs sequentially with push
	 * runs parallel with multiple threads's try_steal_front
	 */
	bool try_pop_back(DataType& res)
	{
		auto ft = lock_front.load(memory_order_acquire);
		auto bk = lock_back.load(memory_order_acquire);

		if (bk > ft)
		{
			while (bk && !lock_back.compare_exchange_weak(bk, bk - 1, memory_order_release, memory_order_relaxed));
			// cout << "bk-1: " << (bk-1 & MASK) << "q's itemaddr: " << &(q[(bk - 1) & MASK]) << endl;
			res = move(q[(bk - 1) & MASK]);
			return true;
		}

		return false;
	}

	/**
	 * tries to steal from the front of the queue
	 * runs in multiple threads
	 */
	bool try_steal_front(DataType& res)
	{
		auto ft = lock_front.load(memory_order_acquire);
		auto bk = lock_back.load(memory_order_acquire);
		// if there is only one item in the queue, try not steal
		// if stealing, contention with try_pop_back, failed anyway
		if (bk && ft < bk/* - 1*/)
		{
			// while(!lock_front.compare_exchange_weak(ft, ft + 1, memory_order_release, memory_order_relaxed));
			ft = lock_front.fetch_add(1, memory_order_acq_rel);
			// check again to see any changes by push or try_pop_back
			bk = lock_back.load(memory_order_acquire);

			if (ft < bk)
			{
				res = move(q[ft & MASK]);
				return true;
			}
			else
			{
				// nothing to steal, reset lock_front
				lock_front.fetch_sub(1, memory_order_acq_rel);
			}
		}

		return false;
	}
};
