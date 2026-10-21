//
//  session_log.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  The session log (game/docs/multiplayer.md section 5): checkpoint plus
//  finalized stream, the deterministic reproduction of a game.  Every
//  session writes one beside the saves, and the diagnosis commands --
//  `replay` and `compare` (core/docs/commands.md section 7) -- read
//  them back.
//
//  The file is a transcript of the client's connection in the wire
//  protocol's own frames (game/wire.hpp): the HELLO we sent and the
//  WELCOME we got; the World we started from, as the SAVE frames a
//  joiner receives; our HASH of it; then for every step the STEP frame
//  as the relay finalized it and the HASH we reported after applying
//  it.  So the log has no format of its own, the stream's decoders read
//  it, the action bodies in it are the console's command lines, and a
//  replay is a client taking its steps from a file instead of a socket.
//

#ifndef session_log_hpp
#define session_log_hpp

#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "server.hpp"
#include "wire.hpp"

namespace wry {

    // A log read back
    struct SessionLogContents {
        wire::Hello hello{};            // the versions of the build that wrote it
        wire::Welcome welcome{};        // the client it was, the relay's quantum
        uint64_t head = 0;              // the checkpoint's time
        uint64_t head_hash = 0;         // its hash, as recorded
        std::vector<uint8_t> checkpoint;    // the save format's bytes
        struct Entry {
            Step step;                  // step head + 1 + i
            uint64_t hash;              // recorded after applying it
        };
        std::vector<Entry> entries;

        // The last step logged (the head, when none)
        uint64_t last() const { return head + entries.size(); }

        // Whether this build wrote it
        bool same_build() const { return hello.build == wire::build_id(); }
    };

    class SessionLog {
    public:

        // Logs kept per directory: the oldest beyond this are removed
        // when a new one is created
        static constexpr size_t keep = 10;

        // A log for a session whose World at `head` serializes to
        // `checkpoint`.  Nothing is written until the first append, so a
        // session that ends before a step leaves no file
        SessionLog(std::filesystem::path directory, uint64_t head, uint64_t head_hash,
                   std::vector<uint8_t> checkpoint);
        ~SessionLog();

        SessionLog(SessionLog const&) = delete;
        SessionLog& operator=(SessionLog const&) = delete;

        // A step and the hash of the World it produced, written through
        // to the file.  The first call creates the file and writes the
        // header, with the session as this client learned it from
        // WELCOME.  False once writing has failed: the log is abandoned
        bool append(Step const& step, uint64_t hash, ClientID client, uint64_t quantum_ns);

        // The file; empty until the first append
        std::filesystem::path const& path() const { return _path; }

    private:
        std::filesystem::path _directory;
        std::filesystem::path _path;
        uint64_t _head;
        uint64_t _head_hash;
        std::vector<uint8_t> _checkpoint;
        FILE* _file = nullptr;
        bool _failed = false;

        bool _open(wire::Welcome const& welcome);
        void _rotate();
        bool _write(std::string const& bytes);
    };

    // The bytes of a whole log, as the writer lays them out: the reader's
    // inverse, for tests and tools
    std::string encode_session_log(SessionLogContents const& contents);

    // A log from its bytes, or from a file.  A log whose writer stopped
    // mid-frame, or between a STEP and its HASH, ends at the last whole
    // entry; any other malformation is the error
    std::expected<SessionLogContents, std::string> decode_session_log(std::span<const uint8_t> bytes);
    std::expected<SessionLogContents, std::string> read_session_log(std::filesystem::path const& path);

    // Two logs of one game compared (multiplayer.md section 5): their
    // ranges, each checkpoint against the other's record of that step,
    // the steps they share, and the first step where the streams and
    // where the hashes differ.  One finding per line
    std::string compare_session_logs(SessionLogContents const& a, SessionLogContents const& b);

    // A hash as the Hash literal (game/command_types.hpp), for reports
    std::string hash_literal(uint64_t hash);

} // namespace wry

#endif /* session_log_hpp */
