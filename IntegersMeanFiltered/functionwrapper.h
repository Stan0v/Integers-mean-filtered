#include <memory>
#include <tuple>
#include <utility>

using namespace std;

/**
 * previously used a function<void()> as member
 * but it cannot use packaged_task since function is copy-constructed
 * however packaged_task is move-only, changed it to type-erasure class
 */
class FunctionWrapper
{
private:
	class ImplBase
	{
	public:
		virtual void invoke() = 0;
		virtual ~ImplBase() {}
	};

	unique_ptr<ImplBase> impl;

	template <typename FunctionType, typename... Args>
	class Impl : public ImplBase
	{
	public:
		FunctionType f;
		tuple<Args...> args;
		Impl(FunctionType&& f_, Args&&... args_) : f(move(f_)), args(make_tuple(move(args_)...)) {}

		void invoke() override
		{
			// expland a tuple as it was a parameter pack
			call(make_index_sequence<tuple_size<tuple<Args...>>::value>{});
		}

		template<size_t... Indices>
		void call(const index_sequence<Indices...>)
		{
			f(get<Indices>(forward<tuple<Args...>>(args))...);
		}
	};
public:
	template <typename F, typename... Args>
	FunctionWrapper(F&& f_, Args&&... args_) : impl(new Impl<F, Args...>(move(f_), move(args_)...)) {}

	void operator()() { impl->invoke(); }

	FunctionWrapper() = default;
	FunctionWrapper(FunctionWrapper&& other) : impl(move(other.impl)) {}
	FunctionWrapper& operator=(FunctionWrapper&& other)
	{
		impl = move(other.impl);
		return *this;
	}

	FunctionWrapper(const FunctionWrapper&) = delete;
	FunctionWrapper(FunctionWrapper&) = delete;
	FunctionWrapper& operator=(const FunctionWrapper&) = delete;
};
