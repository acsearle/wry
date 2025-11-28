//
//  coroutine.cpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#include "coroutine.hpp"

#include "execution.hpp"
#include "test.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<> handle) {
        global_work_queue_schedule(handle.address());
    }

}

namespace wry::coroutine {
    
    define_test("coroutine") {
        []() -> co_future<int> {
            co_await []() -> co_future<double> {
                co_return 8.0;
            }();
            co_return 7;
        }();
        
        Flow flow;
        
        flow.fork([]() -> co_future<int> { co_return 7; }());
        
        co_return;
    };
    
    define_test("co_sender") {
        
        auto a = []() -> co_sender<int> {
            printf("co_sender<int>\n");
            auto b = []() -> co_sender<int> {
                printf("co_sender<int2>\n");
                co_return 7;
            }();
            co_return co_await b;
        }();
        
        auto b = []() -> co_sender<> {
            printf("co_sender<>\n");
            co_return;
        }();
        
        a.connect(execution::_trivial_receiver{}).start();
        b.connect(execution::_trivial_receiver{}).start();

        
        co_return;
    };
    
}
