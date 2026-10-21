//
//  session_log.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include "session_log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <random>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "command_types.hpp"
#include "host.hpp"
#include "save.hpp"
#include "test.hpp"
#include "world.hpp"

namespace wry {

    std::string hash_literal(uint64_t hash) {
        String text = command::to_string(Hash{hash});
        return std::string(text.data(), text.chars.size());
    }

    namespace {

        // The frames of a log, from the front, each checked against its
        // tag's cap as the transport would.  A partial frame at the end
        // is where the writer stopped, not an error
        std::expected<std::vector<std::string_view>, std::string> split_frames(std::span<const uint8_t> bytes) {
            std::vector<std::string_view> frames;
            size_t at = 0;
            while (bytes.size() - at >= 6) {
                uint32_t length = 0;
                std::memcpy(&length, bytes.data() + at, sizeof length);
                uint16_t tag = 0;
                std::memcpy(&tag, bytes.data() + at + 4, sizeof tag);
                if (length < 2)
                    return std::unexpected("frame too short at byte " + std::to_string(at));
                const size_t cap = wire::cap_for_tag(tag);
                if (cap == 0)
                    return std::unexpected("unknown tag " + std::to_string(tag) + " at byte " + std::to_string(at));
                if (length > cap)
                    return std::unexpected("frame too long at byte " + std::to_string(at));
                if (bytes.size() - at < 4 + (size_t)length)
                    break;
                frames.emplace_back((const char*)bytes.data() + at + 4, (size_t)length);
                at += 4 + (size_t)length;
            }
            return frames;
        }

        // The header: the handshake, the checkpoint as a donation would
        // carry it (transfer 0), and its hash
        std::string header_frames(wire::Hello const& hello, wire::Welcome const& welcome,
                                  uint64_t head, uint64_t head_hash, std::span<const uint8_t> checkpoint) {
            Transport::FrameCodec const& codec = wire::codec();
            std::string out;
            out += codec.frame(wire::encode_hello(hello));
            out += codec.frame(wire::encode_welcome(welcome));
            out += codec.frame(wire::encode_save_begin(0, head, checkpoint.size()));
            uint32_t index = 0;
            for (size_t at = 0; at < checkpoint.size(); at += wire::MAX_SAVE_CHUNK) {
                std::string_view chunk((const char*)checkpoint.data() + at,
                                       std::min(wire::MAX_SAVE_CHUNK, checkpoint.size() - at));
                out += codec.frame(wire::encode_save_data(0, index++, chunk));
            }
            out += codec.frame(wire::encode_save_end(0));
            out += codec.frame(wire::encode_hash(head, head_hash));
            return out;
        }

        std::string entry_frames(Step const& step, uint64_t hash) {
            Transport::FrameCodec const& codec = wire::codec();
            return codec.frame(step_to_wire(step)) + codec.frame(wire::encode_hash(step.number, hash));
        }

