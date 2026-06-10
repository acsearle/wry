//
//  save_format.cpp
//  client
//
//  Implementation of the save format sketch.  See save_format.hpp.
//
//  The worked type universe in this sketch:
//    - World (root, non-polymorphic)
//    - Machine (Entity subclass, polymorphic via Entity*)
//    - HeapInt64 (HeapTerm subclass, polymorphic via HeapTerm*)
//    - HeapString (HeapTerm subclass; SKETCH stubs, factory-based load)
//    - ArrayMappedTrie<uint64_t, Term>          // value-for-coordinate map leaves
//    - ArrayMappedTrie<uint64_t, EntityID>       // entity-id-for-coordinate map leaves
//    - ArrayMappedTrie<uint64_t, const Entity*>  // entity-for-entity-id map leaves
//    - ArrayMappedTrie<__uint128_t, int>            // time wheel set node
//    - ArrayMappedTrie<uint64_t, WaitSet>        // ki waiter-index outer map
//    - ArrayMappedTrie<uint64_t, int>            // ki waitset inner set node
//    - PersistentStack<Term>                      // machine stack cells
//

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "save_format.hpp"

#include "array_mapped_trie.hpp"
#include "entity.hpp"
#include "HeapString.hpp"
#include "machine.hpp"
#include "persistent_set.hpp"
#include "persistent_stack.hpp"
#include "term.hpp"
#include "test.hpp"
#include "waitable_map.hpp"
#include "world.hpp"

namespace wry {

    // -----------------------------------------------------------------------
    // Term encoding.  Values are 64-bit tagged words.  In the OBJECT case
    // the high bits are an in-memory HeapTerm pointer; on disk we replace
    // those bits with a SaveRef.  All other tags travel as-is.
    //
    // NOTE: this assumes no Term cycles.  A self-referential container would
    // require the fixup path (because the HeapTerm is not yet emitted when
    // we try to encode a Term pointing to it).  Cycle handling is sketched
    // in Saver::record_back_edge / Loader::Fixup but not driven here.
    // -----------------------------------------------------------------------

    static uint64_t encode_value(const Term& v, Saver& s) {
        if (_term_is_object(v)) {
            const HeapTerm* p = _term_as_object(v);
            SaveRef ref = s.visit_heap_value(p);
            return ((uint64_t)ref << TERM_SHIFT) | TERM_TAG_OBJECT;
        }
        return v._data;
    }

