#include <queue>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <future>

namespace kiq::katrix
{
using mutex_t  = std::mutex;
using cond_t   = std::condition_variable;
using lock_t   = std::lock_guard<std::mutex>;
using u_lock_t = std::unique_lock<std::mutex>;

template <typename T = std::function<void()>>
class reactive_queue
{
using queue_t  = std::queue<T>;
//-------------------------------------------------
queue_t queue_;
mutex_t mutex_;
cond_t  cond_;
//-------------------------------------------------
public:
void push(T fn)
{
  {
    lock_t lock(mutex_);
    queue_.push(fn);
  }
  cond_.notify_one();
}
//-------------------------------------------------
T wait_and_pop()
{
  u_lock_t lock(mutex_);
  while (queue_.empty())
    cond_.wait(lock);
  const auto fn = queue_.front();
  queue_.pop();
  return fn;
}
//-------------------------------------------------
};
//-------------------------------------------------
//-------------------------------------------------
template <typename T = std::function<void()>>
class synchronized_object
{
  using future_t = std::future<void>;
  using pred_fn  = std::function<bool()>;
//-------------------------------------------------

protected:
  reactive_queue<T> queue_;
  bool              done_{false};
  future_t          fut_;
  pred_fn           pred_;
  cond_t            cond_;
  mutex_t           mutex_;
//-------------------------------------------------
public:
  synchronized_object(pred_fn predicate)
  : pred_(predicate)
  {
    run();
  }
//-------------------------------------------------
  ~synchronized_object()
  {
    put([this]() noexcept { done_ = true; });
    if (fut_.valid())
      fut_.wait();
  }
//-------------------------------------------------
  virtual void run()
  {
    fut_ = std::async(std::launch::async, [this]
    {
      while (!done_)
      {
        auto fn = queue_.wait_and_pop();
        {
          std::unique_lock<std::mutex> lock(mutex_);
          cond_.wait(lock, pred_);
          fn();
        }
        cond_.notify_one();
      }
    });
  }
//-------------------------------------------------
  void put(T&& fn)
  {
    queue_.push(std::move(fn));
  }
};
} // ns kiq::katrix
