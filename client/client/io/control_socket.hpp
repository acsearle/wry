//
//  control_socket.hpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//
//  The control socket (core/docs/commands.md section 9): a Unix domain
//  socket, created mode 0600, carrying newline-delimited requests in and
//  newline-delimited replies out, for scripts, tests and the MCP adapter.
//
//  A Transport (io/transport.hpp) with the line codec: the accept loop and
//  the per-connection readers and writers are its, as are the bounds and
//  the kick.  The main thread consumes the inbox -- a connection's
//  opening, its complete lines, and its closing, in arrival order -- and
//  queues replies; what the lines mean is GuiContext's business.
//

#ifndef control_socket_hpp
#define control_socket_hpp

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "coroutine.hpp"
#include "mpsc_queue.hpp"
#include "transport.hpp"

namespace wry {

    class ControlSocket {
    public:

        // A connection's Opened (with its peer label), its lines in arrival
        // order, then its Closed, after which nothing more from it.  The
        // ordinal is the identity; the label is for people
        using Opened = Transport::Opened;
        using Line = Transport::Message;
        using Closed = Transport::Closed;
        using Event = Transport::Event;

        // A line longer than this kicks its connection.
        static constexpr size_t max_line = 1 << 20;

        // Bind and listen at `path` (replacing a stale socket file).  Null,
        // with `error` set, on failure.  Nothing is accepted until the task
        // from serve() runs; connections queue in the backlog meanwhile.
        static std::unique_ptr<ControlSocket> open(std::string const& path,
                                                   std::string& error);

        // The connection loop, as a task for the caller to spawn where this
        // socket should live: the process scope for the singleton, a
        // nursery for a scoped one.  Once only.
        [[nodiscard]] coroutine::Task serve();

        // Graceful: stops accepting, queues an end of stream behind what
        // each connection has pending (the reply to `quit`), waits up to
        // `grace` for the connections to finish, then close().
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);

        // Brutal and immediate; see Transport::close
        void close();

        // The loop has ended, or was never started
        bool finished();

        // Asserts finished(): the owner joins its spawn before destroying
        // this
        ~ControlSocket() = default;

        ControlSocket(ControlSocket const&) = delete;
        ControlSocket& operator=(ControlSocket const&) = delete;

        std::string const& path() const { return _transport->path(); }

        // The inbox, for one consumer: try_take for a frame's worth,
        // co_await take() to wait for input (the headless host idles on it,
        // under a deadline, through sync_wait)
        coroutine::MultiProducerSingleConsumerQueue<Event>& events();

        // Main thread: queue `line` (a '\n' is appended) for `connection`.
        // Dropped if the connection has gone; a connection that will not
        // read its replies is kicked once they exceed the outbound bound.
        void send(uint64_t connection, std::string_view line);

        // Main thread: stop reading from `connection`, send it one error
        // line carrying `reason`, and close it.
        void kick(uint64_t connection, std::string_view reason);

    private:
        ControlSocket() = default;

        std::unique_ptr<Transport> _transport;
    };

} // namespace wry

#endif /* control_socket_hpp */
