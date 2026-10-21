//
//  wire.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include <cstring>
#include <random>

#include "assert.hpp"
#include "hash.hpp"
#include "test.hpp"
#include "wire.hpp"

namespace wry::wire {

    uint64_t build_id() {
        static const uint64_t id = fnv1a(__DATE__ " " __TIME__);
        return id;
    }

    size_t cap_for_tag(uint16_t tag) {
        switch (tag) {
            case HELLO:        return 2 + 4 + 8 + 8;
            case WELCOME:      return 2 + 8 + 8 + 8 + 8;
            case ACTION:       return 2 + 8 + MAX_ACTION_BYTES;
            case STEP:         return MAX_STEP_BYTES;
            case JOIN_REQUEST: return 2 + MAX_NAME_BYTES;
            case QUIT:         return 2;
            case PING:         return 2;
            case KICK:         return 2 + MAX_REASON_BYTES;
            case SHUTDOWN:     return 2;
            case HASH:         return 2 + 8 + 8;
            case REQUEST_SAVE: return 2 + 8 + 8;
            case SAVE_BEGIN:   return 2 + 8 + 8 + 8;
            case SAVE_DATA:    return 2 + 8 + 4 + MAX_SAVE_CHUNK;
            case SAVE_END:     return 2 + 8;
            default:           return 0;
        }
    }

    Transport::FrameCodec const& codec() {
        static Transport::FrameCodec const c{cap_for_tag, KICK};
        return c;
    }

    uint16_t tag_of(std::string_view message) {
        return Transport::FrameCodec::tag_of(message);
    }

    // ----------------------------------------------------------------
    // Byte readers and writers.

    namespace {

        struct Writer {
            std::string bytes;
            explicit Writer(uint16_t tag) {
                u16(tag);
            }
            template<typename T>
            void pod(T x) {
                static_assert(std::is_trivially_copyable_v<T>);
                char buffer[sizeof x];
                std::memcpy(buffer, &x, sizeof x);
                bytes.append(buffer, sizeof x);
            }
            void u8(uint8_t x) { pod(x); }
            void u16(uint16_t x) { pod(x); }
            void u32(uint32_t x) { pod(x); }
            void u64(uint64_t x) { pod(x); }
            void raw(std::string_view x) { bytes.append(x); }
        };

        struct Reader {
            std::string_view bytes;
            size_t at = 0;
            bool ok = true;
            explicit Reader(std::string_view b) : bytes(b) {}
            template<typename T>
            T pod() {
                T x{};
                if (!ok || (bytes.size() - at < sizeof x)) {
                    ok = false;
                    return x;
                }
                std::memcpy(&x, bytes.data() + at, sizeof x);
                at += sizeof x;
                return x;
            }
            uint8_t u8() { return pod<uint8_t>(); }
            uint16_t u16() { return pod<uint16_t>(); }
            uint32_t u32() { return pod<uint32_t>(); }
            uint64_t u64() { return pod<uint64_t>(); }
            std::string_view raw(size_t n) {
                if (!ok || (bytes.size() - at < n)) {
                    ok = false;
                    return {};
                }
                std::string_view x = bytes.substr(at, n);
                at += n;
                return x;
            }
            std::string_view rest() {
                return raw(bytes.size() - at);
            }
            // The whole message was consumed, and nothing failed
            bool done() const {
                return ok && (at == bytes.size());
            }
        };

        bool expect_tag(Reader& r, uint16_t tag) {
            return r.u16() == tag && r.ok;
        }

    } // namespace

    // ----------------------------------------------------------------
    // Encoders.

    std::string encode_hello(Hello const& hello) {
        Writer w{HELLO};
        w.u32(hello.protocol);
        w.u64(hello.build);
        w.u64(hello.tier2);
        return std::move(w.bytes);
    }

    std::string encode_welcome(Welcome const& welcome) {
        Writer w{WELCOME};
        w.u64(welcome.client);
        w.u64(welcome.quantum_ns);
        w.u64(welcome.head);
        w.u64(welcome.first);
        return std::move(w.bytes);
    }

    std::string encode_action(uint64_t seq, std::string_view body) {
        assert(body.size() <= MAX_ACTION_BYTES);
        Writer w{ACTION};
        w.u64(seq);
        w.raw(body);
        return std::move(w.bytes);
    }

