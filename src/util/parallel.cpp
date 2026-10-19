#include "parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fla {

namespace {

/// Set on a thread while it works on a range, so a range that asks for
/// more parallel work does it itself instead of waiting on the pool.
thread_local bool tl_inside = false;

class Pool
{
public:
    Pool()
    {
        int threads = static_cast<int>(std::thread::hardware_concurrency());
        if (const char* asked = std::getenv("PHOENIX_THREADS"))
            threads = std::atoi(asked);
#ifdef __EMSCRIPTEN__
        // In a browser a thread is a web worker, of which the page makes a
        // few when it loads (QT_WASM_PTHREAD_POOL_SIZE in CMakeLists.txt);
        // more would wait on the page's own thread to be made. Some are
        // left for autosave and audio.
        threads = std::min(threads, 6);
#endif
        _threads = std::clamp(threads, 1, 64);
        for (int i = 1; i < _threads; ++i)
            _workers.emplace_back([this]() { work(); });
    }
    ~Pool()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopping = true;
        }
        _wake.notify_all();
        for (std::thread& worker : _workers)
            worker.join();
    }

    int threads() const { return _threads; }

    void run(size_t count, size_t grain, const std::function<void(size_t, size_t)>& body)
    {
        auto job = std::make_shared<Job>();
        job->body = &body;
        job->count = count;
        const size_t chunks = std::min<size_t>(static_cast<size_t>(_threads) * 4, (count + grain - 1) / grain);
        job->chunkSize = (count + chunks - 1) / chunks;
        job->pending = (count + job->chunkSize - 1) / job->chunkSize;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _job = job;
            ++_generation;
        }
        _wake.notify_all();
        take(*job);
        std::exception_ptr failed;
        {
            std::unique_lock<std::mutex> lock(job->mutex);
            job->finished.wait(lock, [&job]() { return job->pending == 0; });
            failed = job->failed;
        }
        {
            std::lock_guard<std::mutex> clear(_mutex);
            if (_job == job)
                _job.reset();
        }
        // What went wrong in a range, thrown here once every range is done
        // with the caller's data.
        if (failed)
            std::rethrow_exception(failed);
    }

private:
    /// One call's work: its chunks are taken in turn by whoever is free.
    struct Job
    {
        const std::function<void(size_t, size_t)>* body = nullptr;
        size_t count = 0;
        size_t chunkSize = 1;
        std::atomic<size_t> next{0};
        std::mutex mutex;
        std::condition_variable finished;
        size_t pending = 0;
        /// The first exception a range threw.
        std::exception_ptr failed;
    };

    /// Works through \a job's chunks until none are left.
    static void take(Job& job)
    {
        struct Inside
        {
            bool was = tl_inside;
            Inside() { tl_inside = true; }
            ~Inside() { tl_inside = was; }
        } inside;
        for (;;)
        {
            const size_t from = job.next.fetch_add(job.chunkSize);
            if (from >= job.count)
                break;
            // A range that throws still counts as done, so the caller is
            // never left waiting; the caller throws it again.
            std::exception_ptr failed;
            try
            {
                (*job.body)(from, std::min(job.count, from + job.chunkSize));
            }
            catch (...)
            {
                failed = std::current_exception();
            }
            std::lock_guard<std::mutex> lock(job.mutex);
            if (failed && !job.failed)
                job.failed = failed;
            if (--job.pending == 0)
                job.finished.notify_all();
        }
    }

    void work()
    {
        unsigned seen = 0;
        for (;;)
        {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _wake.wait(lock, [this, seen]() { return _stopping || (_generation != seen && _job); });
                if (_stopping)
                    return;
                seen = _generation;
                job = _job;
            }
            take(*job);
        }
    }

    int _threads = 1;
    std::vector<std::thread> _workers;
    std::mutex _mutex;
    std::condition_variable _wake;
    std::shared_ptr<Job> _job;
    unsigned _generation = 0;
    bool _stopping = false;
};

Pool& pool()
{
    static Pool instance;
    return instance;
}

} // namespace

void parallelFor(size_t count, size_t grain, const std::function<void(size_t from, size_t to)>& body)
{
    if (count == 0)
        return;
    grain = std::max<size_t>(1, grain);
    if (tl_inside || count <= grain || pool().threads() <= 1)
    {
        body(0, count);
        return;
    }
    pool().run(count, grain, body);
}

namespace {

/// About how many pixels a thread is given at least.
constexpr size_t kPixelsAThread = 8192;

} // namespace

void parallelRows(int height, int width, const std::function<void(int y)>& body)
{
    if (height <= 0)
        return;
    const size_t rows = std::max<size_t>(1, kPixelsAThread / static_cast<size_t>(std::max(1, width)));
    parallelFor(static_cast<size_t>(height), rows, [&body](size_t from, size_t to) {
        for (size_t y = from; y < to; ++y)
            body(static_cast<int>(y));
    });
}

void parallelPixels(size_t count, const std::function<void(size_t from, size_t to)>& body)
{
    parallelFor(count, kPixelsAThread, body);
}

int parallelThreads()
{
    return pool().threads();
}

} // namespace fla
