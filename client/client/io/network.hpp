//
//  network.hpp
//  client
//
//  Created by Antony Searle on 20/8/2026.
//

#ifndef network_hpp
#define network_hpp

#include <span>

#include "coroutine.hpp"
#include "kqueue_reactor.hpp"

namespace wry::network {

    // Single player:
    // - client and server on one machine
    // - over whatever local socket
    // Multiplayer:
    // - client only on all machines, +server on one
    // - promote existing client to server when server departs?

    // rough sketch



    struct Socket {
        int file_descriptor;
    };

    Coroutine::Future<Socket> accept();
    Coroutine::Task connect();

    // See kqueue_reactor.hpp for the semantics (kernel-convention result,
    // cancellation unwinding, single waiter per socket).  Deadlines compose
    // from outside: co_await with_deadline(t, recv_some(socket, buffer)).
    [[nodiscard]] inline Coroutine::Future<ssize_t>
    recv_some(Socket const& socket, std::span<std::byte> buffer) {
        return wry::recv_some(socket.file_descriptor, buffer.data(), buffer.size(), 0);
    }

    // TODO: send_some symmetrically (EVFILT_WRITE) when a caller exists

    Coroutine::Task client() {
        co_return;
    }

    Coroutine::Task server() {
        Coroutine::Nursery nursery;        
        co_await nursery.join();
        co_return;
    }





} // namespace wry::network



#endif /* network_hpp */
