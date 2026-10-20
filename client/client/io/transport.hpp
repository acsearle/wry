//
//  transport.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  Message transport over stream sockets, on the kqueue reactor: the layer
//  the control socket (io/control_socket.hpp) and the game's connections
//  (game/docs/multiplayer.md section 8) share.
//
//  A Transport is a set of connections that share a codec, limits and one
//  inbox, and that may listen (a Unix path, a TCP port) and dial (TCP).
//  Each connection is a reader and a writer, coroutines on the worker
//  pool, under a nursery of their own; they move bytes and plain structs
//  and never touch GC objects.  The consumer -- one thread, the main
//  thread in the game -- takes events from the inbox in arrival order:
//  a connection's Opened, its Messages, then its Closed, after which
//  nothing more from it; and queues messages to send.
//
//  The transport does not trust its peers (multiplayer.md section 7):
//
//    - the codec checks every length before a body is read, and a
//      malformed stream is a violation: the connection is kicked;
//    - inbound is bounded: a connection's reader parks once `max_unconsumed`
//      of its messages sit unconsumed, so the kernel buffers fill and the
//      peer's sends block (backpressure), and resumes as the consumer
//      destroys the Messages it took;
//    - outbound is bounded: a connection whose unsent bytes would exceed
//      `max_queued_bytes` is kicked as a slow consumer;
//    - a kick stops reading at once (nothing further from the peer is
//      processed), queues one courtesy farewell carrying the reason, gives
//      the writer `kick_grace` to deliver it, then closes.
//

