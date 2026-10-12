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
//  Transport only.  A dedicated thread owns every descriptor and moves
//  bytes: it splits input into lines and writes queued output.  It is a
//  foreign thread in the GC sense and never runs GC code.  The main thread
//  takes complete lines from the inbox once per frame and queues replies;
//  what the lines mean is GuiContext's business.
//

#ifndef control_socket_hpp
#define control_socket_hpp

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wry {

    class ControlSocket {
    public:

        struct Line {
            uint64_t connection;
            std::string bytes;          // unvalidated; not including '\n'
        };

        // Bind and listen at `path` (replacing a stale socket file) and
        // start the thread.  Null, with `error` set, on failure.
        static std::unique_ptr<ControlSocket> open(std::string const& path,
                                                   std::string& error);

        // Gives queued replies a moment to drain (the reply to `quit`), then
        // stops the thread, closes every descriptor, unlinks the path.
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

    private:
        ControlSocket() = default;
        void _run();
        void _wake();

        struct Connection {
            int fd = -1;
            std::string in;             // partial line
            std::string out;            // queued, unwritten
        };

        std::string _path;
        int _listen_fd = -1;
        int _wake_read = -1;
        int _wake_write = -1;
        std::thread _thread;

        // Guards everything below, shared by the two threads.
        std::mutex _mutex;
        bool _stopping = false;
        uint64_t _next_connection = 1;
        std::map<uint64_t, Connection> _connections;
        std::vector<Line> _inbox;
        std::vector<uint64_t> _closed;
        std::condition_variable _input;     // notified on _inbox / _closed
    };

} // namespace wry

#endif /* control_socket_hpp */