        std::expected<std::vector<uint8_t>, std::string> read_file(std::filesystem::path const& path) {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in)
                return std::unexpected("cannot read " + path.string());
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes((size_t)size);
            if (size && !in.read((char*)bytes.data(), size))
                return std::unexpected("cannot read " + path.string());
            return bytes;
        }

    } // namespace

    // ---- Writing --------------------------------------------------------

    SessionLog::SessionLog(std::filesystem::path directory, uint64_t head, uint64_t head_hash,
                           std::vector<uint8_t> checkpoint)
    : _directory(std::move(directory))
    , _head(head)
    , _head_hash(head_hash)
    , _checkpoint(std::move(checkpoint)) {
    }

    SessionLog::~SessionLog() {
        if (_file)
            fclose(_file);
    }

    bool SessionLog::_write(std::string const& bytes) {
        if (!_file)
            return false;
        if ((fwrite(bytes.data(), 1, bytes.size(), _file) != bytes.size()) || (fflush(_file) != 0)) {
            _failed = true;
            fclose(_file);
            _file = nullptr;
            return false;
        }
        return true;
    }

    // The file is named for the moment; a second log in the same second
    // takes a suffix.  Created exclusively, so two processes sharing a
    // directory (the net smoke test) never share a file
    bool SessionLog::_open(wire::Welcome const& welcome) {
        std::error_code ec;
        std::filesystem::create_directories(_directory, ec);
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
        int fd = -1;
        for (int n = 1; (n != 1000) && (fd < 0); ++n) {
            std::string name = std::string("session-") + stamp;
            if (n != 1)
                name += "-" + std::to_string(n);
            name += ".wrylog";
            std::filesystem::path candidate = _directory / name;
            fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
            if (fd >= 0)
                _path = candidate;
            else if (errno != EEXIST)
                return false;
        }
        if (fd < 0)
            return false;
        _file = fdopen(fd, "wb");
        if (!_file) {
            ::close(fd);
            return false;
        }
        _rotate();
        const bool ok = _write(header_frames({wire::PROTOCOL_VERSION, wire::build_id(), 0}, welcome,
                                             _head, _head_hash, _checkpoint));
        _checkpoint.clear();
        _checkpoint.shrink_to_fit();
        return ok;
    }

    // Keep the newest `keep` logs in the directory, ours among them
    void SessionLog::_rotate() {
        std::error_code ec;
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> logs;
        for (auto const& entry : std::filesystem::directory_iterator(_directory, ec)) {
            std::filesystem::path const& p = entry.path();
            if ((p.extension() != ".wrylog") || (p == _path))
                continue;
            if (!p.filename().string().starts_with("session-"))
                continue;
            logs.emplace_back(entry.last_write_time(ec), p);
        }
        std::sort(logs.begin(), logs.end(), [](auto const& a, auto const& b) {
            return (a.first != b.first) ? (a.first > b.first) : (a.second > b.second);
        });
        for (size_t i = keep - 1; i < logs.size(); ++i)
            std::filesystem::remove(logs[i].second, ec);
    }

    bool SessionLog::append(Step const& step, uint64_t hash, ClientID client, uint64_t quantum_ns) {
        if (_failed)
            return false;
        if (!_file) {
            wire::Welcome welcome{client, quantum_ns, _head, _head + 1};
            if (!_open(welcome)) {
                _failed = true;
                if (_file) {
                    fclose(_file);
                    _file = nullptr;
                }
                return false;
            }
        }
        return _write(entry_frames(step, hash));
    }

    // ---- Reading --------------------------------------------------------

    std::string encode_session_log(SessionLogContents const& c) {
        std::string out = header_frames(c.hello, c.welcome, c.head, c.head_hash, c.checkpoint);
        for (SessionLogContents::Entry const& e : c.entries)
            out += entry_frames(e.step, e.hash);
        return out;
    }

    std::expected<SessionLogContents, std::string> decode_session_log(std::span<const uint8_t> bytes) {
        auto split = split_frames(bytes);
        if (!split)
            return std::unexpected(split.error());
        std::vector<std::string_view> const& frames = *split;
        size_t i = 0;
        auto take = [&](uint16_t tag, const char* what) -> std::expected<std::string_view, std::string> {
            if (i == frames.size())
                return std::unexpected(std::string("the log ends before the ") + what);
            if (wire::tag_of(frames[i]) != tag)
                return std::unexpected(std::string("expected the ") + what + " at frame " + std::to_string(i)
                                       + ", found tag " + std::to_string(wire::tag_of(frames[i])));
            return frames[i++];
        };
        SessionLogContents c;

        auto hello_frame = take(wire::HELLO, "HELLO");
        if (!hello_frame)
            return std::unexpected(hello_frame.error());
        std::optional<wire::Hello> hello = wire::decode_hello(*hello_frame);
        if (!hello)
            return std::unexpected(std::string("malformed HELLO"));
        if (hello->protocol != wire::PROTOCOL_VERSION)
            return std::unexpected("protocol version " + std::to_string(hello->protocol)
                                   + "; this build reads " + std::to_string(wire::PROTOCOL_VERSION));
        c.hello = *hello;

        auto welcome_frame = take(wire::WELCOME, "WELCOME");
        if (!welcome_frame)
            return std::unexpected(welcome_frame.error());
        std::optional<wire::Welcome> welcome = wire::decode_welcome(*welcome_frame);
        if (!welcome)
            return std::unexpected(std::string("malformed WELCOME"));
        c.welcome = *welcome;

        auto begin_frame = take(wire::SAVE_BEGIN, "checkpoint");
        if (!begin_frame)
            return std::unexpected(begin_frame.error());
        std::optional<wire::SaveBegin> begin = wire::decode_save_begin(*begin_frame);
        if (!begin)
            return std::unexpected(std::string("malformed SAVE_BEGIN"));
        if (begin->total > wire::MAX_SAVE_BYTES)
            return std::unexpected(std::string("the checkpoint is too large"));
        c.head = begin->boundary;
        c.checkpoint.reserve((size_t)begin->total);
        uint32_t index = 0;
        for (;;) {
            if (i == frames.size())
                return std::unexpected(std::string("the log ends inside the checkpoint"));
            std::string_view f = frames[i++];
            const uint16_t tag = wire::tag_of(f);
            if (tag == wire::SAVE_END) {
                std::optional<uint64_t> transfer = wire::decode_save_end(f);
                if (!transfer || (*transfer != begin->transfer))
                    return std::unexpected(std::string("malformed SAVE_END"));
                break;
            }
            if (tag != wire::SAVE_DATA)
                return std::unexpected("expected checkpoint data at frame " + std::to_string(i - 1));
            std::optional<wire::SaveData> data = wire::decode_save_data(f);
            if (!data || (data->transfer != begin->transfer))
                return std::unexpected(std::string("malformed SAVE_DATA"));
            if (data->index != index++)
                return std::unexpected(std::string("checkpoint data out of order"));
            if (c.checkpoint.size() + data->bytes.size() > begin->total)
                return std::unexpected(std::string("checkpoint data beyond its total"));
            c.checkpoint.insert(c.checkpoint.end(), data->bytes.begin(), data->bytes.end());
        }
        if (c.checkpoint.size() != begin->total)
            return std::unexpected(std::string("the checkpoint is short"));

        auto head_frame = take(wire::HASH, "checkpoint's hash");
        if (!head_frame)
            return std::unexpected(head_frame.error());
        std::optional<std::pair<uint64_t, uint64_t>> head_hash = wire::decode_hash(*head_frame);
        if (!head_hash)
            return std::unexpected(std::string("malformed HASH"));
        if (head_hash->first != c.head)
            return std::unexpected("the checkpoint's hash names step " + std::to_string(head_hash->first)
                                   + ", the checkpoint is at " + std::to_string(c.head));
        c.head_hash = head_hash->second;

        // The entries: STEP then HASH, each pair one step.  A STEP that is
        // the last frame is one the writer never got to report
        while (i != frames.size()) {
            std::string_view f = frames[i];
            if (wire::tag_of(f) != wire::STEP)
                return std::unexpected("expected a STEP at frame " + std::to_string(i));
            if (i + 1 == frames.size())
                break;
            std::optional<wire::Step> frame = wire::decode_step(f);
            if (!frame)
                return std::unexpected("malformed STEP at frame " + std::to_string(i));
            if (frame->number != c.last() + 1)
                return std::unexpected("step " + std::to_string(frame->number) + " after step "
                                       + std::to_string(c.last()));
            std::expected<Step, std::string> step = step_from_wire(*frame);
            if (!step)
                return std::unexpected(step.error() + " in step " + std::to_string(frame->number));
            std::string_view h = frames[i + 1];
            if (wire::tag_of(h) != wire::HASH)
                return std::unexpected("expected the HASH of step " + std::to_string(frame->number));
            std::optional<std::pair<uint64_t, uint64_t>> hash = wire::decode_hash(h);
            if (!hash)
                return std::unexpected("malformed HASH after step " + std::to_string(frame->number));
            if (hash->first != frame->number)
                return std::unexpected("a hash for step " + std::to_string(hash->first) + " after step "
                                       + std::to_string(frame->number));
            c.entries.push_back({std::move(*step), hash->second});
            i += 2;
        }
        return c;
    }

    std::expected<SessionLogContents, std::string> read_session_log(std::filesystem::path const& path) {
        auto bytes = read_file(path);
        if (!bytes)
            return std::unexpected(bytes.error());
        auto contents = decode_session_log(*bytes);
        if (!contents)
            return std::unexpected(path.string() + ": " + contents.error());
        return contents;
    }

    // ---- Comparing ------------------------------------------------------

    std::string compare_session_logs(SessionLogContents const& a, SessionLogContents const& b) {
        std::string out;
        auto range = [&](SessionLogContents const& c, const char* name) {
            out += name;
            out += ": checkpoint at time " + std::to_string(c.head) + ", ";
            if (c.entries.empty())
                out += "no steps";
            else
                out += "steps " + std::to_string(c.head + 1) + " to " + std::to_string(c.last());
            out += ", client " + std::to_string(c.welcome.client);
            if (!c.same_build())
                out += ", another build";
            out += '\n';
        };
        range(a, "A");
        range(b, "B");
        // Each checkpoint against the other log's record of that time
        auto check = [&](SessionLogContents const& x, const char* xn, SessionLogContents const& y, const char* yn) {
            if ((x.head > y.head) && (x.head <= y.last())) {
                const uint64_t recorded = y.entries[(size_t)(x.head - y.head - 1)].hash;
                out += std::string(xn) + "'s checkpoint " + ((recorded == x.head_hash) ? "matches " : "differs from ")
                     + yn + "'s hash after step " + std::to_string(x.head) + '\n';
            }
        };
        if (a.head == b.head) {
            out += (a.head_hash == b.head_hash) ? "checkpoints agree\n"
                 : "checkpoints differ at time " + std::to_string(a.head) + " (A " + hash_literal(a.head_hash)
                   + ", B " + hash_literal(b.head_hash) + ")\n";
        } else {
            check(a, "A", b, "B");
            check(b, "B", a, "A");
        }
        const uint64_t lo = std::max(a.head, b.head) + 1;
        const uint64_t hi = std::min(a.last(), b.last());
        if (lo > hi) {
            out += "no steps in common\n";
            return out;
        }
        out += "steps " + std::to_string(lo) + " to " + std::to_string(hi) + " in common\n";
        std::optional<uint64_t> streams_differ;
        std::optional<uint64_t> hashes_differ;
        for (uint64_t n = lo; n <= hi; ++n) {
            SessionLogContents::Entry const& ea = a.entries[(size_t)(n - a.head - 1)];
            SessionLogContents::Entry const& eb = b.entries[(size_t)(n - b.head - 1)];
            if (!streams_differ && (step_to_wire(ea.step) != step_to_wire(eb.step)))
                streams_differ = n;
            if (!hashes_differ && (ea.hash != eb.hash))
                hashes_differ = n;
            if (streams_differ && hashes_differ)
                break;
        }
        out += streams_differ ? "streams differ at step " + std::to_string(*streams_differ) + '\n'
                              : std::string("streams identical\n");
        if (hashes_differ) {
            const uint64_t n = *hashes_differ;
            out += "hashes differ at step " + std::to_string(n) + " (A "
                 + hash_literal(a.entries[(size_t)(n - a.head - 1)].hash) + ", B "
                 + hash_literal(b.entries[(size_t)(n - b.head - 1)].hash) + ")\n";
        } else {
            out += "hashes identical\n";
        }
        return out;
    }

    // ---- Tests ----------------------------------------------------------

    // A log written step by step reads back as what was written: the
    // handshake, the checkpoint (which loads as the World it was), its
    // hash, and every step with its hash; the whole-log encoder
    // reproduces the file; compare names a tampered hash or stream;
    // every truncation and random corruption of the file reads as a
    // prefix or fails, never crashes; and a directory keeps `keep` logs.
    define_test("session_log_roundtrip") {
        World* w = new World;
        w->_time = Time{7};
        w->_term_for_coordinate.set(Coordinate{1, 2}, term_make_integer_with(5));
        w->hack_repair_invariant();
        Root<World const*> root{w};
        const std::vector<uint8_t> bytes = serialize_world(w);
        const uint64_t head = 7;
        const uint64_t head_hash = w->content_hash();

        const std::filesystem::path dir = std::filesystem::temp_directory_path()
            / ("wry-session-log-" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);

        std::vector<Step> steps;
        {
            Step s;
            s.number = 8;
            s.commands.push_back(make_join_command(1));
            s.commands.push_back(make_action_command(1, 1, make_write_action(Coordinate{1, 2}, term_make_integer_with(9))));
            steps.push_back(std::move(s));
        }
        {
            Step s;
            s.number = 9;
            s.commands.push_back(make_action_command(1, 2, make_write_action(Coordinate{-3, 4},
                                                                             term_make_string_with("a longer heap string"))));
            s.commands.push_back(make_leave_command(2));
            Command paused;
            paused.kind = Command::PAUSED;
            s.commands.push_back(paused);
            steps.push_back(std::move(s));
        }
        {
            Step s;
            s.number = 10;
            steps.push_back(std::move(s));
        }
        const uint64_t hashes[3] = {0x1111, 0x2222, 0x3333};

        std::filesystem::path path;
        {
            SessionLog log(dir, head, head_hash, bytes);
            assert(log.path().empty());
            for (size_t i = 0; i != 3; ++i) {
                const bool appended = log.append(steps[i], hashes[i], 1, 16666666);
                assert(appended);
                (void) appended;
            }
            path = log.path();
            assert(!path.empty() && std::filesystem::exists(path));
        }

        auto contents = read_session_log(path);
        assert(contents);
        assert(contents->hello.protocol == wire::PROTOCOL_VERSION);
        assert(contents->same_build());
        assert(contents->welcome.client == 1 && contents->welcome.quantum_ns == 16666666);
        assert(contents->welcome.head == head && contents->welcome.first == head + 1);
        assert(contents->head == head && contents->head_hash == head_hash);
        assert(contents->checkpoint == bytes);
        assert(contents->entries.size() == 3);
        assert(contents->last() == 10);
        for (size_t i = 0; i != 3; ++i) {
            assert(contents->entries[i].step.number == 8 + i);
            assert(contents->entries[i].hash == hashes[i]);
            assert(step_to_wire(contents->entries[i].step) == step_to_wire(steps[i]));
        }
        assert(contents->entries[1].step.commands.size() == 3);
        assert(contents->entries[1].step.commands[2].kind == Command::PAUSED);
        {
            World* loaded = load_world_from_bytes(contents->checkpoint);
            assert(loaded);
            Root<World const*> keep{loaded};
            assert(loaded->content_hash() == head_hash);
        }

        // The whole-log encoder reproduces the file
        auto file = read_file(path);
        assert(file);
        {
            std::string encoded = encode_session_log(*contents);
            assert(encoded.size() == file->size());
            assert(std::memcmp(encoded.data(), file->data(), encoded.size()) == 0);
        }

        // Comparing: itself, a tampered hash, a tampered stream, and a
        // log that starts inside this one
        auto contains = [](std::string const& s, std::string const& what) {
            return s.find(what) != std::string::npos;
        };
        {
            std::string same = compare_session_logs(*contents, *contents);
            assert(contains(same, "checkpoints agree"));
            assert(contains(same, "steps 8 to 10 in common"));
            assert(contains(same, "streams identical"));
            assert(contains(same, "hashes identical"));
        }
        {
            auto other = decode_session_log(*file);
            assert(other);
            other->entries[1].hash ^= 1;
            std::string report = compare_session_logs(*contents, *other);
            assert(contains(report, "streams identical"));
            assert(contains(report, "hashes differ at step 9"));
        }
        {
            auto other = decode_session_log(*file);
            assert(other);
            other->entries[1].step.commands.pop_back();
            std::string report = compare_session_logs(*contents, *other);
            assert(contains(report, "streams differ at step 9"));
        }
        {
            // A joiner's log: checkpoint at 8 with the hash this one
            // recorded after step 8, then steps 9 and 10
            SessionLogContents later;
            later.hello = contents->hello;
            later.welcome = wire::Welcome{2, 16666666, 8, 9};
            later.head = 8;
            later.head_hash = hashes[0];
            later.checkpoint = bytes;
            later.entries.push_back({steps[1], hashes[1]});
            later.entries.push_back({steps[2], hashes[2]});
            std::string report = compare_session_logs(*contents, later);
            assert(contains(report, "B's checkpoint matches A's hash after step 8"));
            assert(contains(report, "steps 9 to 10 in common"));
            assert(contains(report, "hashes identical"));
            later.head_hash ^= 1;
            report = compare_session_logs(later, *contents);
            assert(contains(report, "A's checkpoint differs from B's hash after step 8"));
        }

        // Every truncation reads as a prefix, or fails; never beyond
        for (size_t n = 0; n < file->size(); ++n) {
            auto r = decode_session_log(std::span<const uint8_t>(*file).first(n));
            if (r) {
                assert(r->entries.size() <= 3);
                for (size_t i = 0; i != r->entries.size(); ++i) {
                    assert(r->entries[i].step.number == 8 + i);
                    assert(r->entries[i].hash == hashes[i]);
                }
            }
        }
        // Random corruption never crashes
        {
            std::mt19937_64 rng(20261021);
            for (int k = 0; k != 300; ++k) {
                std::vector<uint8_t> corrupt = *file;
                const int flips = 1 + (int)(rng() % 3);
                for (int f = 0; f != flips; ++f)
                    corrupt[(size_t)(rng() % corrupt.size())] ^= (uint8_t)(1 + (rng() % 255));
                (void) decode_session_log(corrupt);
            }
        }

        // Rotation: more logs than `keep` leave `keep`
        for (size_t k = 0; k != SessionLog::keep + 3; ++k) {
            SessionLog log(dir, head, head_hash, bytes);
            const bool appended = log.append(steps[0], 1, 1, 0);
            assert(appended);
            (void) appended;
        }
        {
            size_t count = 0;
            for (auto const& entry : std::filesystem::directory_iterator(dir))
                if (entry.path().extension() == ".wrylog")
                    ++count;
            assert(count == SessionLog::keep);
        }

        std::filesystem::remove_all(dir);
        co_return;
    };

} // namespace wry
