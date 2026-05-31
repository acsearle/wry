//
//  save_format.cpp
//  client
//
//  Implementation of the save format sketch.  See save_format.hpp.
//
//  The worked type universe in this sketch:
//    - World (root, non-polymorphic)
//    - Machine (Entity subclass, polymorphic via Entity*)
//    - HeapInt64 (HeapValue subclass, polymorphic via HeapValue*)
//    - HeapString (HeapValue subclass; SKETCH stubs, factory-based load)
//    - array_mapped_trie::Node<Value, uint64_t>          // value-for-coordinate map leaves
//    - array_mapped_trie::Node<EntityID, uint64_t>       // entity-id-for-coordinate map leaves
//    - array_mapped_trie::Node<const Entity*, uint64_t>  // entity-for-entity-id map leaves
//    - array_mapped_trie::Node<int, uint64_t>            // PersistentSet payload-less node
//    - PersistentStack<Value>                      // machine stack cells
//
//  Other AMT instantiations needed by World (e.g. for the pair<Time,EntityID>
//  set inside PersistentSet) are listed in the registry; their emit/load
//  bodies follow the same pattern and are left as TODO in this sketch.
//

#include <cstdio>

#include "save_format.hpp"

#include "array_mapped_trie.hpp"
#include "entity.hpp"
#include "HeapString.hpp"
#include "machine.hpp"
#include "persistent_set.hpp"
#include "persistent_stack.hpp"
#include "value.hpp"
#include "world.hpp"

namespace wry {

    // -----------------------------------------------------------------------
    // Value encoding.  Values are 64-bit tagged words.  In the OBJECT case
    // the high bits are an in-memory HeapValue pointer; on disk we replace
    // those bits with a SaveRef.  All other tags travel as-is.
    //
    // NOTE: this assumes no Value cycles.  A self-referential container would
    // require the fixup path (because the HeapValue is not yet emitted when
    // we try to encode a Value pointing to it).  Cycle handling is sketched
    // in Saver::record_back_edge / Loader::Fixup but not driven here.
    // -----------------------------------------------------------------------

    static uint64_t encode_value(const Value& v, Saver& s) {
        if (_value_is_object(v)) {
            const HeapValue* p = _value_as_object(v);
            SaveRef ref = s.visit_heap_value(p);
            return ((uint64_t)ref << VALUE_SHIFT) | VALUE_TAG_OBJECT;
        }
        return v._data;
    }

    static Value decode_value(uint64_t word, Loader& L) {
        Value v;
        if ((word & VALUE_MASK_TAG) == VALUE_TAG_OBJECT) {
            SaveRef ref = (SaveRef)(word >> VALUE_SHIFT);
            // Sketch: forward refs from Value would need fixup against the
            // address of v._data.  Assume DAG for now.
            HeapValue* p = (HeapValue*)L._ptrs[ref];
            v._data = (uint64_t)p | VALUE_TAG_OBJECT;
        } else {
            v._data = word;
        }
        return v;
    }

    // -----------------------------------------------------------------------
    // Per-type save_type_traits specializations.
    // For polymorphic bases we never use save_type_traits<Entity>; dispatch
    // is via the virtual _save_type_tag().  We only declare traits for the
    // concrete types we register.
    // -----------------------------------------------------------------------

