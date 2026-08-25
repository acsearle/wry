//
//  kqueue_reactor.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include "kqueue_reactor.hpp"

namespace wry {

    namespace detail {

        constinit int _global_kqueue = 0;

        std::coroutine_handle<> kevent64_awaitable::await_suspend(std::coroutine_handle<> continuation) {
            const int NCHANGES = 1;
            const int NEVENTS = 1;
            kevent64_s changelist[NCHANGES] = {};
            kevent64_s eventlist[NCHANGES] = {};
            changelist[0] = _change;
            changelist[0].udata = (uint64_t)this;
            _result = ::kevent64(_global_kqueue,
                                 (kevent64_s const*)changelist, NCHANGES,
                                 eventlist, NEVENTS,
                                 KEVENT_FLAG_IMMEDIATE | KEVENT_FLAG_ERROR_EVENTS,
                                 nullptr);
            switch (_result) {
                case 0:
                    // Change succeeded
                    return std::noop_coroutine();
                case 1:
                    // Change failed
                    assert(eventlist[0].flags & EV_ERROR);
                    set_value(eventlist[0]);
                    return _continuation;
                case -1:
                    _errno = errno;
                    return _continuation;
                default:
                    abort();
            }
        }

        kevent64_s kevent64_awaitable::await_resume() {
            switch (_result) {
                case 1:
                    return _event;
                case -1:
                    throw std::system_error(_errno, std::generic_category());
                default:
                    abort();
            }
        }

        void kevent64_awaitable::set_value(kevent64_s event) {
            // Change failed
            // Check routing
            assert(event.udata == (uint64_t)this);
            _event = event;
            // Restore udata
            _event.udata = _change.udata;
        }

    }


    Coroutine::Future<size_t> recv_some(int fildes, void* buf, size_t nbyte) {
        assert(fildes >= 0);
        (void) co_await detail::kevent64_awaitable{{}, {
            .ident = (uint64_t)fildes,
            .filter = EVFILT_READ,
            .flags = EV_ADD | EV_ONESHOT,
        }};
        ssize_t result = ::recv(fildes, buf, nbyte, 0);
        if (result == -1)
            throw std::system_error(errno, std::generic_category());
        co_return result;
    }

    Coroutine::Future<size_t> send_some(int fildes, void* buf, size_t nbyte) {
        assert(fildes >= 0);
        (void) co_await detail::kevent64_awaitable{{}, {
            .ident = (uint64_t)fildes,
            .filter = EVFILT_WRITE,
            .flags = EV_ADD | EV_ONESHOT,
        }};
        ssize_t result = ::send(fildes, buf, nbyte, 0);
        if (result == -1)
            throw std::system_error(errno, std::generic_category());
        co_return result;
    }



    struct kqueue_reactor {

        void run() {
            int queue = detail::_global_kqueue;
            for (;;) {
                const int NEVENT = 16;
                kevent64_s eventlist[NEVENT];
                int result = kevent64(queue, nullptr, 0, eventlist, NEVENT, 0, nullptr);
                if (result == -1) {
                    perror("kqueue_reactor");
                    abort();
                }
                if ((result < 1) || !(result < NEVENT)) {
                    abort();
                }
                for (int i = 0; i != NEVENT; ++i) {
                    auto a = (detail::kevent64_awaitable*)eventlist[i].udata;
                    a->set_value(eventlist[i]);
                    global_work_queue_schedule(a->_continuation);
                }
            }

        }

    };


    void foo() {

        int socket_vector[2] = {};
        socketpair(AF_UNIX, SOCK_STREAM, 0, socket_vector);

    }



#if 0 // Obsolete version
    struct kqueue_reactor {
        
        int _kqueue = kqueue();

        void process(kevent64_s event) {
            // ThreadSanitizer doesn't understand kevent64
            __tsan_acquire((void*)event.udata);
            (*(void (**)(kevent64_s))event.udata)(event);
        }
        
        void run() {
            
            kevent64_s changelist[16];
            int nchanges = 0;
            kevent64_s eventlist[16];
            int nevents = 16;
            unsigned flags = 0;
            timespec timeout = { .tv_sec = 1, .tv_nsec = 0 };
            
            for (;;) {
                const int result = kevent64(_kq, changelist, nchanges, eventlist, nevents, flags, &timeout);
                
                // result > 0: number of events in event list
                // result = 0: timeout
                // result =-1: error (errno)
                
                if (result < 0) {
                    const size_t BUFSZ = 32;
                    char buffer[BUFSZ] = {};
                    snprintf(buffer, BUFSZ, "kevent64 -> %d", result);
                    if (result == -1) {
                        perror(buffer);
                    } else {
                        fprintf(stderr, "%s\n", buffer);
                    }
                }
                
                if (result > 0) {
                    for (int i = 0; i != result; ++i) {
                        process(eventlist[i]);
                    }
                }
            }
            
        }
        
        bool change(kevent64_s const* changelist, int nchanges) {
            const int result = kevent64(_kq,
                                        changelist, nchanges,
                                        nullptr, 0,
                                        KEVENT_FLAG_IMMEDIATE, nullptr);
            if (result != 0) {
                const size_t BUFSZ = 32;
                char buffer[BUFSZ] = {};
                snprintf(buffer, BUFSZ, "kevent64 -> %d", result);
                if (result == -1) {
                    perror(buffer);
                } else {
                    fprintf(stderr, "%s\n", buffer);
                }
            }
            return result == 0;
        }
        
    };
    
    kqueue_reactor global_reactor;
    
    
    bool global_reactor_kevent64_change(kevent64_s const* changelist, int nchanges) {
        return global_reactor.change(changelist, nchanges);
    }


#endif

} // namespace wry