#ifndef transport_hpp
#define transport_hpp

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

    class Transport {
    public:

        // How messages lie on the byte stream.  Stateless and thread-safe:
        // one instance serves every connection of a Transport, from their
        // readers and writers and from the consumer's thread, and must
        // outlive it.
        struct Codec {
            virtual ~Codec() = default;

            // Move the complete messages at the front of `pending` to the
            // back of `out`, leaving any partial message in place.  Returns
            // empty, or the reason the stream is malformed (a partial
            // message over its cap, a frame too long for its tag), after
            // which nothing more is read from the connection.  The messages
            // moved before the violation are still delivered.
            virtual std::string_view split(std::string& pending,
                                           std::vector<std::string>& out) const = 0;

            // The bytes that carry `message` on the wire
            virtual std::string frame(std::string_view message) const = 0;

            // The last message a kicked connection receives, carrying
            // `reason` for the peer's log
            virtual std::string farewell(std::string_view reason) const = 0;
        };

        // Newline-delimited text.  A message is a line without its '\n' (a
        // trailing '\r' is stripped too); a partial line over `max_line` is
        // a violation.  The farewell is the reason as a line.
        struct LineCodec : Codec {
            size_t max_line;
            explicit LineCodec(size_t max = 1 << 20) : max_line(max) {}
            std::string_view split(std::string& pending,
                                   std::vector<std::string>& out) const override;
            std::string frame(std::string_view message) const override;
            std::string farewell(std::string_view reason) const override;
        };

        // Length-prefixed binary frames: `u32 length` (little-endian, of
        // what follows) then `u16 tag` then the payload; a message is the
        // tag and payload.  Every length is checked against its tag's cap
        // as soon as the six-byte prefix has arrived, before any of the
        // body: a tag whose cap is zero is unknown, and a length under two
        // (no room for the tag) or over the cap is a violation.  The
        // farewell is a frame of `kick_tag` whose payload is the reason.
        struct FrameCodec : Codec {
            size_t (*cap_for_tag)(uint16_t tag);
            uint16_t kick_tag;
            FrameCodec(size_t (*cap)(uint16_t), uint16_t kick)
            : cap_for_tag(cap), kick_tag(kick) {}
            std::string_view split(std::string& pending,
                                   std::vector<std::string>& out) const override;
            std::string frame(std::string_view message) const override;
            std::string farewell(std::string_view reason) const override;
            static uint16_t tag_of(std::string_view message);
        };

        struct Limits {
            size_t max_connections = 16;
            // Messages delivered to the inbox and not yet consumed (the
            // Message destroyed), per connection; the reader parks at this
            size_t max_unconsumed = 256;
            // Bytes queued to send and not yet sent, per connection; a
            // send that would exceed this kicks the connection
            size_t max_queued_bytes = 16 << 20;
            // How long a kicked connection's writer has to deliver the
            // farewell before the connection is closed regardless
            std::chrono::milliseconds kick_grace{1000};
        };

        // A connection's record, shared by its frames, the table and the
        // credits; defined in the .cpp
        struct Connection;

        // A connection accepted or dialed, with a label for who is on the
        // other end: for a Unix socket "<name> <pid>", the name being the
        // peer's command line's first word or two (basenames: "python3
        // wry_mcp.py", "client") when readable, else its process name,
        // read at accept time; for TCP the peer's address and port
        struct Opened {
            uint64_t connection;
            std::string peer;
        };

        // The inbound bound's token: a Message holds one of its
        // connection's credits from delivery until the Message is
        // destroyed, which is when the consumer is done with it
        struct Credit {
            std::shared_ptr<Connection> _connection;
            Credit() = default;
            explicit Credit(std::shared_ptr<Connection> c) : _connection(std::move(c)) {}
            Credit(Credit&&) = default;
            Credit& operator=(Credit&&);
            Credit(Credit const&) = delete;
            Credit& operator=(Credit const&) = delete;
            ~Credit();
        };

        struct Message {
            uint64_t connection;
            std::string bytes;          // unvalidated; the codec's message
            Credit credit;
        };

        // Why, first cause wins: "end of stream" (the peer closed),
        // "kicked: <reason>", "closed" (end() or shutdown()), or the
        // failing call and its error
        struct Closed {
            uint64_t connection;
            std::string reason;
        };

        using Event = std::variant<Opened, Message, Closed>;

        // Bind and listen at a Unix path (replacing a stale socket file;
        // the file is created owner-only) or a TCP address and port (an
        // address of "127.0.0.1", "::1", "0.0.0.0", "::"; port 0 for an
        // ephemeral one, read back with port()).  Null, with `error` set,
        // on failure.  Nothing is accepted until the task from serve()
        // runs; connections queue in the backlog meanwhile.
        static std::unique_ptr<Transport> listen_unix(std::string const& path,
                                                      Codec const& codec, Limits limits,
                                                      std::string& error);
        static std::unique_ptr<Transport> listen_tcp(std::string const& address, uint16_t port,
                                                     Codec const& codec, Limits limits,
                                                     std::string& error);

        // No listener: a transport that only dials
        static std::unique_ptr<Transport> make(Codec const& codec, Limits limits);

        uint16_t port() const { return _port; }
        std::string const& path() const { return _path; }

        // The connection loop, as a task for the owner to spawn where this
        // transport should live (the process scope for a singleton, a
        // nursery for a scoped one).  It accepts, if listening, and serves
        // every connection until stopped: by the token its spawner gives
        // it (process shutdown reaches it that way), by shutdown(), or by
        // close().  Once only.
        [[nodiscard]] coroutine::Task serve();

        // Dial a TCP peer (a numeric address; name resolution is not yet
        // offered).  The new connection's id, or -errno.  Its Opened is in
        // the inbox before this returns.  serve() must have been spawned,
        // or be about to be: the connection is served by its loop
        [[nodiscard]] coroutine::Future<int> connect_tcp(std::string address, uint16_t port);

        // Graceful: stops accepting and dialing, queues an end of stream
        // behind what each connection has pending, waits up to `grace` for
        // the connections to finish, then close().  A task: await it, or
        // spawn it, where the owner can (main: sync_wait)
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);

        // Brutal and immediate: cancels the loop and every connection, and
        // unlinks a Unix path.  Does not wait for the coroutines: that is
        // the spawner's join.  Requests stop, so call it on a pinned
        // thread: the cancelled frames unwind on the caller.  Idempotent
        void close();

        // The loop has ended, or was never started
        bool finished();

        // Asserts finished(): the owner joins its spawn before destroying
        // this.  A never-served listener closes here
        ~Transport();

        Transport(Transport const&) = delete;
        Transport& operator=(Transport const&) = delete;

        // The inbox, for one consumer: try_take for a frame's worth,
        // co_await take() to wait for input (the headless host idles on
        // it, under a deadline, through sync_wait)
        coroutine::MultiProducerSingleConsumerQueue<Event>& events();

        // Queue `message` for `connection`, framed by the codec.  Dropped
        // if the connection has gone or been kicked; kicks it if it would
        // exceed the outbound bound
        void send(uint64_t connection, std::string_view message);

        // Stop reading from `connection`, queue the farewell for `reason`,
        // and close once it is delivered or `kick_grace` has passed.  Its
        // Closed carries "kicked: <reason>".  Idempotent
        void kick(uint64_t connection, std::string_view reason);

        // Deliver what is queued for `connection`, then close it
        void end(uint64_t connection);

        // Live connections, counting those admitted but not yet served
        size_t connections();

        // Everything the consumer's thread and the coroutines both touch;
        // defined in the .cpp and held by shared_ptr from both sides
        struct Shared;

    private:
        Transport() = default;

        std::string _path;                  // a Unix listener's, to unlink
        uint16_t _port = 0;                 // a TCP listener's, as bound
        std::shared_ptr<Shared> _shared;
        std::stop_source _stop;             // close(): everything
        std::stop_source _stop_accepting;   // shutdown(): the accept and dial side only
        int _listen_fd = -1;                // until serve() hands it to the loop
        bool _served = false;
    };

} // namespace wry

#endif /* transport_hpp */
