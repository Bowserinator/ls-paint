#include "utils/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lsp {

namespace {

struct Job {
    Job(std::size_t count, const std::function<void(std::size_t)>& work) : count(count), work(work) {}

    // Takes items until none are left. Returns once this thread has nothing
    // more to claim, which is not the same as every item being finished.
    void drain() {
        for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
            work(i);
            if (finished.fetch_add(1) + 1 == count) {
                const std::lock_guard lock(mutex);
                done.notify_all();
            }
        }
    }

    void wait() {
        std::unique_lock lock(mutex);
        done.wait(lock, [this] { return finished.load() == count; });
    }

    std::size_t count;
    const std::function<void(std::size_t)>& work;
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> finished{0};
    std::mutex mutex;
    std::condition_variable done;
};

// Threads that live as long as the program, so a parallel loop costs a wake-up
// instead of a thread start.
class Pool {
public:
    Pool() {
        const std::size_t cores = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        for (std::size_t i = 1; i < cores; ++i) m_threads.emplace_back([this] { serve(); });
    }

    ~Pool() {
        {
            const std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        for (std::thread& thread : m_threads) thread.join();
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] std::size_t helpers() const noexcept { return m_threads.size(); }

    void run(std::size_t count, const std::function<void(std::size_t)>& work) {
        const auto job = std::make_shared<Job>(count, work);
        {
            const std::lock_guard lock(m_mutex);
            m_jobs.push_back(job);
        }
        m_wake.notify_all();
        job->drain();
        job->wait();
        const std::lock_guard lock(m_mutex);
        std::erase(m_jobs, job);
    }

private:
    void serve() {
        std::unique_lock lock(m_mutex);
        for (;;) {
            m_wake.wait(lock, [this] { return m_stopping || has_open_job(); });
            if (m_stopping) return;
            std::shared_ptr<Job> job;
            for (const std::shared_ptr<Job>& candidate : m_jobs) {
                if (candidate->next.load() < candidate->count) {
                    job = candidate;
                    break;
                }
            }
            if (!job) continue;
            lock.unlock();
            job->drain();
            lock.lock();
        }
    }

    [[nodiscard]] bool has_open_job() const {
        return std::ranges::any_of(m_jobs, [](const std::shared_ptr<Job>& job) {
            return job->next.load() < job->count;
        });
    }

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::shared_ptr<Job>> m_jobs;
    std::vector<std::thread> m_threads;
    bool m_stopping = false;
};

Pool& pool() {
    static Pool instance;
    return instance;
}

}  // namespace

void parallel_for(std::size_t count, const std::function<void(std::size_t)>& work) {
    if (count == 0) return;
    if (count == 1) {
        work(0);
        return;
    }
    Pool& workers = pool();
    if (workers.helpers() == 0) {
        for (std::size_t i = 0; i < count; ++i) work(i);
        return;
    }
    workers.run(count, work);
}

}  // namespace lsp
