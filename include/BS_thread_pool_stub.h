// BS_thread_pool_stub.h — Synchronous drop-in replacement for BS::thread_pool
// on Emscripten (WebAssembly without pthreads).
//
// All submitted work executes immediately on the calling thread.
// The API surface matches what GW actually uses from BS::thread_pool v3.5.0.

#pragma once

#include <cstddef>
#include <future>
#include <type_traits>
#include <utility>

namespace BS {

// Minimal multi_future stub: work has already completed synchronously.
template <typename T = void>
class [[nodiscard]] multi_future {
public:
    multi_future(std::size_t = 0) {}
    void wait() const {}
    void get()  const {}
};

class thread_pool {
public:
    thread_pool() noexcept = default;
    explicit thread_pool(std::size_t /*num_threads*/) noexcept {}

    // Run loop(first_index, last_index) synchronously as a single block.
    template <typename F, typename T1, typename T2>
    [[nodiscard]] multi_future<void>
    parallelize_loop(T1 first_index, T2 last_index, F&& loop,
                     std::size_t /*num_blocks*/ = 0) {
        if (first_index < last_index)
            std::forward<F>(loop)(static_cast<T1>(first_index),
                                  static_cast<T1>(last_index));
        return {};
    }

    // Run task(args...) synchronously and return a ready std::future<R>.
    template <typename F, typename... A>
    [[nodiscard]] auto submit(F&& task, A&&... args) {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<A>...>;
        std::promise<R> p;
        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(task), std::forward<A>(args)...);
            p.set_value();
        } else {
            p.set_value(
                std::invoke(std::forward<F>(task), std::forward<A>(args)...));
        }
        return p.get_future();
    }

    void        reset(std::size_t /*num_threads*/ = 1) {}
    void        wait_for_tasks()   const {}
    std::size_t get_thread_count() const { return 1; }
};

} // namespace BS
