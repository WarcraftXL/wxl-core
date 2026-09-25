// One-shot startup completion shared by the loader and deferred core installer.
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
namespace wxl::runtime {
class StartupCompletion {
    std::mutex mutex_;
    std::condition_variable condition_;
    bool complete_=false;
public:
    void Complete() {
        { const std::lock_guard lock(mutex_); complete_=true; }
        condition_.notify_all();
    }
    bool Wait(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock,timeout,[this]{return complete_;});
    }
};
}