    std::string encode_step(uint64_t number, std::span<Entry const> entries) {
        Writer w{STEP};
        w.u64(number);
        assert(entries.size() <= UINT32_MAX);
        w.u32((uint32_t)entries.size());
        for (Entry const& e : entries) {
            w.u64(e.origin);
            w.u64(e.seq);
            assert(e.bytes.size() <= UINT32_MAX);
            w.u32((uint32_t)e.bytes.size());
            w.raw(e.bytes);
        }
        return std::move(w.bytes);
    }

    std::string encode_event(Command::Kind kind, ClientID client) {
        std::string bytes;
        bytes.push_back((char)(uint8_t)kind);
        char buffer[sizeof client];
        std::memcpy(buffer, &client, sizeof client);
        bytes.append(buffer, sizeof client);
        return bytes;
    }

    std::string encode_join_request(std::string_view name) {
        assert(name.size() <= MAX_NAME_BYTES);
        Writer w{JOIN_REQUEST};
        w.raw(name);
        return std::move(w.bytes);
    }

    std::string encode_quit() {
        return std::move(Writer{QUIT}.bytes);
    }

    std::string encode_ping() {
        return std::move(Writer{PING}.bytes);
    }

    std::string encode_shutdown() {
        return std::move(Writer{SHUTDOWN}.bytes);
    }

    std::string encode_hash(uint64_t step, uint64_t hash) {
        Writer w{HASH};
        w.u64(step);
        w.u64(hash);
        return std::move(w.bytes);
    }

    std::string encode_save_request(uint64_t transfer, uint64_t boundary) {
        Writer w{REQUEST_SAVE};
        w.u64(transfer);
        w.u64(boundary);
        return std::move(w.bytes);
    }

    std::string encode_save_begin(uint64_t transfer, uint64_t boundary, uint64_t total) {
        Writer w{SAVE_BEGIN};
        w.u64(transfer);
        w.u64(boundary);
        w.u64(total);
        return std::move(w.bytes);
    }

    std::string encode_save_data(uint64_t transfer, uint32_t index, std::string_view bytes) {
        assert(bytes.size() <= MAX_SAVE_CHUNK);
        Writer w{SAVE_DATA};
        w.u64(transfer);
        w.u32(index);
        w.raw(bytes);
        return std::move(w.bytes);
    }

    std::string encode_save_end(uint64_t transfer) {
        Writer w{SAVE_END};
        w.u64(transfer);
        return std::move(w.bytes);
    }

    // ----------------------------------------------------------------
    // Decoders.

