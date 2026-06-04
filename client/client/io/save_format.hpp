//
//  save_format.hpp
//  client
//
//  Worked sketch of the save format.  Post-order DFS from a frozen World
//  snapshot, dense positional load-order IDs, per-type virtual or template
//  dispatch.  Designed for background-thread save against a persistent-DS
//  snapshot; load is the latency-critical path.
//
//  This is v1 sketch quality: in-memory buffers, no zstd, no schema-version
//  handshake, no incremental save.  All deferrable; the shape is the point.
//

#ifndef save_format_hpp
#define save_format_hpp

#include <cassert>
#include <cstring>
#include <vector>
#include <unordered_map>

#include "stdint.hpp"
#include "save_types.hpp"

namespace wry {

    struct GarbageCollected;
    struct Entity;
    struct HeapTerm;
    struct World;
    struct Term;

    // ---------------------------------------------------------------------
    // Save format version.  Bumped any time the in-RAM Term layout, the
    // GarbageCollected header layout, or the record encoding changes in a
    // way that would silently corrupt an older file.  The loader's
    // [version u32] header field is compared against this; mismatch is a
    // hard reject (no migration path in this sketch).
    //
    // Version 1: initial sketch (no shipped saves).
    // Version 2: bumped 2026-05-24 alongside the Term tag renumber and
    //            ENUMERATION-as-meta-tag fold (review commit 2/3).
    // ---------------------------------------------------------------------

    enum : uint32_t { TERM_SAVE_VERSION = 2 };

    // ---------------------------------------------------------------------
    // Load-order ID.  Dense uint32_t assigned in post-order DFS from World.
    // ID 0 is reserved for null references.  IDs are valid only within one
    // save file; they are NOT EntityID or any other game-domain identity.
    // ---------------------------------------------------------------------

    enum : uint32_t { SAVE_REF_NULL = 0 };
    using SaveRef = uint32_t;

    // ---------------------------------------------------------------------
    // Saver: emits records to a growing byte buffer.  Mutator never touches
    // this; the saver thread holds a frozen snapshot root and walks it.
    // ---------------------------------------------------------------------

    struct Saver {

        std::vector<uint8_t> _stream;
        std::unordered_map<const void*, SaveRef> _seen;
        SaveRef _next_ref = 1;  // 0 reserved for null

        // Pending back-edges: when a cycle is detected mid-walk, the saver
        // emits a placeholder and records (offset_in_stream, target_ptr).
        // After the walk completes, _resolve_pending() patches the stream.
        // Expected empty in the DAG case (Term cycles are the only source).
        struct Pending { size_t offset; const void* target; };
        std::vector<Pending> _pending;

        // Raw byte writers.

        void write_bytes(const void* data, size_t n) {
            const uint8_t* p = (const uint8_t*)data;
            _stream.insert(_stream.end(), p, p + n);
        }

        template<typename T>
        void write_pod(const T& x) {
            static_assert(std::is_trivially_copyable_v<T>);
            write_bytes(&x, sizeof(T));
        }

        void write_u32(uint32_t x) { write_pod(x); }
        void write_u64(uint64_t x) { write_pod(x); }

        // Write a LEB128 varint.  Most tags and lengths compress small.
        void write_varint(uint64_t x) {
            while (x >= 0x80) {
                _stream.push_back((uint8_t)(x & 0x7f) | 0x80);
                x >>= 7;
            }
            _stream.push_back((uint8_t)x);
        }

        // Write a SaveRef.  In the common path, callers already resolved the
        // target via visit_*() and pass the resulting ref.  For back-edges,
        // callers pass the in-progress target pointer to record_back_edge()
        // first, which writes a placeholder and registers it for patching.
        void write_ref(SaveRef r) {
            write_u32(r);
        }

        void record_back_edge(const void* target) {
            _pending.push_back({ _stream.size(), target });
            write_u32(SAVE_REF_NULL);  // placeholder
        }

        // Visit a polymorphic Entity*.  Returns its assigned SaveRef.
        // Implementation lives in save_format.cpp because it dispatches via
        // virtual functions.
        SaveRef visit_entity(const Entity* _Nullable p);
        SaveRef visit_heap_value(const HeapTerm* _Nullable p);