    static Term decode_value(uint64_t word, Loader& L) {
        Term v;
        if ((word & TERM_MASK_TAG) == TERM_TAG_OBJECT) {
            SaveRef ref = (SaveRef)(word >> TERM_SHIFT);
            // Sketch: forward refs from Term would need fixup against the
            // address of v._data.  Assume DAG for now.
            HeapTerm* p = (HeapTerm*)L._ptrs[ref];
            v._data = (uint64_t)p | TERM_TAG_OBJECT;
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

    template<> struct save_type_traits<PersistentStack<Term>> {
        static constexpr uint64_t value = save_type_tag_fnv1a("wry::PersistentStack<Term>");
    };

    // Leaf traits for primitive value types that appear as T in AMT Nodes.
    template<> struct save_type_traits<Term>           { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Term"); };
    template<> struct save_type_traits<EntityID>        { static constexpr uint64_t value = save_type_tag_fnv1a("wry::EntityID"); };
    template<> struct save_type_traits<const Entity*>   { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Entity*"); };
    template<> struct save_type_traits<WaitSet>         { static constexpr uint64_t value = save_type_tag_fnv1a("wry::WaitSet"); };
    template<> struct save_type_traits<int>             { static constexpr uint64_t value = save_type_tag_fnv1a("int"); };
    template<> struct save_type_traits<uint64_t>        { static constexpr uint64_t value = save_type_tag_fnv1a("u64"); };
    template<> struct save_type_traits<__uint128_t>     { static constexpr uint64_t value = save_type_tag_fnv1a("u128"); };

    // AMT Node specializations.  Each (T, U) pair gets a structural tag from
    // the leaf-type traits above.
    template<typename T, typename U>
    struct save_type_traits<ArrayMappedTrie<U, T, ScanDiscipline>> {
        static constexpr uint64_t value = save_type_tag_combine(
            save_type_tag_combine(
                save_type_tag_fnv1a("wry::ArrayMappedTrie"),
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

        begin_record(p->_save_type_tag());
        p->_save_body(*this);

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record();
        // The record header includes the assigned id implicitly by position
        // in the file.  The loader counts records as it reads.
        return id;
    }

    SaveRef Saver::visit_heap_value(const HeapTerm* p) {
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

        begin_record(p->_save_type_tag());
        p->_save_body(*this);

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record();
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

        begin_record(save_type_tag_v<T>);
        emit_body(p, *this);  // ADL or namespace lookup

        SaveRef id = _next_ref++;
        _seen[(const void*)p] = id;
        end_record();
        return id;
    }

    // -----------------------------------------------------------------------
    // emit_body free functions.  One per non-polymorphic concrete type.
    // -----------------------------------------------------------------------

    // World's _save_body is a method on the class (see further down,
    // grouped with the other polymorphic save bodies).  Forward decls
    // for AMT Node bodies are below.

    // PersistentStack<Term>
    static void emit_body(const PersistentStack<Term>* n, Saver& s) {
        SaveRef next_ref = s.visit<PersistentStack<Term>>(n->_next);
        uint64_t payload = encode_value(n->_payload, s);
        s.write_ref(next_ref);
        s.write_u64(payload);
    }

    // AMT Node body, generic over (T, U).  Caller supplies a lambda that
    // emits one leaf value (either as raw bytes or as a SaveRef after visiting
    // a referenced GC object).

    template<typename T, typename U, typename EmitLeaf>
    static void emit_amt_body(const ArrayMappedTrie<U, T, ScanDiscipline>* n, Saver& s, EmitLeaf&& emit_leaf) {
        using N = ArrayMappedTrie<U, T, ScanDiscipline>;
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
            // For leaf values that may carry sub-references (Term, Entity*),
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

    // AMT Node<Term, uint64_t>: leaf Values; OBJECT-tagged ones reference
    // HeapValues, which we visit and replace with SaveRefs inside the encoded
    // word.
    static void emit_body(const ArrayMappedTrie<uint64_t, Term, ScanDiscipline>* n, Saver& s) {
        emit_amt_body(n, s, [&s](const Term& v) { return encode_value(v, s); });
    }

    // AMT Node<EntityID, uint64_t>: leaf values are 64-bit ids, no references.
    static void emit_body(const ArrayMappedTrie<uint64_t, EntityID, ScanDiscipline>* n, Saver& s) {
        emit_amt_body(n, s, [](EntityID e) { return e.data; });
    }

    // AMT Node<const Entity*, uint64_t>: leaves are polymorphic Entity refs.
    static void emit_body(const ArrayMappedTrie<uint64_t, const Entity*, ScanDiscipline>* n, Saver& s) {
        emit_amt_body(n, s, [&s](const Entity* p) { return s.visit_entity(p); });
    }

    // AMT Node<int, __uint128_t>: PersistentSet of pair<...,EntityID> keys.
    static void emit_body(const ArrayMappedTrie<__uint128_t, int, ScanDiscipline>* n, Saver& s) {
        // Set-style leaves: int dummy payload carries no information.  Write
        // zero so encoded width is well defined; loader ignores it.
        emit_amt_body(n, s, [](int) { return (int32_t)0; });
    }

    // AMT Node<int, uint64_t>: the inner waitset of a ki entry; a
    // PersistentSet of EntityID codes.  Set-style, as above.
    static void emit_body(const ArrayMappedTrie<uint64_t, int, ScanDiscipline>* n, Saver& s) {
        emit_amt_body(n, s, [](int) { return (int32_t)0; });
    }

    // AMT Node<WaitSet, uint64_t>: the ki waiter-index outer map.  Leaves
    // are nested WaitSets, emitted as refs to their inner set root nodes.
    static void emit_body(const ArrayMappedTrie<uint64_t, WaitSet, ScanDiscipline>* n, Saver& s) {
        emit_amt_body(n, s, [&s](const WaitSet& ws) {
            return s.visit<ArrayMappedTrie<uint64_t, int, ScanDiscipline>>(ws._inner);
        });
    }

    // The kv side hashes Coordinate / EntityID keys to u64 codes.  The time
    // wheel (_waiting_on_time) is keyed by pair<Time, EntityID>, which the
    // DefaultKeyService packs into a u128 code, so its set nodes are
    // Node<int, u128>.  The ki waiter index is a nested map: an outer
    // Node<WaitSet, u64> whose leaves reference inner Node<int, u64> set
    // roots holding EntityID codes.
    using NodeEntityID_U64    = ArrayMappedTrie<uint64_t, EntityID, ScanDiscipline>;
    using NodeEntityPtr_U64   = ArrayMappedTrie<uint64_t, const Entity*, ScanDiscipline>;
    using NodeValue_U64       = ArrayMappedTrie<uint64_t, Term, ScanDiscipline>;
    using NodeSet_U128        = ArrayMappedTrie<__uint128_t, int, ScanDiscipline>;
    using NodeWaitSet_U64     = ArrayMappedTrie<uint64_t, WaitSet, ScanDiscipline>;
    using NodeSet_U64         = ArrayMappedTrie<uint64_t, int, ScanDiscipline>;

    // -----------------------------------------------------------------------
    // Polymorphic _save_body implementations.  These live here, not in the
    // class .cpp, to keep all save-format coupling localized to one file.
    // -----------------------------------------------------------------------

    void World::_save_body(Saver& s) const {
        SaveRef eid_for_coord_kv = s.visit<NodeEntityID_U64>(_entity_id_for_coordinate.kv._inner);
        SaveRef ent_for_eid_kv   = s.visit<NodeEntityPtr_U64>(_entity_for_entity_id.kv._inner);
        SaveRef val_for_coord_kv = s.visit<NodeValue_U64>(_term_for_coordinate.kv._inner);
        SaveRef waiting_on_time  = s.visit<NodeSet_U128>(_waiting_on_time._inner);

        // The ki waiter index is semantic state, not a regenerable cache: a
        // waiter registered before the save must still be registered after a
        // load, or its wake is silently lost.  Nested map: the outer
        // Node<WaitSet,u64> leaves reference inner Node<int,u64> set roots.
        SaveRef eid_for_coord_ki = s.visit<NodeWaitSet_U64>(_entity_id_for_coordinate.ki._inner);
        SaveRef ent_for_eid_ki   = s.visit<NodeWaitSet_U64>(_entity_for_entity_id.ki._inner);
        SaveRef val_for_coord_ki = s.visit<NodeWaitSet_U64>(_term_for_coordinate.ki._inner);

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
        SaveRef stack_head_ref = s.visit<PersistentStack<Term>>(_stack);

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
        // World inherits HeapTerm post-review, so it joins the
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
        w->_entity_for_entity_id.kv._inner     = (NodeEntityPtr_U64*)L._ptrs[ent_kv];
        w->_term_for_coordinate.kv._inner     = (NodeValue_U64*)L._ptrs[val_kv];
        w->_waiting_on_time._inner             = (NodeSet_U128*)L._ptrs[wait];

        // Pre-ki saves wrote SAVE_REF_NULL for these three refs; _ptrs[0] is
        // nullptr, so such files load with an empty waiter index.
        w->_entity_id_for_coordinate.ki._inner = (NodeWaitSet_U64*)L._ptrs[eid_ki];
        w->_entity_for_entity_id.ki._inner     = (NodeWaitSet_U64*)L._ptrs[ent_ki];
        w->_term_for_coordinate.ki._inner     = (NodeWaitSet_U64*)L._ptrs[val_ki];
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
        m->_stack = (PersistentStack<Term>*)L._ptrs[stack_ref];
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
        auto* n = new PersistentStack<Term>(nullptr);
        L._ptrs[id] = n;
        n->_next = (PersistentStack<Term>*)L._ptrs[next_ref];
        n->_payload = decode_value(payload, L);
    }

    // AMT node loader template, generic over (T, U).  Allocates with the
    // right capacity via Node::make, then dispatches to fill_values for the
    // leaf case.
    template<typename T, typename U, typename FillValues>
    static void load_amt_node(Loader& L, SaveRef id, FillValues&& fill_values) {
        using N = ArrayMappedTrie<U, T, ScanDiscipline>;
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
        load_amt_node<Term, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
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

    static void load_into_amt_node_int_u64(Loader& L, SaveRef id) {
        load_amt_node<int, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                (void)L.read_u32();
                n->_values[i] = 0;
            }
        });
    }

    static void load_into_amt_node_wait_set_u64(Loader& L, SaveRef id) {
        load_amt_node<WaitSet, uint64_t>(L, id, [&L](auto* n, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                SaveRef r = L.read_u32();
                n->_values[i] = WaitSet{ (const NodeSet_U64*)L._ptrs[r] };
            }
        });
    }

    // The registry table.

    static const SaveableTraits g_saveable_traits[] = {
        { save_type_tag_v<World>,                                            "wry::World",                          &load_into_world },
        { save_type_tag_v<Machine>,                                          "wry::Machine",                        &load_into_machine },
        { save_type_tag_v<HeapInt64>,                                        "wry::HeapInt64",                      &load_into_heap_int64 },
        { save_type_tag_v<HeapString>,                                       "wry::HeapString",                     &load_into_heap_string },
        { save_type_tag_v<PersistentStack<Term>>,                     "wry::PersistentStack<Term>",   &load_into_persistent_stack_node },
        { save_type_tag_v<NodeValue_U64>,                                    "Node<Term,u64>",                     &load_into_amt_node_value_u64 },
        { save_type_tag_v<NodeEntityID_U64>,                                 "Node<EntityID,u64>",                  &load_into_amt_node_entity_id_u64 },
        { save_type_tag_v<NodeEntityPtr_U64>,                                "Node<Entity*,u64>",                   &load_into_amt_node_entity_ptr_u64 },
        { save_type_tag_v<NodeSet_U128>,                                     "Node<int,u128>",                      &load_into_amt_node_int_u128 },
        { save_type_tag_v<NodeWaitSet_U64>,                                  "Node<WaitSet,u64>",                   &load_into_amt_node_wait_set_u64 },
        { save_type_tag_v<NodeSet_U64>,                                      "Node<int,u64>",                       &load_into_amt_node_int_u64 },
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

    // -----------------------------------------------------------------------
    // Round-trip test for the ki waiter index.  In-memory; the framing
    // (header, records, root ref) mirrors save_game/load_game in save.cpp.
    // -----------------------------------------------------------------------

    define_test("save_format_ki_roundtrip") {

        World* w = new World;
        w->_time = Time{77};

        // Random waiters on the EntityID-keyed map, against an oracle.  The
        // key domain is small enough to collide (multi-entity waitsets) and
        // the entity values spread enough to force multi-level inner sets.
        std::map<uint64_t, std::set<uint64_t>> oracle;
        for (int i = 0; i != 200; ++i) {
            uint64_t k = 1 + std::rand() % 64;
            uint64_t e = 1 + (uint64_t)std::rand();
            WaitSet ws;
            (void) w->_entity_for_entity_id.ki.try_get(EntityID{k}, ws);
            ws.set(EntityID{e});
            w->_entity_for_entity_id.ki.set(EntityID{k}, ws);
            oracle[k].insert(e);
        }

        // Coordinate-keyed waiters on the other two maps, plus kv and time
        // wheel entries so the ki records interleave with the other types.
        {
            WaitSet ws;
            ws.set(EntityID{101});
            ws.set(EntityID{202});
            w->_term_for_coordinate.ki.set(Coordinate{1, -2}, ws);
        }
        {
            WaitSet ws;
            ws.set(EntityID{303});
            w->_entity_id_for_coordinate.ki.set(Coordinate{-3, 4}, ws);
        }
        w->_term_for_coordinate.set(Coordinate{1, -2}, term_make_integer_with(7));
        w->_waiting_on_time.set({Time{5}, EntityID{101}});

        // Save.
        Saver s;
        s.write_u32(0x57525953);  // 'WRYS', as save_game writes
        s.write_u32(1);
        size_t record_count_offset = s._stream.size();
        s.write_u32(0);  // placeholder
        SaveRef root_ref = s.save_world(w);
        s.resolve_pending();
        uint32_t record_count = s._next_ref - 1;
        std::memcpy(s._stream.data() + record_count_offset, &record_count, sizeof(uint32_t));
        s.write_ref(root_ref);

        // Load.
        Loader L;
        L._cursor = s._stream.data();
        L._end    = s._stream.data() + s._stream.size();
        World* w2 = L.load_world();
        assert(w2);
        assert(w2->_time == w->_time);

        auto ki_as_set = [](const auto& ki, auto key) {
            std::set<uint64_t> out;
            WaitSet ws;
            if (ki.try_get(key, ws))
                ws.for_each([&out](EntityID e) { out.insert(e.data); });
            return out;
        };

        for (uint64_t k = 1; k != 65; ++k) {
            std::set<uint64_t> expected;
            if (auto it = oracle.find(k); it != oracle.end())
                expected = it->second;
            assert(ki_as_set(w2->_entity_for_entity_id.ki, EntityID{k}) == expected);
        }
        assert((ki_as_set(w2->_term_for_coordinate.ki, Coordinate{1, -2})
                == std::set<uint64_t>{101, 202}));
        assert((ki_as_set(w2->_entity_id_for_coordinate.ki, Coordinate{-3, 4})
                == std::set<uint64_t>{303}));
        assert(ki_as_set(w2->_term_for_coordinate.ki, Coordinate{9, 9}).empty());

        Term t;
        bool has = w2->_term_for_coordinate.try_get(Coordinate{1, -2}, t);
        assert(has);
        assert(t._data == term_make_integer_with(7)._data);
        assert(w2->_waiting_on_time.contains({Time{5}, EntityID{101}}));

        co_return;
    };

}  // namespace wry
