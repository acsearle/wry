//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <sanitizer/tsan_interface.h>

// kqueue
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include <utility>

#include "execution.hpp"

namespace wry {
    
    void global_reactor_cancel();
    void global_reactor_service();
    
    // We use a dedicated thread waiting on a platform-specific mechanism for
    // asynchronous event handling, notably IO, and run the callback run on
    // that thread, trusting it to return promptly.
    //
    // macOS    : kqueue
    // Windows  : IOCP
    // Linux    : epoll (io_uring?)
    // Fallback : select
    
    // kqueue
    
    bool global_reactor_kevent64_change(kevent64_s*, int);
        
    template<typename Receiver>
    struct _kevent64_operation {
        void (*_callback)(kevent64_s);
        kevent64_s event;
        Receiver _receiver;
        
        static void _static_callback(kevent64_s event) {
            auto* that = (_kevent64_operation*)event.udata;
            std::move(that->_receiver).set_value(std::move(event));
        }
        
        void start() {
            _callback = &_static_callback;
            // ThreadSanitizer doesn't understand kevent64
            __tsan_release(&_callback);
            event.flags = EV_ADD | EV_ONESHOT | EV_UDATA_SPECIFIC;
            event.udata = (uint64_t)this;
            (void) global_reactor_kevent64_change(&event, 1);
        }
        
    };
    
    struct kevent64_sender {
        kevent64_s event;
        template<typename Receiver>
        auto connect(Receiver receiver) {
            return _kevent64_operation<Receiver>{{}, std::move(event), std::move(receiver)};
        }
    };
    
    
    // Sender factories
    
    auto async_read(int fd) {
        return execution::then(kevent64_sender{
            kevent64_s{
                .ident = (uint64_t)fd,
                .filter = EVFILT_READ,
            }}, [](kevent64_s event) {
                return event.data;
            });
    }
    
    auto async_write(int fd) {
        return execution::then(kevent64_sender{
            kevent64_s{
                .ident = (uint64_t)fd,
                .filter = EVFILT_WRITE,
            }}, [](kevent64_s event) {
                return event.data;
            });
    }
    
    
    

} // namespace wry

#endif /* kqueue_reactor_hpp */
