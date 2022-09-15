#include <mutex>
#include <condition_variable>
#include <memory>

// for debug
#include <cassert>

using namespace std;

template <typename T>
class ThreadSafeQueue
{
private:
	struct node
	{
		shared_ptr<T> data;
		unique_ptr<node> next;
	};

	mutex lock_head;
	mutex lock_tail;
	unique_ptr<node> head;
	node* tail;
	condition_variable data_cond;

	// grab lock_tail and get the tail
	node* get_tail()
	{
		lock_guard<mutex> tail_lk(lock_tail);
		return tail;
	}

	// pop head without grabing lock_head
	unique_ptr<node> pop_head()
	{
		unique_ptr<node> old_head = move(head);
		assert(old_head->next != nullptr);
		head = move(old_head->next);
		assert(head != nullptr);
		return old_head;
	}

	// wait to get lock_head
	unique_lock<mutex> wait_for_data()
	{
		unique_lock<mutex> head_lk(lock_head);
		data_cond.wait(head_lk, [&] { return head.get() != get_tail(); });
		return move(head_lk);
	}

	// wait, grab lock_head and pop head
	unique_ptr<node> wait_pop_head()
	{
		unique_lock<mutex> head_lk(&ThreadSafeQueue::wait_for_data);
		return pop_head();
	}

	// wait, grab lock_head and pop head
	unique_ptr<node> wait_pop_head(T& value)
	{
		unique_lock<mutex> head_lk(&ThreadSafeQueue::wait_for_data);
		value = move(*(head->data));
		return pop_head();
	}

	unique_ptr<node> try_pop_head()
	{
		lock_guard<mutex> head_lk(lock_head);

		if (head.get() == get_tail())
			return unique_ptr<node>{};

		return pop_head();
	}

	unique_ptr<node> try_pop_head(T& value)
	{
		lock_guard<mutex> head_lk(lock_head);

		if (head.get() == get_tail())
			return unique_ptr<node>{};

		// assert(head != nullptr);
		value = move(*(head->data));
		return pop_head();
	}

public:
	// the queue is empty, head and tail points to the same object
	ThreadSafeQueue() : head(new node), tail(head.get()) {}

	ThreadSafeQueue(const ThreadSafeQueue& other) = delete;
	ThreadSafeQueue& operator=(const ThreadSafeQueue& other) = delete;

	void push(T value);

	shared_ptr<T> wait_and_pop()
	{
		unique_ptr<node> old_head = wait_pop_head();
		return old_head->data;
	}

	void wait_and_pop(T& value)
	{
		unique_ptr<node> old_head = wait_pop_head(value);
	}

	shared_ptr<T> try_pop()
	{
		unique_ptr<node> old_head = try_pop_head();
		return old_head ? old_head->data : shared_ptr<T>{};
	}

	bool try_pop(T& value)
	{
		unique_ptr<node> old_head = try_pop_head(value);
		return old_head ? true : false;
	}

	bool empty()
	{
		lock_guard<mutex> head_lk(lock_head);
		return head.get() == get_tail();
	}
};

template <typename T>
void ThreadSafeQueue<T>::push(T value)
{
	shared_ptr<T> new_value(make_shared<T>(move(value)));
	unique_ptr<node> p(new node);
	{
		lock_guard<mutex> tail_lk(lock_tail);
		tail->data = new_value;
		node* const new_tail = p.get();
		tail->next = move(p);
		tail = new_tail;
	}
	data_cond.notify_one();
}
