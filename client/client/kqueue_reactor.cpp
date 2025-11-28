//
//  kqueue_reactor.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include "kqueue_reactor.hpp"

namespace wry {
    
    struct kqueue_reactor {
        
        int _kq = kqueue();
        
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

    
} // namespace wry
