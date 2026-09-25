#include "runtime/StartupCompletion.hpp"
#include <cassert>
#include <future>
#include <thread>
using namespace std::chrono_literals;
int main(){
    wxl::runtime::StartupCompletion gate;
    assert(!gate.Wait(1ms)); // never authorize enabling a partial chain
    std::promise<void> entered;
    auto waiter=std::async(std::launch::async,[&]{entered.set_value();return gate.Wait(2s);});
    entered.get_future().wait();
    assert(waiter.wait_for(20ms)==std::future_status::timeout);
    gate.Complete();assert(waiter.get());
    assert(gate.Wait(0ms)); // completion before a waiter must not lose its wakeup
    gate.Complete();assert(gate.Wait(0ms));
    wxl::runtime::StartupCompletion other;
    auto a=std::async(std::launch::async,[&]{return other.Wait(2s);});
    auto b=std::async(std::launch::async,[&]{return other.Wait(2s);});
    other.Complete();assert(a.get() && b.get());
}
