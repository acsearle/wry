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

namespace wry::network {

    // Single player:
    // - client and server on one machine
    // - over whatever local socket
    // Multiplayer:
    // - client only on all machines, +server on one
    // - promote existing client to server when server departs?

    // TODO:
    // Coroutine::Future exception support
    // Coroutine SPSC queue

    // rough sketch



    struct Socket {
        int file_descriptor;
    };

    Coroutine::Future<Socket> accept();
    Coroutine::Task connect();

    Coroutine::Future<size_t> recv_some(Socket const&, std::span<std::byte const>);
    Coroutine::Future<size_t> send_some(Socket const&, std::span<std::byte>);

    Coroutine::Task client() {
        co_return;
    }

    Coroutine::Task server() {
        Coroutine::Nursery nursery;        
        nursery.join();
        co_return;
    }





} // namespace wry::network



#endif /* network_hpp */
