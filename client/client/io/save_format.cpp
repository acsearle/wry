//
//  save_format.cpp
//  client
//
//  Implementation of the save format sketch.  See save_format.hpp.
//
//  The worked type universe in this sketch:
//    - World (root, non-polymorphic)
//    - Machine (Entity subclass, polymorphic via Entity*)
//    - Spawner / Source / Sink / Counter / Evenator (LocalizedEntity subclasses)
//    - Player (Entity subclass; persists identity only)
//    - HeapInt64 (HeapTerm subclass, polymorphic via HeapTerm*)
//    - HeapString (HeapTerm subclass; SKETCH stubs, factory-based load)
//    - ArrayMappedTrie<uint64_t, Term>          // value-for-coordinate map entries
//    - ArrayMappedTrie<uint64_t, EntityID>       // entity-id-for-coordinate map entries
//    - ArrayMappedTrie<uint64_t, const Entity*>  // entity-for-entity-id map entries
//    - ArrayMappedTrie<uint64_t, Terrain>        // terrain-for-coordinate map entries
//    - ArrayMappedTrie<__uint128_t, std::monostate>  // time wheel set entries
//    - ArrayMappedTrie<uint64_t, WaitSet>        // ki waiter indexes and the location multimap; sets inline
//    - PersistentStack<Term>                      // machine stack cells
//

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

#include "save_format.hpp"