        // Visit a non-polymorphic GC object of statically-known type T.
        // T must specialize save_type_traits<T> with ::value (the tag) and
        // declare a static void emit_body(const T*, Saver&).
        template<typename T>
        SaveRef visit(const T* _Nullable p);

        // Begin a record: writes (tag, body length placeholder).  Returns
        // the stream offset where the body starts, so caller can patch the
        // length once the body is done.  Internal use by visit_*().
        size_t begin_record(uint64_t type_tag) {
            write_varint(type_tag);
            size_t len_offset = _stream.size();
            uint32_t placeholder = 0;
            write_pod(placeholder);  // 4-byte fixed-size length, patched later
            return len_offset;
        }

        void end_record(size_t len_offset) {
            uint32_t body_len = (uint32_t)(_stream.size() - len_offset - sizeof(uint32_t));
            std::memcpy(_stream.data() + len_offset, &body_len, sizeof(uint32_t));
        }

        // Top-level: walk the World snapshot, returning the SaveRef of the
        // World record (always the last record, by construction).
        SaveRef save_world(const World* _Nonnull root);

        // After the walk, patch any pending back-edges.  In the DAG case,
        // this is a no-op.
        void resolve_pending();

    }; // struct Saver

    // ---------------------------------------------------------------------
    // Loader: single-pass reader.  ptrs[id] is filled as records arrive.
    // ---------------------------------------------------------------------

    struct Loader {

        const uint8_t* _cursor = nullptr;
        const uint8_t* _end = nullptr;

        // Indexed by SaveRef.  Sized once from the file header.
        std::vector<void*> _ptrs;

        // Forward refs (ref_id >= current_record_id when read).  Patched
        // after the whole file is consumed.
        struct Fixup { void** field; SaveRef target; };
        std::vector<Fixup> _fixups;

        // Raw byte readers.

        template<typename T>
        T read_pod() {
            static_assert(std::is_trivially_copyable_v<T>);
            assert(_cursor + sizeof(T) <= _end);
            T x;
            std::memcpy(&x, _cursor, sizeof(T));
            _cursor += sizeof(T);
            return x;
        }

        uint32_t read_u32() { return read_pod<uint32_t>(); }
        uint64_t read_u64() { return read_pod<uint64_t>(); }

        uint64_t read_varint() {
            uint64_t x = 0;
            int shift = 0;
            for (;;) {
                assert(_cursor < _end);
                uint8_t b = *_cursor++;
                x |= (uint64_t)(b & 0x7f) << shift;
                if (!(b & 0x80)) return x;
                shift += 7;
                assert(shift < 64);
            }
        }

        // Resolve a SaveRef to a pointer, registering a fixup if the target
        // has not been materialized yet.  In the DAG case, all refs resolve
        // immediately and _fixups stays empty.
        template<typename T>
        T* _Nullable resolve(SaveRef r, T** field_for_fixup = nullptr) {
            if (r == SAVE_REF_NULL) return nullptr;
            assert(r < _ptrs.size());
            if (_ptrs[r]) return (T*)_ptrs[r];
            // forward reference; record for later patching.  field_for_fixup
            // must point to the pointer field we want patched.
            assert(field_for_fixup);
            _fixups.push_back({ (void**)field_for_fixup, r });
            return nullptr;
        }

        // Drive the load.  Returns the World root.
        World* load_world();

        // After the main pass, patch any forward refs (empty in DAG case).
        void resolve_fixups();

    }; // struct Loader

    // ---------------------------------------------------------------------
    // SaveableTraits: per-concrete-type dispatch entry.  One row per type
    // listed in WRY_FOR_EACH_SAVEABLE_TYPE.  Built once at startup from the
    // X-macro list.
    // ---------------------------------------------------------------------

    struct SaveableTraits {
        uint64_t tag;
        const char* name;
        // Allocate + deserialize.  Reads the record body from loader, allocates
        // a GarbageCollected (or other) of the right type, stores into
        // loader._ptrs[id], and reads field values into it.
        void (* _Nonnull load_into)(Loader& loader, SaveRef id);
    };

    // Returns nullptr if tag is unknown (unrecognized type — likely a save
    // from a newer build).  Callers can decide whether to skip the record
    // (using the length field) or abort.
    const SaveableTraits* _Nullable find_saveable_traits(uint64_t tag);

}  // namespace wry

#endif  // save_format_hpp
