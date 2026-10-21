//
//  wire.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  The game's wire protocol (game/docs/multiplayer.md section 7): the
//  frame tags, their length caps, and the encoding of each message.
//  Frames are `u32 length | u16 tag | payload`, little-endian, no
//  alignment; the Transport's FrameCodec owns the first field, this
//  module the rest.  Every decoder is bounds-checked and total: a
//  malformed payload is nullopt, never a crash, and the caller treats it
//  as a violation (the relay) or a no-op (a client).
//
//  The relay reads envelopes only.  An ACTION's body, and the bytes of a
//  STEP entry, are opaque to it: the canonical command line of a
//  Player::Action (game/command_types.hpp), which only clients parse.
//

#ifndef wire_hpp
#define wire_hpp

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "server.hpp"
#include "transport.hpp"

namespace wry::wire {

    // Bumped whenever a message's layout changes; equality is enforced at
    // the handshake, as is the build (multiplayer.md section 1)
    inline constexpr uint32_t PROTOCOL_VERSION = 1;

    // The build identity for HELLO.  For now the compile time of this
    // module, which two processes running one binary share; a stamped
    // revision is the real thing (multiplayer.md 9.8)
    uint64_t build_id();

    enum Tag : uint16_t {
        HELLO = 1,          // client -> server: versions
        WELCOME = 2,        // server -> client: client id, quantum, head
        ACTION = 3,         // client -> server: seq, body
        STEP = 4,           // server -> client: a finalized step
        JOIN_REQUEST = 5,   // client -> server: a name
        QUIT = 6,           // client -> server
        PING = 7,           // server -> client: liveness while paused
        KICK = 8,           // server -> client: the farewell, a reason
        SHUTDOWN = 9,       // server -> client
        HASH = 10,          // client -> server: step, hash
        REQUEST_SAVE = 11,  // server -> donor: transfer, boundary
        SAVE_BEGIN = 12,    // donor -> server -> joiner: transfer, boundary, total
        SAVE_DATA = 13,     // donor -> server -> joiner: transfer, index, bytes
        SAVE_END = 14,      // donor -> server -> joiner: transfer
    };

    inline constexpr size_t MAX_ACTION_BYTES = 1024;    // an ACTION's body
    inline constexpr size_t MAX_NAME_BYTES = 64;        // a JOIN_REQUEST's name
    inline constexpr size_t MAX_REASON_BYTES = 1024;    // a KICK's reason
    inline constexpr size_t MAX_STEP_BYTES = 2 << 20;   // a STEP frame
    inline constexpr size_t MAX_SAVE_CHUNK = 1 << 20;   // a SAVE_DATA payload
    inline constexpr size_t MAX_SAVE_BYTES = 256 << 20; // a whole donated save

    // The cap on a frame's length (tag and payload) by tag; 0 for a tag
    // the protocol does not have
    size_t cap_for_tag(uint16_t tag);

    // The codec every game transport uses
    Transport::FrameCodec const& codec();

    uint16_t tag_of(std::string_view message);

    struct Hello {
        uint32_t protocol;
        uint64_t build;
        uint64_t tier2;
    };

    struct Welcome {
        ClientID client;
        uint64_t quantum_ns;
        uint64_t head;      // the last step finalized
        uint64_t first;     // the number of the first step this client will get
    };

    struct Action {
        uint64_t seq;
        std::string_view body;
    };

    // One entry of a STEP: who, their sequence number, and the bytes --
    // an action's line for a client, an encoded event for the server
    struct Entry {
        ClientID origin;
        uint64_t seq;
        std::string bytes;
    };

    struct Step {
        uint64_t number;
        std::vector<Entry> entries;
    };

    // A server event, as the bytes of a server-origin entry
    struct Event {
        Command::Kind kind;
        ClientID client;
    };

    struct SaveRequest {
        uint64_t transfer;
        uint64_t boundary;
    };

    struct SaveBegin {
        uint64_t transfer;
        uint64_t boundary;
        uint64_t total;
    };

    struct SaveData {
        uint64_t transfer;
        uint32_t index;
        std::string_view bytes;
    };

    // Encoders produce the message: tag then payload.  The transport
    // frames it
    std::string encode_hello(Hello const& hello);
    std::string encode_welcome(Welcome const& welcome);
    std::string encode_action(uint64_t seq, std::string_view body);
    std::string encode_step(uint64_t number, std::span<Entry const> entries);
    std::string encode_event(Command::Kind kind, ClientID client);
    std::string encode_join_request(std::string_view name);
    std::string encode_quit();
    std::string encode_ping();
    std::string encode_shutdown();
    std::string encode_hash(uint64_t step, uint64_t hash);
    std::string encode_save_request(uint64_t transfer, uint64_t boundary);
    std::string encode_save_begin(uint64_t transfer, uint64_t boundary, uint64_t total);
    std::string encode_save_data(uint64_t transfer, uint32_t index, std::string_view bytes);
    std::string encode_save_end(uint64_t transfer);

    // Decoders take the whole message and check its tag
    std::optional<Hello> decode_hello(std::string_view message);
    std::optional<Welcome> decode_welcome(std::string_view message);
    std::optional<Action> decode_action(std::string_view message);
    std::optional<Step> decode_step(std::string_view message);
    std::optional<Event> decode_event(std::string_view bytes);
    std::optional<std::string_view> decode_join_request(std::string_view message);
    std::optional<std::string_view> decode_kick(std::string_view message);
    std::optional<std::pair<uint64_t, uint64_t>> decode_hash(std::string_view message);
    std::optional<SaveRequest> decode_save_request(std::string_view message);
    std::optional<SaveBegin> decode_save_begin(std::string_view message);
    std::optional<SaveData> decode_save_data(std::string_view message);
    std::optional<uint64_t> decode_save_end(std::string_view message);

} // namespace wry::wire

#endif /* wire_hpp */