#include "array_mapped_trie.hpp"
#include "assert.hpp"
#include "entity.hpp"
#include "epoch.hpp"
#include "fields.hpp"
#include "HeapString.hpp"
#include "machine.hpp"
#include "persistent_set.hpp"
#include "persistent_stack.hpp"
#include "player.hpp"
#include "spawner.hpp"
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
    template<> struct save_type_traits<Spawner>   { static constexpr uint64_t value = Spawner::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Source>    { static constexpr uint64_t value = Source::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Sink>      { static constexpr uint64_t value = Sink::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Counter>   { static constexpr uint64_t value = Counter::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Evenator>  { static constexpr uint64_t value = Evenator::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<Player>    { static constexpr uint64_t value = Player::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<HeapInt64> { static constexpr uint64_t value = HeapInt64::SAVE_TYPE_TAG; };
    template<> struct save_type_traits<HeapString>{ static constexpr uint64_t value = HeapString::SAVE_TYPE_TAG; };

    template<> struct save_type_traits<PersistentStack<Term>> {
        static constexpr uint64_t value = save_type_tag_fnv1a("wry::PersistentStack<Term>");
    };

    // Leaf traits for primitive value types that appear as T in AMT Nodes.
    template<> struct save_type_traits<Term>           { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Term"); };
    template<> struct save_type_traits<EntityID>        { static constexpr uint64_t value = save_type_tag_fnv1a("wry::EntityID"); };
    template<> struct save_type_traits<const Entity*>   { static constexpr uint64_t value = save_type_tag_fnv1a("wry::Entity*"); };
    // WaitSet and EntityIDSet are one type; the tag string is a persisted
    // identifier and keeps the original name.
    template<> struct save_type_traits<WaitSet>         { static constexpr uint64_t value = save_type_tag_fnv1a("wry::WaitSet"); };
    template<> struct save_type_traits<int>             { static constexpr uint64_t value = save_type_tag_fnv1a("int"); };
    template<> struct save_type_traits<std::monostate>   { static constexpr uint64_t value = save_type_tag_fnv1a("unit"); };
    template<> struct save_type_traits<uint64_t>        { static constexpr uint64_t value = save_type_tag_fnv1a("u64"); };
    template<> struct save_type_traits<__uint128_t>     { static constexpr uint64_t value = save_type_tag_fnv1a("u128"); };

    // Entry records, one per trie.  Each (T, U) pair gets a tag from the
    // leaf-type traits above.
    template<typename T, typename U>
    struct save_type_traits<ArrayMappedTrie<U, T, ScanDiscipline>> {
        static constexpr uint64_t value = save_type_tag_combine(
            save_type_tag_combine(
                save_type_tag_fnv1a("wry::Entries"),
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

    // Map and set bodies: the whole trie as (code, value) entries in code
    // order, nothing of its shape.  Each value's encoding is written in
    // place, so entries may be variable-length (the ki waitsets).  Values
    // that reference other GC objects (Entity*, OBJECT Terms) are visited as
    // they are met, so their records precede the map's, post-order as
    // before.  The loader rebuilds the canonical trie in one pass with
    // SortedBuilder; the bytes are a function of content alone.

    template<typename T, typename U, typename EmitValue>
    static void emit_entries(const ArrayMappedTrie<U, T, ScanDiscipline>* root, Saver& s, EmitValue&& emit_value) {
        root->for_each([&s, &emit_value](U code, T value) {
            s.write_pod(code);
            emit_value(value);
        });
    }

    // Entries<Term,u64>: OBJECT-tagged Terms reference HeapTerms, visited
    // and replaced with SaveRefs inside the encoded word.
    static void emit_body(const ArrayMappedTrie<uint64_t, Term, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [&s](const Term& v) { s.write_u64(encode_value(v, s)); });
    }

    // Entries<EntityID,u64>: 64-bit ids, no references.
    static void emit_body(const ArrayMappedTrie<uint64_t, EntityID, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [&s](EntityID e) { s.write_u64(e.data); });
    }

    // Entries<Entity*,u64>: polymorphic Entity refs.
    static void emit_body(const ArrayMappedTrie<uint64_t, const Entity*, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [&s](const Entity* p) { s.write_ref(s.visit_entity(p)); });
    }

    // Entries<unit,u128>: the time wheel; codes only.
    static void emit_body(const ArrayMappedTrie<__uint128_t, std::monostate, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [](std::monostate) {});
    }

    // Entries<WaitSet,u64>: the ki waiter indexes and the location multimap.
    // Each waitset travels inline as [count u32][member codes u64 ...] in
    // code order rather than as a record of its own.
    static void emit_body(const ArrayMappedTrie<uint64_t, WaitSet, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [&s](const WaitSet& ws) {
            uint32_t count = 0;
            ws.for_each([&count](EntityID) { ++count; });
            s.write_u32(count);
            ws.for_each([&s](EntityID e) { s.write_u64(e.data); });
        });
    }

    // Entries<int,u64>: Terrain is an alias of int; small ints, no
    // references.  (The tag comes from save_type_traits<int>.)
    static void emit_body(const ArrayMappedTrie<uint64_t, Terrain, ScanDiscipline>* n, Saver& s) {
        emit_entries(n, s, [&s](Terrain t) { s.write_pod((int32_t)t); });
    }

    // The kv side encodes Coordinate / EntityID keys to u64 codes.  The time
    // wheel (_waiting_on_time) is keyed by pair<Time, EntityID>, which the
    // DefaultKeyService packs into a u128 code.  The ki waiter index is a
    // nested map, Node<WaitSet, u64>, whose waitsets are inner Node<unit,
    // u64> sets of EntityID codes; on disk they travel inline.
    using NodeEntityID_U64    = ArrayMappedTrie<uint64_t, EntityID, ScanDiscipline>;
    using NodeEntityPtr_U64   = ArrayMappedTrie<uint64_t, const Entity*, ScanDiscipline>;
    using NodeValue_U64       = ArrayMappedTrie<uint64_t, Term, ScanDiscipline>;
    using NodeTerrain_U64     = ArrayMappedTrie<uint64_t, Terrain, ScanDiscipline>;
    using NodeSet_U128        = ArrayMappedTrie<__uint128_t, std::monostate, ScanDiscipline>;
    using NodeWaitSet_U64     = ArrayMappedTrie<uint64_t, WaitSet, ScanDiscipline>;
    using NodeSet_U64         = ArrayMappedTrie<uint64_t, std::monostate, ScanDiscipline>;

    // -----------------------------------------------------------------------
    // The save body of a record-like object: its _fields enumeration
    // (core/fields.hpp) walked by this visitor.  State fields are
    // written in enumeration order, each in its own encoding;
    // a field that references another object visits it first, so the
    // referent's record precedes this one (post-order, as before).  The
    // loaders below read the same fields in the same order by hand.
    // -----------------------------------------------------------------------

    struct SaveFieldVisitor : FieldVisitor<SaveFieldVisitor> {

        Saver& s;

        explicit SaveFieldVisitor(Saver& saver) : s(saver) {}

        template<FieldRole R, typename T>
        void visit(const char*, T const& x) {
            if constexpr (R == FieldRole::state)
                write(x);
        }

        // Scalars: enumerators as u32, every other integer as u64.
        template<typename T> requires std::is_enum_v<T>
        void write(T x) { s.write_u32((uint32_t)x); }
        template<std::integral T>
        void write(T x) { s.write_u64((uint64_t)x); }
        void write(EntityID x) { s.write_u64(x.data); }
        void write(Coordinate x) { s.write_pod(x); }
        void write(std::string_view x) {
            s.write_u64((uint64_t)x.size());
            s.write_bytes(x.data(), x.size());
        }

        // References.
        void write(const Term& x) { s.write_u64(encode_value(x, s)); }
        void write(const Entity* x) { s.write_ref(s.visit_entity(x)); }
        void write(const PersistentStack<Term>* x) { s.write_ref(s.visit<PersistentStack<Term>>(x)); }
        template<typename Key, typename T, typename H, typename D>
        void write(const PersistentMap<Key, T, H, D>& x) {
            using AMT = typename PersistentMap<Key, T, H, D>::AMT;
            s.write_ref(s.visit<AMT>(x._inner ? &*x._inner : nullptr));
        }
        template<typename Key, typename T>
        void write(const WaitableMap<Key, T>& x) {
            write(x.kv);
            write(x.ki);
        }

        // The World's schedule: the time wheel with the ready set folded
        // in as the current-time bucket (world.hpp).
        void write(const World::TimeWheelFold& x) {
            World::Set t{x.wheel};
            for (auto [entity_id, _] : x.ready)
                t.set({x.time, entity_id});
            s.write_ref(s.visit<NodeSet_U128>(t._inner));
        }

    };

    template<typename T>
    static void save_fields(const T* p, Saver& s) {
        SaveFieldVisitor v{s};
        p->_fields(v);
    }

    void World::_save_body(Saver& s) const      { save_fields(this, s); }
    void Machine::_save_body(Saver& s) const    { save_fields(this, s); }
    void Spawner::_save_body(Saver& s) const    { save_fields(this, s); }
    void Sink::_save_body(Saver& s) const       { save_fields(this, s); }
    void Counter::_save_body(Saver& s) const    { save_fields(this, s); }
    void Evenator::_save_body(Saver& s) const   { save_fields(this, s); }
    void Source::_save_body(Saver& s) const     { save_fields(this, s); }
    void Player::_save_body(Saver& s) const     { save_fields(this, s); }
    void HeapInt64::_save_body(Saver& s) const  { save_fields(this, s); }
    void HeapString::_save_body(Saver& s) const { save_fields(this, s); }

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

    static EntityID read_entity_id(Loader& L) {
        EntityID id{ L.read_u64() };
        return id;
    }

    // World load
    static void load_into_world(Loader& L, SaveRef id) {
        World* w = new World;
        L._ptrs[id] = w;

        w->_time = (Time)L.read_u64();
        w->_entity_id_source = read_entity_id(L);
        SaveRef eid_kv  = L.read_u32();
        SaveRef eid_ki  = L.read_u32();
        SaveRef loc_kv  = L.read_u32();
        SaveRef loc_ki  = L.read_u32();
        SaveRef ent_kv  = L.read_u32();
        SaveRef ent_ki  = L.read_u32();
        SaveRef val_kv  = L.read_u32();
        SaveRef val_ki  = L.read_u32();
        SaveRef ter_kv  = L.read_u32();
        SaveRef ter_ki  = L.read_u32();
        SaveRef wait    = L.read_u32();
        SaveRef players = L.read_u32();

        w->_entity_id_for_coordinate.kv._inner = (NodeEntityID_U64*)L._ptrs[eid_kv];
        w->_player_for_client._inner           = (NodeEntityID_U64*)L._ptrs[players];
        w->_located_for_coordinate.kv._inner   = (NodeWaitSet_U64*)L._ptrs[loc_kv];
        w->_entity_for_entity_id.kv._inner     = (NodeEntityPtr_U64*)L._ptrs[ent_kv];
        w->_term_for_coordinate.kv._inner     = (NodeValue_U64*)L._ptrs[val_kv];
        w->_terrain_for_coordinate.kv._inner   = (NodeTerrain_U64*)L._ptrs[ter_kv];
        w->_waiting_on_time._inner             = (NodeSet_U128*)L._ptrs[wait];

        // Pre-ki saves wrote SAVE_REF_NULL for these three refs; _ptrs[0] is
        // nullptr, so such files load with an empty waiter index.
        w->_entity_id_for_coordinate.ki._inner = (NodeWaitSet_U64*)L._ptrs[eid_ki];
        w->_located_for_coordinate.ki._inner   = (NodeWaitSet_U64*)L._ptrs[loc_ki];
        w->_entity_for_entity_id.ki._inner     = (NodeWaitSet_U64*)L._ptrs[ent_ki];
        w->_term_for_coordinate.ki._inner     = (NodeWaitSet_U64*)L._ptrs[val_ki];
        w->_terrain_for_coordinate.ki._inner   = (NodeWaitSet_U64*)L._ptrs[ter_ki];
    }

    static void load_into_machine(Loader& L, SaveRef id) {
        Machine* m = new Machine;
        L._ptrs[id] = m;
        m->_entity_id = read_entity_id(L);
        m->_free_entity_id = read_entity_id(L);
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
        m->seal();
    }

    template<typename T>
    static void load_into_localized_entity(Loader& L, SaveRef id) {
        T* p = new T;
        L._ptrs[id] = p;
        p->_entity_id = read_entity_id(L);
        p->_free_entity_id = read_entity_id(L);
        p->_location = L.read_pod<Coordinate>();
        p->seal();
    }

    static void load_into_source(Loader& L, SaveRef id) {
        Source* p = new Source;
        L._ptrs[id] = p;
        p->_entity_id = read_entity_id(L);
        p->_free_entity_id = read_entity_id(L);
        p->_location = L.read_pod<Coordinate>();
        p->_of_this = decode_value(L.read_u64(), L);
        p->seal();
    }

    static void load_into_player(Loader& L, SaveRef id) {
        Player* p = new Player;
        L._ptrs[id] = p;
        p->_entity_id = read_entity_id(L);
        p->_free_entity_id = read_entity_id(L);
        p->seal();
    }

    static void load_into_heap_int64(Loader& L, SaveRef id) {
        L._ptrs[id] = new HeapInt64((int64_t)L.read_u64());
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
        // Post-order: the cell below is already loaded, so the constructor
        // can take it and compute this cell's cached hash from it.
        auto* next = (const PersistentStack<Term>*)L._ptrs[next_ref];
        L._ptrs[id] = new PersistentStack<Term>(next, decode_value(payload, L));
    }

    // Entry loaders, generic over (T, U): read (code, value) entries up to
    // the end of the record body and rebuild the trie in one pass.  The
    // saver wrote the entries in code order, which is SortedBuilder's
    // precondition (asserted in Debug).
    template<typename T, typename U, typename DecodeValue>
    static void load_entries(Loader& L, SaveRef id, DecodeValue&& decode_value) {
        using N = ArrayMappedTrie<U, T, ScanDiscipline>;
        typename N::SortedBuilder builder;
        while (L._cursor < L._body_end) {
            U code = L.read_pod<U>();
            if constexpr (std::is_empty_v<T>)
                builder.push(code);
            else
                builder.push(code, decode_value());
        }
        L._ptrs[id] = (void*)builder.finish();
    }

    static void load_into_entries_value_u64(Loader& L, SaveRef id) {
        load_entries<Term, uint64_t>(L, id, [&L] { return decode_value(L.read_u64(), L); });
    }

    static void load_into_entries_entity_id_u64(Loader& L, SaveRef id) {
        load_entries<EntityID, uint64_t>(L, id, [&L] { return EntityID{ L.read_u64() }; });
    }

    static void load_into_entries_entity_ptr_u64(Loader& L, SaveRef id) {
        load_entries<const Entity*, uint64_t>(L, id, [&L] { return (const Entity*)L._ptrs[L.read_u32()]; });
    }

    static void load_into_entries_set_u128(Loader& L, SaveRef id) {
        load_entries<std::monostate, __uint128_t>(L, id, [] {});
    }

    static void load_into_entries_wait_set_u64(Loader& L, SaveRef id) {
        load_entries<WaitSet, uint64_t>(L, id, [&L] {
            uint32_t count = L.read_u32();
            NodeSet_U64::SortedBuilder inner;
            for (uint32_t i = 0; i != count; ++i)
                inner.push(L.read_u64());
            return WaitSet{ inner.finish() };
        });
    }

    static void load_into_entries_terrain_u64(Loader& L, SaveRef id) {
        load_entries<Terrain, uint64_t>(L, id, [&L] { return (Terrain)L.read_pod<int32_t>(); });
    }

    // The registry table.

    static constexpr SaveableTraits g_saveable_traits[] = {
        { save_type_tag_v<World>,                                            "wry::World",                          &load_into_world },
        { save_type_tag_v<Machine>,                                          "wry::Machine",                        &load_into_machine },
        { save_type_tag_v<Spawner>,                                          "wry::Spawner",                        &load_into_localized_entity<Spawner> },
        { save_type_tag_v<Source>,                                           "wry::Source",                         &load_into_source },
        { save_type_tag_v<Sink>,                                             "wry::Sink",                           &load_into_localized_entity<Sink> },
        { save_type_tag_v<Counter>,                                          "wry::Counter",                        &load_into_localized_entity<Counter> },
        { save_type_tag_v<Evenator>,                                         "wry::Evenator",                       &load_into_localized_entity<Evenator> },
        { save_type_tag_v<Player>,                                           "wry::Player",                         &load_into_player },
        { save_type_tag_v<HeapInt64>,                                        "wry::HeapInt64",                      &load_into_heap_int64 },
        { save_type_tag_v<HeapString>,                                       "wry::HeapString",                     &load_into_heap_string },
        { save_type_tag_v<PersistentStack<Term>>,                     "wry::PersistentStack<Term>",   &load_into_persistent_stack_node },
        { save_type_tag_v<NodeValue_U64>,                                    "Entries<Term,u64>",                   &load_into_entries_value_u64 },
        { save_type_tag_v<NodeEntityID_U64>,                                 "Entries<EntityID,u64>",               &load_into_entries_entity_id_u64 },
        { save_type_tag_v<NodeEntityPtr_U64>,                                "Entries<Entity*,u64>",                &load_into_entries_entity_ptr_u64 },
        { save_type_tag_v<NodeSet_U128>,                                     "Entries<unit,u128>",                  &load_into_entries_set_u128 },
        { save_type_tag_v<NodeWaitSet_U64>,                                  "Entries<WaitSet,u64>",                &load_into_entries_wait_set_u64 },
        { save_type_tag_v<NodeTerrain_U64>,                                  "Entries<int,u64>",                    &load_into_entries_terrain_u64 },
    };

    // Tags are 64-bit hashes of type names.  A collision between two
    // registered types would dispatch one type's records to the other's
    // loader: an assert in Debug, a silently misparsed file in Release.
    // The tags are constants, so refuse to build such a registry instead.
    static constexpr bool save_type_tags_are_distinct() {
        size_t n = std::size(g_saveable_traits);
        for (size_t i = 0; i != n; ++i)
            for (size_t j = i + 1; j != n; ++j)
                if (g_saveable_traits[i].tag == g_saveable_traits[j].tag)
                    return false;
        return true;
    }
    static_assert(save_type_tags_are_distinct(), "save type tag collision in g_saveable_traits");

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
        // Hard reject on version mismatch, per the TERM_SAVE_VERSION
        // contract: an older record layout would misparse, not degrade.
        if (version != TERM_SAVE_VERSION) {
            fprintf(stderr, "save: version %u (loader expects %u); refusing to load\n",
                    version, (unsigned)TERM_SAVE_VERSION);
            return nullptr;
        }

        _ptrs.assign(count + 1, nullptr);  // +1 because IDs are 1-based

        for (uint32_t id = 1; id <= count; ++id) {
            uint64_t tag = read_varint();
            uint32_t body_len = read_u32();
            const uint8_t* body_start = _cursor;
            _body_end = body_start + body_len;
            assert(_body_end <= _end);
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
    // The content hash oracle (see save_format.hpp).
    // -----------------------------------------------------------------------

    namespace {

        template<typename N, typename ValueHash>
        uint64_t oracle_trie_hash(const N* root, ValueHash&& value_hash) {
            uint64_t sum = 0;
            if (root)
                root->for_each([&sum, &value_hash](auto code, auto const& value) {
                    sum += entry_hash(code, value_hash(value));
                });
            return sum;
        }

        uint64_t oracle_stack_hash(const PersistentStack<Term>* top) {
            if (!top)
                return 0;
            uint64_t h = hash_combine(oracle_stack_hash(top->_next), content_hash(top->_payload));
            assert(h == top->_hash);
            return h;
        }

        uint64_t oracle_term_hash(Term t) {
            if (_term_is_object(t) && t._data) {
                uint64_t h = _term_as_object(t)->_compute_content_hash();
                assert(h == content_hash(t)); // the sealed hash
                return h;
            }
            return content_hash(t);
        }

        uint64_t oracle_entity_hash(const Entity* e) {
            if (!e)
                return 0;
            if (const Machine* m = dynamic_cast<const Machine*>(e))
                (void) oracle_stack_hash(m->_stack);
            if (const Source* s = dynamic_cast<const Source*>(e))
                (void) oracle_term_hash(s->_of_this);
            uint64_t h = e->_compute_content_hash();
            assert(h == e->content_hash());
            return h;
        }

        uint64_t oracle_waitset_hash(const WaitSet& ws) {
            uint64_t h = oracle_trie_hash(ws._inner ? &*ws._inner : nullptr,
                                          [](std::monostate) { return (uint64_t)0; });
            assert(h == ws.hash());
            return h;
        }

        template<typename M, typename ValueHash>
        uint64_t oracle_map_hash(const M& m, ValueHash&& value_hash) {
            uint64_t h = oracle_trie_hash(m._inner ? &*m._inner : nullptr, value_hash);
            assert(h == m.hash());
            return h;
        }

    } // namespace

    uint64_t world_content_hash_oracle(const World* w) {
        // Verify every cached part from a walk ...
        auto ki = [](const WaitSet& ws) { return oracle_waitset_hash(ws); };
        (void) oracle_map_hash(w->_entity_id_for_coordinate.kv, [](EntityID e) { return content_hash(e); });
        (void) oracle_map_hash(w->_entity_id_for_coordinate.ki, ki);
        (void) oracle_map_hash(w->_located_for_coordinate.kv, ki);
        (void) oracle_map_hash(w->_located_for_coordinate.ki, ki);
        (void) oracle_map_hash(w->_entity_for_entity_id.kv, oracle_entity_hash);
        (void) oracle_map_hash(w->_entity_for_entity_id.ki, ki);
        (void) oracle_map_hash(w->_term_for_coordinate.kv, oracle_term_hash);
        (void) oracle_map_hash(w->_term_for_coordinate.ki, ki);
        (void) oracle_map_hash(w->_terrain_for_coordinate.kv, [](Terrain t) { return content_hash(t); });
        (void) oracle_map_hash(w->_terrain_for_coordinate.ki, ki);
        uint64_t wheel = oracle_trie_hash(w->_waiting_on_time._inner ? &*w->_waiting_on_time._inner : nullptr,
                                          [](std::monostate) { return (uint64_t)0; });
        assert(wheel == w->_waiting_on_time.hash());
        // ... then the World from the verified parts, bypassing its seal.
        return w->_compute_content_hash();
    }

    // -----------------------------------------------------------------------
    // Round-trip tests.  In-memory; the framing (header, records, root ref)
    // mirrors save_game/load_game in save.cpp.
    // -----------------------------------------------------------------------

    static std::vector<uint8_t> test_save_to_buffer(const World* w) {
        Saver s;
        s.write_u32(0x57525953);  // 'WRYS', as save_game writes
        s.write_u32(TERM_SAVE_VERSION);
        size_t record_count_offset = s._stream.size();
        s.write_u32(0);  // placeholder
        SaveRef root_ref = s.save_world(w);
        s.resolve_pending();
        uint32_t record_count = s._next_ref - 1;
        std::memcpy(s._stream.data() + record_count_offset, &record_count, sizeof(uint32_t));
        s.write_ref(root_ref);
        return std::move(s._stream);
    }

    static World* test_load_from_buffer(const std::vector<uint8_t>& buffer) {
        Loader L;
        L._cursor = buffer.data();
        L._end    = buffer.data() + buffer.size();
        return L.load_world();
    }

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
        // Strictly future relative to _time == 77: the world invariant
        // (nothing waiting on the past or present outside _ready) now
        // holds at construction, so hack_repair_invariant is a no-op.
        w->_waiting_on_time.set({Time{78}, EntityID{101}});
        w->hack_repair_invariant();

        std::vector<uint8_t> buffer = test_save_to_buffer(w);
        World* w2 = test_load_from_buffer(buffer);
        assert(w2);
        assert(w2->_time == w->_time);
        w2->hack_repair_invariant();
        assert(w2->content_hash() == w->content_hash());
        assert(world_content_hash_oracle(w2) == w->content_hash());

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
        assert(w2->_waiting_on_time.contains({Time{78}, EntityID{101}}));

        co_return;
    };

    // The ready-set and deterministic-EntityID round-trip.
    //
    // Step a world until its ready set is live and it has spawned, save
    // (which folds _ready into the emitted time wheel as the current-time
    // bucket, and carries the EntityID cursor and any held tickets), load,
    // repair (which unfolds the bucket back out), then step BOTH lineages
    // onward and require identical behavior -- byte-equality of re-saves,
    // the strongest oracle available, valid because stepping is a pure
    // function of the World: EntityIDs are minted from the World's own
    // cursor by rank in the ready set, never from process state, so the
    // two lineages must agree on every ID they assign.
    //
    // The population: a Counter (re-arms every tick, so every boundary has
    // a nonempty _ready) and Spawners.  A Spawner needs two ticks per
    // machine -- one to request a ticket, one to spend it -- and spawns
    // only while its cell is unoccupied; the spawned machine, on an empty
    // value plane, parks in place, so each Spawner yields exactly one
    // machine.  Two Spawners exercise the prefix sum: their tickets in one
    // tick are ranked by EntityID order in the ready set, and the
    // assertions below check the ranks landed disjoint and in order.
    define_test("save_format_ready_roundtrip") {

        World* w = new World;
        Counter* counter = new Counter;
        counter->_entity_id = w->generate_entity_id();
        counter->_location = Coordinate{0, 0};
        counter->seal();
        w->_entity_for_entity_id.set(counter->_entity_id, counter);
        { EntityIDSet s; s.set(counter->_entity_id);
          w->_located_for_coordinate.set(counter->_location, s); }
        w->_waiting_on_time.set({Time{0}, counter->_entity_id});

        Spawner* spawners[2] = { new Spawner, new Spawner };
        Coordinate spawner_at[2] = { Coordinate{5, 5}, Coordinate{-7, 3} };
        for (int i = 0; i != 2; ++i) {
            spawners[i]->_entity_id = w->generate_entity_id();
            spawners[i]->_location = spawner_at[i];
            spawners[i]->seal();
            w->_entity_for_entity_id.set(spawners[i]->_entity_id, spawners[i]);
            { EntityIDSet s; s.set(spawners[i]->_entity_id);
              w->_located_for_coordinate.set(spawner_at[i], s); }
            w->_waiting_on_time.set({Time{0}, spawners[i]->_entity_id});
        }
        EntityID cursor0 = w->_entity_id_source;
        w->hack_repair_invariant();

        auto step_once = [](Root<World*>& r) -> coroutine::Task {
            epoch::Epoch pin = pin_global_epoch();
            Root<World*> next = co_await r._ptr->step();
            unpin_global_epoch(pin);
            r = std::move(next);
        };

        // Step to time 3.  Tick 0: both spawners request (two tickets, the
        // cursor advances by 2, and the delivery lands in each spawner's
        // installed successor).  Tick 1: both spend, each installing a new
        // Machine under the ID it was dealt.  Tick 2: the machines wake.
        Root<World*> a{w};
        for (int i = 0; i != 3; ++i)
            co_await step_once(a);
        assert(a._ptr->_time == Time{3});
        assert(!a._ptr->_ready.is_empty());

        // The cursor advanced (by at least the two first-tick requests),
        // and both spawn cells are now occupied by machines whose IDs were
        // dealt from the block above the starting cursor: distinct, in the
        // block, and ordered like their spawners (rank order == EntityID
        // order of the requesters).
        assert(a._ptr->_entity_id_source.data >= cursor0.data + 2);
        EntityID spawned[2] = {};
        for (int i = 0; i != 2; ++i) {
            CHECK(a._ptr->_entity_id_for_coordinate.try_get(spawner_at[i], spawned[i]));
            assert(spawned[i]);
            assert(spawned[i].data >= cursor0.data);
            assert(spawned[i].data < a._ptr->_entity_id_source.data);
        }
        assert(spawned[0] != spawned[1]);
        assert((spawners[0]->_entity_id < spawners[1]->_entity_id)
               == (spawned[0] < spawned[1]));

        // Save mid-flight, reload, unfold the bucket into _ready.
        std::vector<uint8_t> b1 = test_save_to_buffer(a._ptr);
        World* l = test_load_from_buffer(b1);
        assert(l);
        assert(l->_ready.is_empty());
        assert(l->_entity_id_source == a._ptr->_entity_id_source);
        assert(l->_waiting_on_time.contains({Time{3}, counter->_entity_id}));
        // The content hash folds _ready into the time wheel as the save
        // does, so the commissioned load (bucket unfolded into _ready)
        // hashes as the stepped original did incrementally, and the
        // oracle agrees with both.
        const uint64_t hash_at_save = a._ptr->content_hash();
        assert(hash_at_save == world_content_hash_oracle(a._ptr));
        l->hack_repair_invariant();
        assert(!l->_ready.is_empty());
        assert(l->content_hash() == hash_at_save);
        assert(world_content_hash_oracle(l) == hash_at_save);
        Root<World*> b{l};

        // Both lineages step onward and must not diverge -- including in
        // every EntityID minted from here on.
        for (int i = 0; i != 3; ++i) {
            co_await step_once(a);
            co_await step_once(b);
        }

        Term ta{}, tb{};
        CHECK(a._ptr->_term_for_coordinate.try_get(Coordinate{0, 0}, ta));
        CHECK(b._ptr->_term_for_coordinate.try_get(Coordinate{0, 0}, tb));
        assert(ta.as_int64_t() == 6);
        assert(tb.as_int64_t() == 6);
        assert(a._ptr->_entity_id_source == b._ptr->_entity_id_source);

        std::vector<uint8_t> ba = test_save_to_buffer(a._ptr);
        std::vector<uint8_t> bb = test_save_to_buffer(b._ptr);
        assert(ba == bb);
        assert(a._ptr->content_hash() == b._ptr->content_hash());
        assert(a._ptr->content_hash() != hash_at_save);
        assert(world_content_hash_oracle(a._ptr) == a._ptr->content_hash());
        assert(world_content_hash_oracle(b._ptr) == b._ptr->content_hash());

        printf("ready_roundtrip: cursor %llu -> %llu, spawned EntityID{%llu} and EntityID{%llu}\n",
               (unsigned long long)cursor0.data,
               (unsigned long long)a._ptr->_entity_id_source.data,
               (unsigned long long)spawned[0].data,
               (unsigned long long)spawned[1].data);

        co_return;
    };

    define_test("save_format_entity_roundtrip") {

        // Mirrors the model() startup population: a Player plus localized
        // entities, all reachable through the entity-for-entity-id map.
        World* w = new World;

        Player* player = new Player;
        player->_entity_id = w->generate_entity_id();
        Spawner* spawner = new Spawner;
        spawner->_entity_id = w->generate_entity_id();
        Source* source = new Source;
        source->_entity_id = w->generate_entity_id();
        Sink* sink = new Sink;
        sink->_entity_id = w->generate_entity_id();

        spawner->_location = Coordinate{0, 0};
        source->_location = Coordinate{2, 2};
        source->_of_this = Term(new HeapInt64(1234567));
        sink->_location = Coordinate{4, 2};

        // Simulate a long-lived game: a saved ID far past this process's
        // oracle, to check the loader advances it (no post-load collisions).
        sink->_entity_id = EntityID{1000000};

        for (const Entity* e : { (const Entity*)player, (const Entity*)spawner,
                                 (const Entity*)source, (const Entity*)sink }) {
            e->seal();
            w->_entity_for_entity_id.set(e->_entity_id, e);
        }

        w->hack_repair_invariant();
        std::vector<uint8_t> buffer = test_save_to_buffer(w);
        World* w2 = test_load_from_buffer(buffer);
        assert(w2);
        w2->hack_repair_invariant();
        assert(w2->content_hash() == w->content_hash());
        assert(world_content_hash_oracle(w2) == w->content_hash());

        auto get = [w2](EntityID id) -> const Entity* {
            const Entity* e = nullptr;
            bool has = w2->_entity_for_entity_id.try_get(id, e);
            assert(has);
            assert(e);
            assert(e->_entity_id == id);
            return e;
        };

        const Entity* p2 = get(player->_entity_id);
        assert(p2->_save_type_tag() == Player::SAVE_TYPE_TAG);

        const Entity* sp2 = get(spawner->_entity_id);
        assert(sp2->_save_type_tag() == Spawner::SAVE_TYPE_TAG);
        assert((static_cast<const Spawner*>(sp2)->_location == Coordinate{0, 0}));

        const Entity* so2 = get(source->_entity_id);
        assert(so2->_save_type_tag() == Source::SAVE_TYPE_TAG);
        const Source* loaded_source = static_cast<const Source*>(so2);
        assert((loaded_source->_location == Coordinate{2, 2}));
        assert(_term_is_object(loaded_source->_of_this));
        const HeapTerm* h = _term_as_object(loaded_source->_of_this);
        assert(h && h->_save_type_tag() == HeapInt64::SAVE_TYPE_TAG);
        assert(static_cast<const HeapInt64*>(h)->as_int64_t() == 1234567);

        const Entity* sk2 = get(sink->_entity_id);
        assert(sk2->_save_type_tag() == Sink::SAVE_TYPE_TAG);
        assert((static_cast<const Sink*>(sk2)->_location == Coordinate{4, 2}));

        // TODO: assert that the world's entity_id source is greater than
        // any entity observed in the save
        
        co_return;
    };

    define_test("save_format_world_roundtrip") {

        // A world that is a superset of the model() starting content: all
        // three WaitableMaps populated on both kv and ki sides, the time
        // wheel, a Machine with a stack, and a heap object shared across
        // several parents (so saver dedup and load-time sharing are
        // exercised).  Two layers of checking: targeted semantic asserts for
        // debuggability, then the strong backstop -- save -> load -> save
        // byte-equality.  If the loader faithfully rebuilds everything the
        // saver emits (same records, same sharing, same order), re-saving the
        // loaded world reproduces the stream exactly; any dropped field,
        // wrong type, or broken sharing diverges the bytes.

        World* w = new World;
        w->_time = Time{42};

        // Heap object referenced by a coordinate term, the Source, and the
        // Machine stack -- one record, three refs, dedup'd by the saver.
        HeapInt64* shared = new HeapInt64(9001);
        Term shared_term = Term((const HeapTerm*)shared);

        Player* player = new Player;
        player->_entity_id = w->generate_entity_id();
        Spawner* spawner = new Spawner;  spawner->_location = Coordinate{0, 0};
        spawner->_entity_id = w->generate_entity_id();
        Source* source = new Source;     source->_location = Coordinate{2, 2};
                                         source->_of_this = shared_term;
        source->_entity_id = w->generate_entity_id();
        Sink* sink = new Sink;           sink->_location = Coordinate{4, 2};
        sink->_entity_id = w->generate_entity_id();
        Counter* counter = new Counter;  counter->_location = Coordinate{-2, 2};
        counter->_entity_id = w->generate_entity_id();
        Evenator* even = new Evenator;   even->_location = Coordinate{3, 3};
        even->_entity_id = w->generate_entity_id();

        Machine* machine = new Machine;
        machine->_entity_id = w->generate_entity_id();
        machine->_phase = Machine::PHASE_TRAVELLING;
        machine->_on_arrival = OPCODE_NOOP;
        machine->_old_heading = HEADING_EAST;
        machine->_new_heading = HEADING_NORTH;
        machine->_old_location = Coordinate{5, 5};
        machine->_new_location = Coordinate{6, 5};
        machine->_old_time = Time{40};
        machine->_new_time = Time{41};
        machine->push(term_make_integer_with(7));
        machine->push(shared_term);  // share the heap int into the stack too
        machine->push(term_make_opcode(OPCODE_FLIP_FLOP));

        const Entity* entities[] = { player, spawner, source, sink,
                                     counter, even, machine };
        for (const Entity* e : entities) {
            e->seal();
            w->_entity_for_entity_id.set(e->_entity_id, e);
        }

        w->_entity_id_for_coordinate.set(spawner->_location, spawner->_entity_id);
        w->_entity_id_for_coordinate.set(source->_location, source->_entity_id);
        w->_entity_id_for_coordinate.set(machine->_new_location, machine->_entity_id);

        // Location multimap: statics and the machine, including one
        // multi-member set (spawner + machine sharing a cell) so the
        // nested set encoding is exercised on the kv side too.
        { EntityIDSet s; s.set(spawner->_entity_id); s.set(machine->_entity_id);
          w->_located_for_coordinate.set(spawner->_location, s); }
        { EntityIDSet s; s.set(source->_entity_id);
          w->_located_for_coordinate.set(source->_location, s); }
        { EntityIDSet s; s.set(sink->_entity_id);
          w->_located_for_coordinate.set(sink->_location, s); }
        { EntityIDSet s; s.set(machine->_entity_id);
          w->_located_for_coordinate.set(machine->_new_location, s); }

        w->_term_for_coordinate.set(Coordinate{0, 1}, term_make_integer_with(1));
        w->_term_for_coordinate.set(Coordinate{0, 2}, term_make_integer_with(2));
        w->_term_for_coordinate.set(Coordinate{0, 4}, term_make_opcode(OPCODE_FLIP_FLOP));
        w->_term_for_coordinate.set(Coordinate{-2, -2}, shared_term);
        w->_term_for_coordinate.set(Coordinate{7, 7}, term_make_string_with("hello save"));

        // Terrain patch spanning positive and negative coordinates, plus a
        // waiter on a terrain cell (an entity waiting on a terrain change),
        // so both sides of the terrain WaitableMap hit the file.
        w->_terrain_for_coordinate.set(Coordinate{0, 0}, TERRAIN_GRASS);
        w->_terrain_for_coordinate.set(Coordinate{1, 0}, TERRAIN_WATER);
        w->_terrain_for_coordinate.set(Coordinate{-40, 25}, TERRAIN_ROCK);
        w->_terrain_for_coordinate.set(Coordinate{6, -3}, TERRAIN_SAND);
        { WaitSet ws; ws.set(spawner->_entity_id);
          w->_terrain_for_coordinate.ki.set(Coordinate{1, 0}, ws); }

        { WaitSet ws; ws.set(player->_entity_id);
          w->_term_for_coordinate.ki.set(Coordinate{0, 1}, ws); }
        { WaitSet ws; ws.set(sink->_entity_id); ws.set(counter->_entity_id);
          w->_entity_id_for_coordinate.ki.set(Coordinate{0, 0}, ws); }
        { WaitSet ws; ws.set(machine->_entity_id);
          w->_entity_for_entity_id.ki.set(source->_entity_id, ws); }

        // Strictly future relative to _time == 42: the world invariant
        // (nothing waiting on the past or present outside _ready) now
        // holds at construction, so hack_repair_invariant is a no-op and
        // the save -> load -> save byte-equality backstop is unaffected.
        w->_waiting_on_time.set({Time{43}, player->_entity_id});
        w->_waiting_on_time.set({Time{44}, machine->_entity_id});
        w->hack_repair_invariant();

        std::vector<uint8_t> b1 = test_save_to_buffer(w);
        World* w2 = test_load_from_buffer(b1);
        assert(w2);
        w2->hack_repair_invariant();

        // Content hash: the loaded world hashes as the original, at
        // different addresses throughout; both agree with the oracle.
        assert(w2->content_hash() == w->content_hash());
        assert(world_content_hash_oracle(w) == w->content_hash());
        assert(world_content_hash_oracle(w2) == w2->content_hash());
        // And the entity hashes are content hashes: the shared heap int
        // and the machine's stack hash alike on both sides.
        {
            const Entity* me = nullptr;
            CHECK(w2->_entity_for_entity_id.try_get(machine->_entity_id, me));
            assert(me != machine);
            assert(me->content_hash() == machine->content_hash());
            assert(PersistentStack<Term>::hash_of(static_cast<const Machine*>(me)->_stack)
                   == PersistentStack<Term>::hash_of(machine->_stack));
            Term tv;
            CHECK(w2->_term_for_coordinate.try_get(Coordinate{-2, -2}, tv));
            assert(_term_as_object(tv) != shared);
            assert(content_hash(tv) == content_hash(shared_term));
        }

        // Maps / terms / entities / wait set: targeted semantic checks.
        assert(w2->_time == w->_time);

        Term t;
        CHECK(w2->_term_for_coordinate.try_get(Coordinate{0, 2}, t));
        assert(t._data == term_make_integer_with(2)._data);

        EntityID eid;
        CHECK(w2->_entity_id_for_coordinate.try_get(Coordinate{2, 2}, eid));
        assert(eid == source->_entity_id);

        // The shared heap object survives and is STILL shared (one pointer)
        // across the coordinate term and the Source after reload.
        Term tv_coord;
        CHECK(w2->_term_for_coordinate.try_get(Coordinate{-2, -2}, tv_coord));
        const Entity* se = nullptr;
        CHECK(w2->_entity_for_entity_id.try_get(source->_entity_id, se));
        assert(se->_save_type_tag() == Source::SAVE_TYPE_TAG);
        Term tv_source = static_cast<const Source*>(se)->_of_this;
        assert(_term_is_object(tv_coord) && _term_is_object(tv_source));
        assert(_term_as_object(tv_coord) == _term_as_object(tv_source));
        assert(static_cast<const HeapInt64*>(_term_as_object(tv_coord))
                   ->as_int64_t() == 9001);

        const Entity* me = nullptr;
        CHECK(w2->_entity_for_entity_id.try_get(machine->_entity_id, me));
        assert(me->_save_type_tag() == Machine::SAVE_TYPE_TAG);
        const Machine* m2 = static_cast<const Machine*>(me);
        assert(m2->_phase == Machine::PHASE_TRAVELLING);
        assert((m2->_new_location == Coordinate{6, 5}));
        assert(m2->_new_time == Time{41});
        assert(PersistentStack<Term>::size(m2->_stack) == 3);

        assert(w2->_waiting_on_time.contains({Time{44}, machine->_entity_id}));

        WaitSet ws;
        CHECK(w2->_entity_id_for_coordinate.ki.try_get(Coordinate{0, 0}, ws));
        std::set<uint64_t> got;
        ws.for_each([&got](EntityID e) { got.insert(e.data); });
        assert((got == std::set<uint64_t>{ sink->_entity_id.data,
                                           counter->_entity_id.data }));

        // Location multimap round-trips, including the multi-member set.
        {
            EntityIDSet ls;
            CHECK(w2->_located_for_coordinate.try_get(spawner->_location, ls));
            std::set<uint64_t> located;
            ls.for_each([&located](EntityID e) { located.insert(e.data); });
            assert((located == std::set<uint64_t>{ spawner->_entity_id.data,
                                                   machine->_entity_id.data }));
            EntityIDSet ss;
            CHECK(w2->_located_for_coordinate.try_get(sink->_location, ss));
            assert(ss.contains(sink->_entity_id));
        }

        Terrain terrain = -1;
        CHECK(w2->_terrain_for_coordinate.try_get(Coordinate{1, 0}, terrain));
        assert(terrain == TERRAIN_WATER);
        CHECK(w2->_terrain_for_coordinate.try_get(Coordinate{-40, 25}, terrain));
        assert(terrain == TERRAIN_ROCK);
        CHECK(!w2->_terrain_for_coordinate.try_get(Coordinate{9, 9}, terrain));
        {
            WaitSet tws;
            CHECK(w2->_terrain_for_coordinate.ki.try_get(Coordinate{1, 0}, tws));
            std::set<uint64_t> twaiters;
            tws.for_each([&twaiters](EntityID e) { twaiters.insert(e.data); });
            assert((twaiters == std::set<uint64_t>{ spawner->_entity_id.data }));
        }

        // Strong backstop: re-saving the loaded world reproduces the stream.
        // Shape: the loaded tries are rebuilt from flat entries, so each must
        // be in canonical form and, because the originals were built by
        // insertion (also canonical), equal to its original node for node.
        // The ki maps are compared through to their inlined waitsets.
        {
            auto same = [](auto const& a, auto const& b) {
                using N = std::remove_cv_t<std::remove_pointer_t<decltype(a._inner)>>;
                N::assert_canonical(b._inner);
                return N::same_shape(a._inner, b._inner);
            };
            auto same_ki = [&same](auto const& a, auto const& b) {
                bool ok = same(a, b);
                a.for_each([&ok, &b](auto key, EntityIDSet ws) {
                    EntityIDSet ws2;
                    ok = ok && b.try_get(key, ws2)
                            && NodeSet_U64::same_shape(ws._inner, ws2._inner);
                    NodeSet_U64::assert_canonical(ws2._inner);
                });
                return ok;
            };
            assert(same(w->_term_for_coordinate.kv, w2->_term_for_coordinate.kv));
            assert(same(w->_entity_id_for_coordinate.kv, w2->_entity_id_for_coordinate.kv));
            assert(same(w->_entity_for_entity_id.kv, w2->_entity_for_entity_id.kv));
            assert(same(w->_terrain_for_coordinate.kv, w2->_terrain_for_coordinate.kv));
            assert(same_ki(w->_located_for_coordinate.kv, w2->_located_for_coordinate.kv));
            assert(same_ki(w->_term_for_coordinate.ki, w2->_term_for_coordinate.ki));
            assert(same_ki(w->_entity_id_for_coordinate.ki, w2->_entity_id_for_coordinate.ki));
            assert(same_ki(w->_located_for_coordinate.ki, w2->_located_for_coordinate.ki));
            assert(same_ki(w->_entity_for_entity_id.ki, w2->_entity_for_entity_id.ki));
            assert(same_ki(w->_terrain_for_coordinate.ki, w2->_terrain_for_coordinate.ki));
            NodeSet_U128::assert_canonical(w2->_waiting_on_time._inner);
        }

        std::vector<uint8_t> b2 = test_save_to_buffer(w2);
        assert(b1 == b2);

        co_return;
    };


    // The client map is World state: saved, loaded, and in the hash.
    define_test("save_format_player_for_client_roundtrip") {
        World* w = new World;
        Player* p = new Player;
        p->_entity_id = w->generate_entity_id();
        p->seal();
        w->_entity_for_entity_id.set(p->_entity_id, p);
        w->_waiting_on_time.set({Time{0}, p->_entity_id});
        w->_player_for_client.set(ClientID{3}, p->_entity_id);
        w->hack_repair_invariant();

        std::vector<uint8_t> buffer = test_save_to_buffer(w);
        World* w2 = test_load_from_buffer(buffer);
        assert(w2);
        w2->hack_repair_invariant();
        EntityID mapped{};
        assert(w2->_player_for_client.try_get(ClientID{3}, mapped) && (mapped == p->_entity_id));
        assert(w2->_player_for_client.size() == 1);
        assert(w2->content_hash() == w->content_hash());

        // A different mapping is a different World
        World* w3 = new World;
        Player* q = new Player;
        q->_entity_id = w3->generate_entity_id();
        q->seal();
        w3->_entity_for_entity_id.set(q->_entity_id, q);
        w3->_waiting_on_time.set({Time{0}, q->_entity_id});
        w3->_player_for_client.set(ClientID{4}, q->_entity_id);
        w3->hack_repair_invariant();
        assert(w3->content_hash() != w->content_hash());
        co_return;
    };

}  // namespace wry