    std::optional<Hello> decode_hello(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, HELLO))
            return std::nullopt;
        Hello h;
        h.protocol = r.u32();
        h.build = r.u64();
        h.tier2 = r.u64();
        if (!r.done())
            return std::nullopt;
        return h;
    }

    std::optional<Welcome> decode_welcome(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, WELCOME))
            return std::nullopt;
        Welcome w;
        w.client = r.u64();
        w.quantum_ns = r.u64();
        w.head = r.u64();
        w.first = r.u64();
        if (!r.done())
            return std::nullopt;
        return w;
    }

    std::optional<Action> decode_action(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, ACTION))
            return std::nullopt;
        Action a;
        a.seq = r.u64();
        a.body = r.rest();
        if (!r.done() || (a.body.size() > MAX_ACTION_BYTES))
            return std::nullopt;
        return a;
    }

    std::optional<Step> decode_step(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, STEP))
            return std::nullopt;
        Step s;
        s.number = r.u64();
        uint32_t count = r.u32();
        if (!r.ok)
            return std::nullopt;
        // Each entry is at least 20 bytes, so a count the message cannot
        // hold is refused before anything is reserved
        if ((size_t)count > (message.size() - r.at) / 20)
            return std::nullopt;
        s.entries.reserve(count);
        for (uint32_t i = 0; i != count; ++i) {
            Entry e;
            e.origin = r.u64();
            e.seq = r.u64();
            uint32_t length = r.u32();
            if (!r.ok)
                return std::nullopt;
            e.bytes = std::string(r.raw(length));
            if (!r.ok)
                return std::nullopt;
            s.entries.push_back(std::move(e));
        }
        if (!r.done())
            return std::nullopt;
        return s;
    }

    std::optional<Event> decode_event(std::string_view bytes) {
        Reader r{bytes};
        Event e;
        uint8_t kind = r.u8();
        e.client = r.u64();
        if (!r.done())
            return std::nullopt;
        switch (kind) {
            case Command::JOIN:
            case Command::LEAVE:
            case Command::PAUSED:
            case Command::RESUMED:
                e.kind = (Command::Kind)kind;
                return e;
            default:
                return std::nullopt;
        }
    }

    std::optional<std::string_view> decode_join_request(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, JOIN_REQUEST))
            return std::nullopt;
        std::string_view name = r.rest();
        if (!r.done() || (name.size() > MAX_NAME_BYTES))
            return std::nullopt;
        return name;
    }

    std::optional<std::string_view> decode_kick(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, KICK))
            return std::nullopt;
        std::string_view reason = r.rest();
        if (!r.done())
            return std::nullopt;
        return reason;
    }

    std::optional<std::pair<uint64_t, uint64_t>> decode_hash(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, HASH))
            return std::nullopt;
        uint64_t step = r.u64();
        uint64_t hash = r.u64();
        if (!r.done())
            return std::nullopt;
        return std::pair{step, hash};
    }

    std::optional<SaveRequest> decode_save_request(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, REQUEST_SAVE))
            return std::nullopt;
        SaveRequest q;
        q.transfer = r.u64();
        q.boundary = r.u64();
        if (!r.done())
            return std::nullopt;
        return q;
    }

    std::optional<SaveBegin> decode_save_begin(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, SAVE_BEGIN))
            return std::nullopt;
        SaveBegin b;
        b.transfer = r.u64();
        b.boundary = r.u64();
        b.total = r.u64();
        if (!r.done())
            return std::nullopt;
        return b;
    }

    std::optional<SaveData> decode_save_data(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, SAVE_DATA))
            return std::nullopt;
        SaveData d;
        d.transfer = r.u64();
        d.index = r.u32();
        d.bytes = r.rest();
        if (!r.done() || (d.bytes.size() > MAX_SAVE_CHUNK))
            return std::nullopt;
        return d;
    }

    std::optional<uint64_t> decode_save_end(std::string_view message) {
        Reader r{message};
        if (!expect_tag(r, SAVE_END))
            return std::nullopt;
        uint64_t transfer = r.u64();
        if (!r.done())
            return std::nullopt;
        return transfer;
    }

    // ----------------------------------------------------------------
    // Tests.

    define_test("wire_codec") {
        std::mt19937_64 rng(2026);

        // Every message round-trips, and its frame fits its cap
        {
            Hello h{PROTOCOL_VERSION, build_id(), 0};
            std::string m = encode_hello(h);
            assert(m.size() <= cap_for_tag(tag_of(m)));
            auto d = decode_hello(m);
            assert(d && d->protocol == h.protocol && d->build == h.build && d->tier2 == h.tier2);
        }
        {
            Welcome w{7, 16666667, 12345, 100};
            auto d = decode_welcome(encode_welcome(w));
            assert(d && d->client == 7 && d->quantum_ns == 16666667 && d->head == 12345 && d->first == 100);
        }
        {
            std::string body = "write Coordinate(1, 2) OPCODE::SWAP";
            std::string m = encode_action(99, body);
            assert(m.size() <= cap_for_tag(ACTION));
            auto d = decode_action(m);
            assert(d && d->seq == 99 && d->body == body);
            std::string over = encode_action(1, "");
            over.append(MAX_ACTION_BYTES + 1, 'x');
            assert(!decode_action(over));
        }
        {
            std::vector<Entry> entries;
            entries.push_back({SERVER_CLIENT_ID, 0, encode_event(Command::JOIN, 3)});
            entries.push_back({3, 1, "write Coordinate(0, 0) 1"});
            entries.push_back({5, 7, ""});
            std::string m = encode_step(42, entries);
            auto d = decode_step(m);
            assert(d && d->number == 42 && d->entries.size() == 3);
            for (size_t i = 0; i != 3; ++i) {
                assert(d->entries[i].origin == entries[i].origin);
                assert(d->entries[i].seq == entries[i].seq);
                assert(d->entries[i].bytes == entries[i].bytes);
            }
            auto e = decode_event(d->entries[0].bytes);
            assert(e && e->kind == Command::JOIN && e->client == 3);
            assert(!decode_event("x"));
            assert(!decode_event(encode_event((Command::Kind)9, 1)));
            auto empty = decode_step(encode_step(1, {}));
            assert(empty && empty->number == 1 && empty->entries.empty());
        }
        {
            std::string request = encode_join_request("antony");
            auto n = decode_join_request(request);
            assert(n && *n == "antony");
            std::string long_name = encode_join_request("");
            long_name.append(MAX_NAME_BYTES + 1, 'n');
            assert(!decode_join_request(long_name));
            assert(decode_kick(codec().farewell("because")).value() == "because");
            auto h = decode_hash(encode_hash(10, 0x1234));
            assert(h && h->first == 10 && h->second == 0x1234);
            assert(tag_of(encode_quit()) == QUIT && encode_quit().size() == 2);
            assert(tag_of(encode_ping()) == PING && tag_of(encode_shutdown()) == SHUTDOWN);
        }
        {
            auto q = decode_save_request(encode_save_request(5, 77));
            assert(q && q->transfer == 5 && q->boundary == 77);
            auto b = decode_save_begin(encode_save_begin(5, 77, 123456));
            assert(b && b->transfer == 5 && b->boundary == 77 && b->total == 123456);
            std::string payload(MAX_SAVE_CHUNK, 'p');
            std::string data = encode_save_data(5, 3, payload);
            assert(data.size() <= cap_for_tag(SAVE_DATA));
            auto d = decode_save_data(data);
            assert(d && d->transfer == 5 && d->index == 3 && d->bytes == payload);
            std::string over = encode_save_data(5, 0, "");
            over.append(MAX_SAVE_CHUNK + 1, 'p');
            assert(!decode_save_data(over));
            auto e = decode_save_end(encode_save_end(5));
            assert(e && *e == 5);
            assert(!decode_save_end(encode_save_request(5, 77)));
        }

        // The wrong tag, a truncation, or trailing bytes is nullopt, never
        // a crash
        {
            std::vector<std::string> messages = {
                encode_hello({1, 2, 3}), encode_welcome({1, 2, 3, 4}), encode_action(1, "abc"),
                encode_step(1, std::vector<Entry>{{1, 1, "abc"}, {0, 0, encode_event(Command::LEAVE, 2)}}),
                encode_join_request("n"), encode_hash(1, 2),
                encode_save_request(1, 2), encode_save_begin(1, 2, 3), encode_save_data(1, 2, "abc"),
                encode_save_end(1),
            };
            for (std::string const& m : messages) {
                assert(!decode_hello(encode_quit()) && !decode_welcome(encode_quit()));
                for (size_t cut = 0; cut < m.size(); ++cut) {
                    std::string_view t(m.data(), cut);
                    (void) decode_hello(t);
                    (void) decode_welcome(t);
                    (void) decode_action(t);
                    (void) decode_step(t);
                    (void) decode_join_request(t);
                    (void) decode_hash(t);
                    (void) decode_save_request(t);
                    (void) decode_save_begin(t);
                    (void) decode_save_data(t);
                    (void) decode_save_end(t);
                }
                std::string longer = m + "!";
                assert(!decode_hello(longer) && !decode_welcome(longer) && !decode_step(longer)
                       && !decode_hash(longer));
            }
            // A STEP claiming more entries than it holds, or an entry
            // longer than the rest of the message
            std::string s = encode_step(1, std::vector<Entry>{{1, 1, "abc"}});
            std::string huge = s;
            uint32_t count = 0x7fffffff;
            std::memcpy(huge.data() + 2 + 8, &count, 4);
            assert(!decode_step(huge));
            std::string overlong = s;
            uint32_t length = 1000;
            std::memcpy(overlong.data() + 2 + 8 + 4 + 8 + 8, &length, 4);
            assert(!decode_step(overlong));
            // Random bytes
            for (int i = 0; i != 2000; ++i) {
                std::string junk(rng() % 64, '\0');
                for (char& c : junk)
                    c = (char)(rng() & 0xff);
                (void) decode_step(junk);
                (void) decode_action(junk);
                (void) decode_event(junk);
            }
        }
        co_return;
    };

} // namespace wry::wire
