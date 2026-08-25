//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <sanitizer/tsan_interface.h>

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include <utility>

#include "execution.hpp"

namespace wry {

    // First pass

    // For simplicity:
    // - we abort if the kqueue fails
    // - no provision for cancellation
    // - throw system_error

    namespace detail {

        struct kevent64_awaitable : std::suspend_always {
            kevent64_s _change;
            std::coroutine_handle<> _continuation;
            int _result;
            union {
                kevent64_s _event;
                int _errno;
            };
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) /* yesexcept */;
            kevent64_s await_resume() /* yesexcept */;
            void set_value(kevent64_s event);
        };


    }


    Coroutine::Future<size_t> recv_some(int fildes, void* buf, size_t nbyte);
    Coroutine::Future<size_t> send_some(int fildes, void* buf, size_t nbyte);





    // We use a dedicated thread waiting on a platform-specific mechanism for
    // asynchronous event handling, notably IO, and run the callback run on
    // that thread, trusting it to return promptly.
    //
    // macOS    : kqueue
    // Windows  : IOCP
    // Linux    : epoll (io_uring?)
    // Fallback : select

    void global_reactor_cancel();
    bool global_reactor_kevent64_change(kevent64_s const* changelist, int nchanges);

    void global_reactor_service();




} // namespace wry

#endif /* kqueue_reactor_hpp */
