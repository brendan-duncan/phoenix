#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace phx {

/// Runs \a work(i) for every i below \a count on a few threads, each taking
/// the next index as it finishes one, and returns when all are done. Small
/// jobs run on the calling thread.
template <typename Work>
void parallelFor(size_t count, Work work)
{
    const size_t threads = std::min<size_t>(count, std::max(1u, std::min(std::thread::hardware_concurrency(), 8u)));
    if (threads <= 1)
    {
        for (size_t i = 0; i < count; ++i)
            work(i);
        return;
    }
    std::atomic<size_t> next{0};
    const auto run = [&]() {
        for (size_t i = next++; i < count; i = next++)
            work(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (size_t t = 1; t < threads; ++t)
        pool.emplace_back(run);
    run();
    for (std::thread& thread : pool)
        thread.join();
}

} // namespace phx
