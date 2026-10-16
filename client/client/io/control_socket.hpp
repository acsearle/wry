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
//  The main thread takes complete lines from an inbox once per frame and
//  queues replies, which a connection's writer sends; what the lines mean
//  is GuiContext's business.
//

#ifndef control_socket_hpp
#define control_socket_hpp

#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "coroutine.hpp"

namespace wry {

    class ControlSocket {
    public:

        struct Line {
            uint64_t connection;
            std::string bytes;          // unvalidated; not including '\n'
        };

        // Bind and listen at `path` (replacing a stale socket file).  Null,
        // with `error` set, on failure.  Nothing is accepted until the task
        // from serve() runs; connections queue in the backlog meanwhile.
        static std::unique_ptr<ControlSocket> open(std::string const& path,
                                                   std::string& error);

        // The accept loop, as a task for the caller to spawn where this
        // socket should live: under the wait group for the process
        // singleton, under a nursery for a scoped one.  It runs under the
        // token its spawner gives it (process shutdown reaches it that way,
        // through the delegate chain) and under this object's own source
        // (close()).  Once only.
        [[nodiscard]] Coroutine::Task serve();

        // Gives each connection up to a second to send what is queued for
        // it (the reply to `quit`), then cancels the rest and unlinks the
        // path.  Does not wait for the coroutines: that is the spawner's
        // join.  Requests stop, so call it on a pinned thread: the
        // cancelled frames unwind on the caller.  Idempotent.
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

        // Main thread: complete lines received since the last call, in
        // arrival order per connection; and connections that have closed.
        std::vector<Line> take_lines();
        std::vector<uint64_t> take_closed();

        // Main thread: whether lines or closings are waiting, and a wait
        // for them (the headless host's idle).  Spurious and timed-out
        // returns are allowed; callers re-check.
        bool has_input();
        void wait_for_input(std::chrono::milliseconds timeout);

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
        std::stop_source _stop;             // close(): this object's own
        int _listen_fd = -1;                // until serve() hands it to the loop
        bool _served = false;
    };

} // namespace wry

#endif /* control_socket_hpp */
