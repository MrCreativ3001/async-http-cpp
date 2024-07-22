#ifndef CPP_ASYNC_HTTP_FUTURE_H
#define CPP_ASYNC_HTTP_FUTURE_H

#include "utils.h"

template <typename T>
class Poll {
   private:
    Optional<T> value;

   public:
    static Poll<T> pending() {
        Poll<T> poll;
        poll.value = Optional<T>::empty();
        return poll;
    }

    static Poll<T> ready(T value) {
        Poll<T> poll;
        poll.value = Optional<T>::of(value);
        return poll;
    }

    bool isPending() { return value.isEmpty(); }

    bool isReady() { return !isPending(); }

    T get() { return value.get(); }

    operator bool() { return isReady(); }
};

// After the first call to poll, the future should never be moved or copied.
template <class Derived, typename Output>
class Future {
   public:
    Poll<Output> poll() = delete;
};

template <typename Output>
class VirtualFuture : public Future<VirtualFuture<Output>, Output> {
   public:
    Poll<Output> poll() { return Poll<Output>::pending(); }
};

template <typename T>
class Instant : public Future<Instant<T>, T> {
   public:
    explicit Instant(T t) : t(t) {}

    Poll<T> poll() { return Poll<T>::ready(t); }

   private:
    T t;
};

namespace template_utils {

template <typename F>
struct future_output {
    typedef decltype(declval<typename remove_pointer<F>::type>().poll().get())
        type;
};

};  // namespace template_utils

template <typename F>
typename template_utils::future_output<F>::type blockOn(F future) {
    Poll<typename template_utils::future_output<F>::type> poll;

    // This should be compiler optimized
    if constexpr (template_utils::is_pointer<F>::value) {
        while ((poll = future->poll()).isPending()) {
        }
    } else {
        while ((poll = future.poll()).isPending()) {
        }
    }

    return poll.get();
}

// TODO: Autodected pointers
#define AWAIT(future, outputName)                                              \
    Poll<typename template_utils::future_output<decltype(future)>::type> poll; \
    poll = future.poll();                                                      \
    if (poll.isPending()) {                                                    \
        return Poll<typename template_utils::future_output<                    \
            decltype(this)>::type>::pending();                                 \
    }                                                                          \
    typename template_utils::future_output<decltype(future)>::type             \
        outputName = poll.get();

#define READY(value)                                    \
    return Poll<typename template_utils::future_output< \
        decltype(this)>::type>::ready(value);

#define INIT_STATE(state_, future, futureValue) \
    this->state = State::state_;                \
    this->future = futureValue;                 \
    }                                           \
    case State::state_: {
#define INIT_AWAIT(state, future, futureValue, outputName) \
    INIT_STATE(state, future, futureValue)                 \
    AWAIT(future, outputName)

#define AWAIT_PTR(future, outputName)                                          \
    Poll<typename template_utils::future_output<decltype(future)>::type> poll; \
    poll = future->poll();                                                     \
    if (poll.isPending()) {                                                    \
        return Poll<typename template_utils::future_output<                    \
            decltype(this)>::type>::pending();                                 \
    }                                                                          \
    typename template_utils::future_output<decltype(future)>::type             \
        outputName = poll.get();

#endif
