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
//  Transport only, on the kqueue reactor: an accept loop and, per
//  connection, a reader and a writer, all coroutines on the worker pool
//  (core/kqueue_reactor.hpp).  They move bytes and never touch GC objects.
//  The main thread consumes an event queue -- complete lines, and
//  connections closing, in arrival order -- and queues replies, which a
//  connection's writer sends; what the lines mean is GuiContext's business.
//

#ifndef control_socket_hpp
#define control_socket_hpp

#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coroutine.hpp"
#include "mpsc_queue.hpp"

namespace wry {

    class ControlSocket {
    public:

        // A connection accepted, with a label for who is on the other end:
        // "<name> <pid>", the name being the peer's command line's first
        // word or two (basenames: "python3 wry_mcp.py", "client") when
        // readable, else its process name.  Read at accept time; a peer that
        // forks after connecting keeps the original's label
        struct Opened {
            uint64_t connection;
            std::string peer;
        };

        struct Line {
            uint64_t connection;
            std::string bytes;          // unvalidated; not including '\n'
        };

        struct Closed {
            uint64_t connection;
        };

        // What the main thread consumes: a connection's Opened, its lines
        // in arrival order, then its Closed, after which nothing more from
        // it.  The ordinal is the identity; the label is for people
        using Event = std::variant<Opened, Line, Closed>;

        // Bind and listen at `path` (replacing a stale socket file).  Null,
        // with `error` set, on failure.  Nothing is accepted until the task
        // from serve() runs; connections queue in the backlog meanwhile.
        static std::unique_ptr<ControlSocket> open(std::string const& path,
                                                   std::string& error);

        // The accept loop, as a task for the caller to spawn where this
        // socket should live: under the wait group for the process
        // singleton, under a nursery for a scoped one.  It runs under the
        // token its spawner gives it (process shutdown reaches it that way,
        // through the delegate chain) and under this object's own sources
        // (shutdown(), close()).  Once only.
        [[nodiscard]] coroutine::Task serve();

        // Graceful: stops accepting, queues an end of stream behind what
        // each connection has pending (the reply to `quit`), waits up to
        // `grace` for the connections to finish, then close().  A task:
        // await it, or spawn it, where the owner can (main: sync_wait)
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);

        // Brutal and immediate: cancels the accept loop and every
        // connection, and unlinks the path.  Does not wait for the
        // coroutines: that is the spawner's join.  Requests stop, so call
        // it on a pinned thread: the cancelled frames unwind on the caller.
        // Idempotent
        void close();

        // The accept loop has ended, or was never started
        bool finished();

        // Asserts finished(): the owner joins its spawn (a nursery's join,
        // the wait group's drain) before destroying this.  A never-served
        // socket closes its listener here
        ~ControlSocket();

        ControlSocket(ControlSocket const&) = delete;
        ControlSocket& operator=(ControlSocket const&) = delete;

        std::string const& path() const { return _path; }

        // The event queue, for one consumer: try_take for a frame's worth,
        // co_await take() to wait for input (the headless host idles on it,
        // under a deadline, through sync_wait)
        coroutine::MultiProducerSingleConsumerQueue<Event>& events();

        // Main thread: queue `line` (a '\n' is appended) for `connection`.
        // Silently dropped if the connection has gone.
        void send(uint64_t connection, std::string_view line);

        // A line longer than this closes its connection.
        static constexpr size_t max_line = 1 << 20;
        static constexpr size_t max_connections = 16;

        // State shared by the main thread and the coroutines; defined in
        // the .cpp.  The coroutines hold it by shared_ptr, so it outlives
        // whichever side finishes last.
        struct Shared;

    private:
        ControlSocket() = default;

        std::string _path;
        std::shared_ptr<Shared> _shared;
        std::stop_source _stop;             // close(): everything
        std::stop_source _stop_accepting;   // shutdown(): the accept loop only
        int _listen_fd = -1;                // until serve() hands it to the loop
        bool _served = false;
    };

} // namespace wry

#endif /* control_socket_hpp */