    template<> struct save_type_traits<World>     { static constexpr uint64_t value = World::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Machine>   { static constexpr uint64_t value = Machine::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<HeapInt64> { static constexpr uint64_t value = HeapInt64::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<HeapString>{ static constexpr uint64_t value = HeapString::SAVE_TYPE_TAG; };

    template<> struct save_type_traits<PersistentStack<Value>> {
        static constexpr uint64_t value = save_type_tag_fnv1a("wry::PersistentStack<Value>");
    };

    // Leaf traits for primitive value types that appear as T in AMT Nodes.
    template<> struct save_type_traits<Value>           { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Value"); };
    template<> struct save_type_traits<EntityID>        { static constexpr uint64_t value = save_type_tag_fnv1a("wry::EntityID"); };
    template<> struct save_type_traits<const Entity*>   { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Entity*"); };
    template<> struct save_type_traits<int>             { static constexpr uint64_t value = save_type_tag_fnv1a("int"); };
    template<> struct save_type_traits<uint64_t>        { static constexpr uint64_t value = save_type_tag_fnv1a("u64"); };
    template<> struct save_type_traits<__uint128_t>     { static constexpr uint64_t value = save_type_tag_fnv1a("u128"); };

    // AMT Node specializations.  Each (T, U) pair gets a structural tag from
    // the leaf-type traits above.
    template<typename T, typename U>
    struct save_type_traits<array_mapped_trie::Node<T, U>> {
        static constexpr uint64_t value = save_type_tag_combine(
            save_type_tag_combine(
                save_type_tag_fnv1a("wry::array_mapped_trie::Node"),
                save_type_traits<T>::value,
                17),
            save_type_traits<U>::value,
            31);
    };

    // -----------------------------------------------------------------------
    // Polymorphic visitors.  Dispatch on dynamic type via virtual function.
    // -----------------------------------------------------------------------

    SaveRef Saver::visit_entity(const Entity* p) {
        if (!p) return SAVE_REF_NULL;
        auto it = _seen.find((const void*)p);
        if (it != _seen.end()) {
            if (it->second == SAVE_REF_NULL) {
                // In-progress; back-edge.  Sketch assumes DAG.
                record_back_edge(p);
                return SAVE_REF_NULL;
            }
            return it->second;
        }
        _seen[(const void*)p] = SAVE_REF_NULL;  // in-progress sentinel

        size_t len_off = begin_record(p->_save_type_tag());
        p->_save_body(*this);

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record(len_off);
        // The record header includes the assigned id implicitly by position
        // in the file.  The loader counts records as it reads.
        return id;
    }

    SaveRef Saver::visit_heap_value(const HeapValue* p) {
        if (!p) return SAVE_REF_NULL;
        auto it = _seen.find((const void*)p);
        if (it != _seen.end()) {
            if (it->second == SAVE_REF_NULL) {
                record_back_edge(p);
                return SAVE_REF_NULL;
            }
            return it->second;
        }
        _seen[(const void*)p] = SAVE_REF_NULL;

        size_t len_off = begin_record(p->_save_type_tag());
        p->_save_body(*this);

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record(len_off);
        return id;
    }

    // Templated visit() for non-polymorphic types.  Each instantiation needs
    // a corresponding emit_body() free function below.
    template<typename T>
    SaveRef Saver::visit(const T* p) {
        if (!p) return SAVE_REF_NULL;
        auto it = _seen.find((const void*)p);
        if (it != _seen.end()) {
            assert(it->second != SAVE_REF_NULL && "back-edge in non-polymorphic type unsupported in sketch");
            return it->second;
        }
        _seen[(const void*)p] = SAVE_REF_NULL;

        size_t len_off = begin_record(save_type_tag_v<T>);
        emit_body(p, *this);  // ADL or namespace lookup

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record(len_off);
        return id;
    }

    // -----------------------------------------------------------------------
    // emit_body free functions.  One per non-polymorphic concrete type.
    // -----------------------------------------------------------------------

    // World's _save_body is a method on the class (see further down,
    // grouped with the other polymorphic save bodies).  Forward decls
    // for AMT Node bodies are below.

    // PersistentStack<Value>
    static void emit_body(const PersistentStack<Value>* n, Saver& s) {
        SaveRef next_ref = s.visit<PersistentStack<Value>>(n->_next);
        uint64_t payload = encode_value(n->_payload, s);
        s.write_ref(next_ref);
        s.write_u64(payload);
    }

    // AMT Node body, generic over (T, U).  Caller supplies a lambda that
    // emits one leaf value (either as raw bytes or as a SaveRef after visiting
    // a referenced GC object).

    template<typename T, typename U, typename EmitLeaf>
    static void emit_amt_body(const array_mapped_trie::Node<T, U>* n, Saver& s, EmitLeaf&& emit_leaf) {
        using N = array_mapped_trie::Node<T, U>;
        int count = __builtin_popcountg(n->_bitmap);

        if (n->has_children()) {
            // Visit children first (post-order).
            std::vector<SaveRef> child_refs(count);
            for (int i = 0; i < count; ++i)
                child_refs[i] = s.visit<N>(n->_children[i]);
            s.write_pod(n->_prefix);
            s.write_u32((uint32_t)n->_shift);
            s.write_u32((uint32_t)n->_bitmap);
            s.write_u32((uint32_t)count);
            for (SaveRef r : child_refs)
                s.write_ref(r);
        } else {
            // For leaf values that may carry sub-references (Value, Entity*),
            // visit them first.  emit_leaf returns the bytes-or-ref to write
            // and may have triggered child record emissions as a side effect.
            using Encoded = decltype(emit_leaf(n->_values[0]));
            std::vector<Encoded> encoded(count);
            for (int i = 0; i < count; ++i)
                encoded[i] = emit_leaf(n->_values[i]);
            s.write_pod(n->_prefix);
            s.write_u32((uint32_t)n->_shift);
            s.write_u32((uint32_t)n->_bitmap);
            s.write_u32((uint32_t)count);
            for (const auto& e : encoded)
                s.write_pod(e);
        }
    }

    // AMT Node<Value, uint64_t>: leaf Values; OBJECT-tagged ones reference
    // HeapValues, which we visit and replace with SaveRefs inside the encoded
    // word.
    static void emit_body(const array_mapped_trie::Node<Value, uint64_t>* n, Saver& s) {
        emit_amt_body(n, s, [&s](const Value& v) { return encode_value(v, s); });
    }

    // AMT Node<EntityID, uint64_t>: leaf values are 64-bit ids, no references.
    static void emit_body(const array_mapped_trie::Node<EntityID, uint64_t>* n, Saver& s) {
        emit_amt_body(n, s, [](EntityID e) { return e.data; });
    }

    // AMT Node<const Entity*, uint64_t>: leaves are polymorphic Entity refs.
    static void emit_body(const array_mapped_trie::Node<const Entity*, uint64_t>* n, Saver& s) {
        emit_amt_body(n, s, [&s](const Entity* p) { return s.visit_entity(p); });
    }

    // AMT Node<int, __uint128_t>: PersistentSet of pair<...,EntityID> keys.
    static void emit_body(const array_mapped_trie::Node<int, __uint128_t>* n, Saver& s) {
        // Set-style leaves: int dummy payload carries no information.  Write
        // zero so encoded width is well defined; loader ignores it.
        emit_amt_body(n, s, [](int) { return (int32_t)0; });
    }

    // The set-flavor PersistentSet is keyed by pair<X, EntityID>, which the
    // DefaultKeyService hashes to a 16-byte (u128) hash, so the ki AMTs use
    // Node<int, __uint128_t>.  The kv side hashes Coordinate / EntityID to
    // u64.
    using NodeEntityID_U64    = array_mapped_trie::Node<EntityID, uint64_t>;
    using NodeEntityPtr_U64   = array_mapped_trie::Node<const Entity*, uint64_t>;
    using NodeValue_U64       = array_mapped_trie::Node<Value, uint64_t>;
    using NodeSet_U128        = array_mapped_trie::Node<int, __uint128_t>;

    // -----------------------------------------------------------------------
    // Polymorphic _save_body implementations.  These live here, not in the
    // class .cpp, to keep all save-format coupling localized to one file.
    // -----------------------------------------------------------------------

    void World::_save_body(Saver& s) const {
        SaveRef eid_for_coord_kv = s.visit<NodeEntityID_U64>(_entity_id_for_coordinate.kv._inner);
        SaveRef eid_for_coord_ki = s.visit<NodeSet_U128>(_entity_id_for_coordinate.ki._inner);
        SaveRef ent_for_eid_kv   = s.visit<NodeEntityPtr_U64>(_entity_for_entity_id.kv._inner);
        SaveRef ent_for_eid_ki   = s.visit<NodeSet_U128>(_entity_for_entity_id.ki._inner);
        SaveRef val_for_coord_kv = s.visit<NodeValue_U64>(_value_for_coordinate.kv._inner);
        SaveRef val_for_coord_ki = s.visit<NodeSet_U128>(_value_for_coordinate.ki._inner);
        SaveRef waiting_on_time  = s.visit<NodeSet_U128>(_waiting_on_time._inner);

        s.write_u64((uint64_t)_time);
        s.write_ref(eid_for_coord_kv);
        s.write_ref(eid_for_coord_ki);
        s.write_ref(ent_for_eid_kv);
        s.write_ref(ent_for_eid_ki);
        s.write_ref(val_for_coord_kv);
        s.write_ref(val_for_coord_ki);
        s.write_ref(waiting_on_time);
    }

    void Machine::_save_body(Saver& s) const {
        // Visit stack first (post-order).
        SaveRef stack_head_ref = s.visit<PersistentStack<Value>>(_stack);

        s.write_u64(_entity_id.data);
        s.write_u32((uint32_t)_phase);
        s.write_u64((uint64_t)_on_arrival);
        s.write_ref(stack_head_ref);
        s.write_u64((uint64_t)_old_heading);
        s.write_u64((uint64_t)_new_heading);
        s.write_pod(_old_location);
        s.write_pod(_new_location);
        s.write_u64((uint64_t)_old_time);
        s.write_u64((uint64_t)_new_time);
    }

    void HeapInt64::_save_body(Saver& s) const {
        s.write_u64((uint64_t)_integer);
    }

    void HeapString::_save_body(Saver& s) const {
        s.write_u64((uint64_t)_size);
        s.write_bytes(_bytes, _size);
    }

    // -----------------------------------------------------------------------
    // Saver driver.
    // -----------------------------------------------------------------------

    SaveRef Saver::save_world(const World* root) {
        // World inherits HeapValue post-review, so it joins the
        // polymorphic visit_heap_value path along with HeapInt64 /
        // HeapString / Entity / Machine.  The registry entry for World
        // (in g_saveable_traits) drives load.
        return visit_heap_value(root);
    }

    void Saver::resolve_pending() {
        // Sketch: cycle handling not yet driven; should be empty in DAG case.
        for (auto& p : _pending) {
            auto it = _seen.find(p.target);
            assert(it != _seen.end() && it->second != SAVE_REF_NULL);
            SaveRef r = it->second;
            std::memcpy(_stream.data() + p.offset, &r, sizeof(SaveRef));
        }
        _pending.clear();
    }

    // -----------------------------------------------------------------------
    // SaveableTraits registry.  load_into functions read a record body and
    // populate loader._ptrs[id].
    // -----------------------------------------------------------------------

    // World load
    static void load_into_world(Loader& L, SaveRef id) {
        World* w = new World;
        L._ptrs[id] = w;

        w->_time = (Time)L.read_u64();
        SaveRef eid_kv  = L.read_u32();
        SaveRef eid_ki  = L.read_u32();
        SaveRef ent_kv  = L.read_u32();
        SaveRef ent_ki  = L.read_u32();
        SaveRef val_kv  = L.read_u32();
        SaveRef val_ki  = L.read_u32();
        SaveRef wait    = L.read_u32();

        w->_entity_id_for_coordinate.kv._inner = (NodeEntityID_U64*)L._ptrs[eid_kv];
        w->_entity_id_for_coordinate.ki._inner = (NodeSet_U128*)L._ptrs[eid_ki];
        w->_entity_for_entity_id.kv._inner     = (NodeEntityPtr_U64*)L._ptrs[ent_kv];
        w->_entity_for_entity_id.ki._inner     = (NodeSet_U128*)L._ptrs[ent_ki];
        w->_value_for_coordinate.kv._inner     = (NodeValue_U64*)L._ptrs[val_kv];
        w->_value_for_coordinate.ki._inner     = (NodeSet_U128*)L._ptrs[val_ki];
        w->_waiting_on_time._inner             = (NodeSet_U128*)L._ptrs[wait];
    }

    static void load_into_machine(Loader& L, SaveRef id) {
        Machine* m = new Machine;
        L._ptrs[id] = m;
        m->_entity_id = EntityID{ L.read_u64() };
        m->_phase = (decltype(m->_phase))L.read_u32();
        m->_on_arrival = (int64_t)L.read_u64();
        SaveRef stack_ref = L.read_u32();
        m->_old_heading = (int64_t)L.read_u64();
        m->_new_heading = (int64_t)L.read_u64();
        m->_old_location = L.read_pod<Coordinate>();
        m->_new_location = L.read_pod<Coordinate>();
        m->_old_time = (Time)L.read_u64();
        m->_new_time = (Time)L.read_u64();
        m->_stack = (PersistentStack<Value>*)L._ptrs[stack_ref];
    }

    static void load_into_heap_int64(Loader& L, SaveRef id) {
        HeapInt64* h = new HeapInt64;
        L._ptrs[id] = h;
        h->_integer = (int64_t)L.read_u64();
    }

    static void load_into_heap_string(Loader& L, SaveRef id) {
        uint64_t size = L.read_u64();
        std::string_view view((const char*)L._cursor, (size_t)size);
        L._cursor += size;
        // Goes through interning factory; the result is what should be stored.
        L._ptrs[id] = (void*)HeapString::make(view);
    }

    static void load_into_persistent_stack_node(Loader& L, SaveRef id) {
        SaveRef next_ref = L.read_u32();
        uint64_t payload = L.read_u64();
        // Allocate via the GC operator new.  We can't use the existing
        // constructor (which forwards args); poke fields directly.
        auto* n = new PersistentStack<Value>(nullptr);
        L._ptrs[id] = n;
        n->_next = (PersistentStack<Value>*)L._ptrs[next_ref];
        n->_payload = decode_value(payload, L);
    }

    // AMT node loader template, generic over (T, U).  Allocates with the
    // right capacity via Node::make, then dispatches to fill_values for the
    // leaf case.
    template<typename T, typename U, typename FillValues>
    static void load_amt_node(Loader& L, SaveRef id, FillValues&& fill_values) {
        using N = array_mapped_trie::Node<T, U>;
        U prefix = L.read_pod<U>();
        uint32_t shift  = L.read_u32();
        uint32_t bitmap = L.read_u32();
        uint32_t count  = L.read_u32();

        N* n = N::make(prefix, (int)shift, count, count, bitmap);
        L._ptrs[id] = n;

        if (n->has_children()) {
            for (uint32_t i = 0; i < count; ++i) {
                SaveRef child_ref = L.read_u32();
                n->_children[i] = (const N*)L._ptrs[child_ref];
            }
        } else {
            fill_values(n, count);
        }
    }

    static void load_into_amt_node_value_u64(Loader& L, SaveRef id) {
        load_amt_node<Value, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t word = L.read_u64();
                n->_values[i] = decode_value(word, L);
            }
        });
    }

    static void load_into_amt_node_entity_id_u64(Loader& L, SaveRef id) {
        load_amt_node<EntityID, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i)
                n->_values[i] = EntityID{ L.read_u64() };
        });
    }

    static void load_into_amt_node_entity_ptr_u64(Loader& L, SaveRef id) {
        load_amt_node<const Entity*, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                SaveRef r = L.read_u32();
                n->_values[i] = (const Entity*)L._ptrs[r];
            }
        });
    }

    static void load_into_amt_node_int_u128(Loader& L, SaveRef id) {
        load_amt_node<int, __uint128_t>(L, id, [&L](auto* n, uint32_t count) {
            // The saver writes a 4-byte zero per leaf slot for set-style
            // nodes; consume them so framing stays consistent.
            for (uint32_t i = 0; i < count; ++i) {
                (void)L.read_u32();
                n->_values[i] = 0;
            }
        });
    }

    // The registry table.

    static const SaveableTraits g_saveable_traits[] = {
        { save_type_tag_v<World>,                                            "wry::World",                          &load_into_world },
        { save_type_tag_v<Machine>,                                          "wry::Machine",                        &load_into_machine },
        { save_type_tag_v<HeapInt64>,                                        "wry::HeapInt64",                      &load_into_heap_int64 },
        { save_type_tag_v<HeapString>,                                       "wry::HeapString",                     &load_into_heap_string },
        { save_type_tag_v<PersistentStack<Value>>,                     "wry::PersistentStack<Value>",   &load_into_persistent_stack_node },
        { save_type_tag_v<NodeValue_U64>,                                    "Node<Value,u64>",                     &load_into_amt_node_value_u64 },
        { save_type_tag_v<NodeEntityID_U64>,                                 "Node<EntityID,u64>",                  &load_into_amt_node_entity_id_u64 },
        { save_type_tag_v<NodeEntityPtr_U64>,                                "Node<Entity*,u64>",                   &load_into_amt_node_entity_ptr_u64 },
        { save_type_tag_v<NodeSet_U128>,                                     "Node<int,u128>",                      &load_into_amt_node_int_u128 },
    };

    const SaveableTraits* find_saveable_traits(uint64_t tag) {
        for (const auto& t : g_saveable_traits) {
            if (t.tag == tag) return &t;
        }
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // Loader driver.  File layout:
    //   [magic u32] [version u32] [record_count u32]
    //   N records, each: [tag varint] [body_len u32] [body bytes...]
    //   [root SaveRef u32]
    // -----------------------------------------------------------------------

    World* Loader::load_world() {
        uint32_t magic   = read_u32();
        uint32_t version = read_u32();
        uint32_t count   = read_u32();
        (void)magic;
        (void)version;

        _ptrs.assign(count + 1, nullptr);  // +1 because IDs are 1-based

        for (uint32_t id = 1; id <= count; ++id) {
            uint64_t tag = read_varint();
            uint32_t body_len = read_u32();
            const uint8_t* body_start = _cursor;
            const SaveableTraits* tr = find_saveable_traits(tag);
            if (!tr) {
                // Unknown type tag -- skip the record body and leave ptrs[id]
                // null.  A real loader would record this for diagnostics.
                fprintf(stderr, "save: unknown type tag %llx; skipping record %u\n",
                        (unsigned long long)tag, id);
                _cursor = body_start + body_len;
                continue;
            }
            tr->load_into(*this, id);
            // Sanity: the load_into should have consumed exactly body_len bytes.
            assert(_cursor == body_start + body_len);
        }

        SaveRef root_ref = read_u32();
        resolve_fixups();
        return (World*)_ptrs[root_ref];
    }

    void Loader::resolve_fixups() {
        for (auto& f : _fixups) {
            assert(f.target < _ptrs.size());
            *f.field = _ptrs[f.target];
        }
        _fixups.clear();
    }

}  // namespace wry
