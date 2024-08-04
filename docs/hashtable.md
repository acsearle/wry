#  Hash table

We need
- Garbage collection-compatible
- Bounded worse case, in particular no O(N) rehash/resize
- Performant

We don't need
- Particularly high load factors / space efficiency
- Reference stability

It may be worth having different objects rather than a one-size fits all
for tables that have short lives, are otherwise bounded, or do not support
erase.

An incremental copying strategy is reasonably straightforward but seems
incompatible with tombstones.  When the table is roughly steady state, it will
accrue tombstones, and then rehash; the rehash then trades size growth off
against the number of slots that must be scanned per increment, with six being
the minimum needed to downsize a sparsely populated graveyard.  

An alternative is robin hood hashing which produces no tombstones and instead
does the work of recompacting as part of erasure, and will never need to resize
in a steady state.  By moving stuff around, it will generate a lot of GC 
traffic.

The no-resize alternative is the HAMT or BTree with O(log N) operations.






#if 0


/*
 template<typename K, typename V>
 struct HashMap : Object {
 
 virtual void _object_debug() const {
 printf("HashMap<K, V>");
 }
 
 virtual void _object_scan() const {
 
 }
 
 };
 
 template<>
 struct HashMap<Value, Value> : Object {
 
 virtual ~HashMap() override {
 }
 
 virtual void _object_debug() const override {
 printf("HashMap<K, V>");
 }
 
 virtual void _object_scan() const override {
 
 }
 
 virtual bool _value_empty() const override {}
 virtual size_t _value_size() const override {}
 virtual bool _value_contains(Value key) const override {}
 virtual Value _value_find(Value key) const override {}
 virtual Value _value_insert_or_assign(Value key, Value value) override {}
 virtual Value _value_erase(Value key) override {}
 
 };
 
 
 */
namespace wry::gc {
    
    template<typename A>
    struct Maybe {
        bool _isJust;
        union {
            unsigned char _dummy;
            A _a;
        };
        
        /*
        template<typename B>
        explicit Maybe(B&& b)
        : _isJust(true)
        , _a(std::forward<B>(b)) {
        }
         */
        
        Maybe()
        : _isJust(false) {
        }
        
        Maybe(const Maybe& other)
        : _isJust(other._isJust) {
            if (other._isJust)
                new (&_a) A(other._a);
        }

        Maybe(Maybe&& other)
        : _isJust(other._isJust) {
            if (other._isJust)
                new (&_a) A(std::move(other._a));
        }
        
        ~Maybe() {
            if (_isJust)
                _a.~A();
        }
        
        Maybe& operator=(const Maybe& other) {
            if (_isJust) {
                if (other._isJust) {
                    _a = other._a;
                } else {
                    _a.~A();
                    _isJust = false;
                }
            } else {
                if (other._isJust) {
                    new (&_a) A(other._a);
                    _isJust = false;
                }
            }
        }
        
        Maybe& operator=(Maybe&& other) {
            if (_isJust) {
                if (other._isJust) {
                    _a = other._a;
                } else {
                    _a.~A();
                    _isJust = false;
                }
            } else {
                if (other._isJust) {
                    new (&_a) A(other._a);
                    _isJust = false;
                }
            }
        }
        
    };
    
    template<typename A>
    Maybe(A&& a) -> Maybe<std::decay_t<A>>;
    
    constexpr struct {
        template<typename A> operator Maybe<A>() {
            return Maybe<A>();
        }
    } Nothing;

    constexpr struct {
        template<typename A>
        Maybe<std::decay_t<A>> operator()(A&& a) {
            return Maybe<std::decay_t<A>>(std::forward<A>(a));
        }
    } Just;
    
    template<typename A>
    bool isNothing(const Maybe<A>& m) {
        return !m._isJust;
    }
    
    template<typename A>
    bool isJust(const Maybe<A>& m) {
        return m._isJust;
    }

    template<typename A>
    std::decay_t<A> fromMaybe(A&& a, const Maybe<std::decay_t<A>>& m) {
        if (isJust(m)) {
            return m._a;
        } else {
            return std::forward<A>(a);
        }
    }

    template<typename A>
    std::decay_t<A> fromMaybe(A&& a, Maybe<std::decay_t<A>>&& m) {
        if (isJust(m)) {
            return std::move(m._a);
        } else {
            return std::forward<A>(a);
        }
    }
    
    template<typename B, typename F, typename A>
    std::decay_t<B> maybe(B&& b, F&& f, const Maybe<A>& m) {
        if (isJust(m)) {
            return std::forward<F>(f)(m._a);
        } else {
            return std::forward<B>(b);
        }
    }

    template<typename B, typename F, typename A>
    std::decay_t<B> maybe(B&& b, F&& f, Maybe<A>&& m) {
        if (isJust(m)) {
            return std::forward<F>(f)(std::move(m._a));
        } else {
            return std::forward<B>(b);
        }
    }

    
    
    
    
    template<typename K, typename V>
    struct PairKeyValue {
        K key;
        V value;
    };
    
    template<typename K, typename V>
    struct MaybePairKeyValue {
        bool _isJust = false;
        union {
            unsigned char _dummy;
            PairKeyValue<K, V> kv;
        };
        
        bool isJust() const { return _isJust; }
        
    };
        
    template<typename K, typename V>
    struct PrimitiveHashSet {
        
        using T = MaybePairKeyValue<K, V>;
        
        T* _data;
        size_t _capacity;
        size_t _size;
        
        PrimitiveHashSet(T* data_, size_t capacity_, size_t size_)
        : _data(data_)
        , _capacity(capacity_)
        , _size(size_) {
            assert(!capacity_ || data_);
            assert(!capacity_ || std::has_single_bit(capacity_));
            assert(!capacity_ || (size_ < capacity_));
        }

        T* data() const { return _data; }
        size_t capacity() const { return _capacity; }
        size_t size() const { return _size; }
        
        T* begin() const { return _data; }
        T* end() const { return _data + _capacity; }
        bool empty() const { return !_size; }

        size_t _mask(size_t hashcode) const {
            return( _capacity - 1) & hashcode;
        }
        
        size_t _displacement(size_t index) const {
            assert(index < capacity());
            assert(HashSetEntry_is_occupied(_data[index]));
            size_t hashcode = HashSetEntry_get_hashcode(_data[index]);
            return _mask(index - hashcode);
        }
        
        size_t _succ(size_t index) const {
            return _mask(index + 1);
        }
        
        size_t _pred(size_t index) const {
            return _mask(index - 1);
        }
        
        void _invariant() const {
            size_t n = 0;
            for (size_t i = 0; i != _capacity; ++i) {
                if (HashSetEntry_is_occupied(_data[i])) {
                    ++n;
                    size_t di = _displacement(i);
                    size_t j = _pred(i);
                    if (HashSetEntry_is_occupied(_data[j])) {
                        // if there is an occupied slot before us, it should be
                        // at least as displaced as we are
                        size_t dj = _displacement(j);
                        assert(dj + 1 >= di);
                    } else {
                        // if there is an empty slot before this entry, we must
                        // be in our ideal slot
                        assert(di == 0);
                    }
                }
            }
            assert(n == _size);
        }
        
        void _steal(size_t index) const {
            assert(index < capacity());
            assert(HashSetEntry_is_occupied(_data[index]));
            // find first unoccupied entry
            size_t j = index;
            for (;;) {
                j = _succ(j);
                if (!HashSetEntry_is_occupied(_data[j]))
                    break;
            }
            // move_backward
            for (;;) {
                size_t k = _pred(j);
                _data[j] = std::move(_data[k]);
                if (k == index)
                    break;
                j = k;
            }
        }
        
        void _give(size_t index) const {
            assert(index < capacity());
            for (;;) {
                assert(HashSetEntry_is_occupied(_data[index]));
                size_t j = _succ(j);
                if (!HashSetEntry_is_occupied(_data[j]))
                    break;
                size_t dj = _displacement(j);
                if (dj == 0)
                    break;
                // move forward
                _data[index] = std::move(_data[j]);
                index = j;
            }
            assert(HashSetEntry_is_occupied(_data[index]));
            HashSetEntry_erase(_data[index]);
        }
        
        template<typename Keylike>
        T* find(size_t hashcode, Keylike&& k) const {
            size_t index = _mask(hashcode);
            size_t di = 0;
            for (;;) {
                if (!HashSetEntry_is_occupied(_data[index]))
                    // key would be here
                    return nullptr;
                if (HashSetEntry_is_equivalent(_data[index], hashcode, k))
                    // (equivalent) key is here
                    return _data + index;
                //TODO: we know hashcode?
                size_t dj = _displacement(_data[index]);
                if (dj < di)
                    // key would be here
                    return nullptr;
                index = _succ(index);
                ++di;
            }
        }
        
        template<typename Keylike>
        size_t erase(size_t hashcode, Keylike&& k) const {
            T* p = find(hashcode, k);
            if (!p)
                return 0;
            _give(p - _data);
            --_size;
            return 1;
        }
        
        template<typename Keylike>
        size_t try_insert(size_t hashcode, Keylike&& k) const {
            size_t index = _mask(hashcode);
            size_t di = 0;
            for (;;) {
                if (!HashSetEntry_is_occupied(_data[index]))
                    break;
                if (HashSetEntry_is_equivalent(_data[index], hashcode, k))
                    return 0;
                //TODO: we know hashcode?
                size_t dj = _displacement(_data[index]);
                if (dj < di) {
                    _steal(index);
                    break;
                }
                index = _succ(index);
                ++di;
            }
            _data[index] = std::forward<Keylike>(k);
            ++_size;
            return 1;
        }
        
        template<typename Keylike>
        size_t try_assign(size_t hashcode, Keylike&& k) const {
            T* p = find(hashcode, k);
            if (!p)
                return 0;
            *p = std::forward<Keylike>(k);
            return 1;
        }

        template<typename Keylike>
        size_t insert_or_assign(size_t hashcode, Keylike&& k) const {
            size_t index = _mask(hashcode);
            size_t di = 0;
            for (;;) {
                if (!HashSetEntry_is_occupied(_data[index]))
                    break;
                if (HashSetEntry_is_equivalent(_data[index], hashcode, k)) {
                    _data[index] = std::forward<Keylike>(k);
                    return 0;
                }
                //TODO: we know hashcode?
                size_t dj = _displacement(_data[index]);
                if (dj < di) {
                    _steal(index);
                    break;
                }
                index = _succ(index);
                ++di;
            }
            _data[index] = std::forward<Keylike>(k);
            ++_size;
            return 1;
        }
        
    };
    
    
    enum class EntryTag {
        VACANT = 0,
        OCCUPIED,
        TOMBSTONE,
    };

    template<typename A>
    concept Entry = requires(const A& a) {
        { entry_get_hash(a) } -> std::convertible_to<std::size_t>;
        { entry_get_tag(a) } -> std::convertible_to<EntryTag>;
    };
        
    template<typename Entry>
    struct RawEntry {
        
        EntryTag tag;
        Entry entry;
        
        EntryTag get_tag() { return tag; }
        
        template<typename J>
        static hash_t get_keylike_hash(const J& keylike) { return object_hash(keylike); }
        static hash_t get_keylike_hash(const RawEntry& keylike) { return get_keylike_hash(keylike.entry); }
        hash_t get_self_hash() const { return get_keylike_hash(entry); }

        template<typename J>
        bool equals(const J& keylike, hash_t hashcode) {
            return entry == keylike;
        }
        
        EntryTag erase() {
            object_passivate(entry);
            return exchange(tag, EntryTag::TOMBSTONE);
        }
        
        template<typename J>
        EntryTag assign(J&& keylike) {
            if constexpr (std::is_same_v<std::decay_t<J>, RawEntry>) {
                entry = std::forward_like<J>(keylike.entry);
            } else {
                entry = std::forward<J>(keylike);
            }
            return exchange(tag, EntryTag::OCCUPIED);
        }

    };
    
    template<typename K>
    void object_trace(const RawEntry<K>& self) {
        object_trace(self.entry);
    }
    
    template<typename Entry>
    EntryTag entry_get_tag(const RawEntry<Entry>& entry) {
        return entry.tag;
    }
    
    template<typename Entry, typename J>
    bool entry_equality(const RawEntry<Entry>& entry, const J& keylike, hash_t keylike_hashcode) {
        return entry.entry == keylike;
    }
    
    template<typename Entry>
    hash_t entry_get_hash(const RawEntry<Entry>& entry) {
        return entry.get_self_hash();
    }
    
    // Attempt 3: RobinHood hash table avoids the situation of a table full of
    // tombstones
    //
    // RobinHood tables are self-compacting, have no tombstones, and thus never
    // fill up with tombstones
    //
    // This table never resizes itself (hence, Static) but is used as the
    // building block of an incrementally resizing table
    
    template<typename Entry>
    struct GarbageCollectedRobinHoodStaticHashTable {
        
        // hot?
        Entry* _data = nullptr;
        hash_t _mask = -1;
        
        // cold?
        size_t _size = 0;
        Traced<GarbageCollectedIndirectStaticArray<Entry>*> _storage;
        
        void swap(GarbageCollectedRobinHoodStaticHashTable& other) {
            using std::swap;
            swap(_data, other._data);
            swap(_mask, other._mask);
            swap(_size, other._size);
            swap(_storage, other._storage);
        }
        
        void clear_and_reserve(size_t new_capacity) {
            assert(std::has_single_bit(new_capacity));
            _mask = new_capacity - 1;
            _size = 0;
            _storage = new GarbageCollectedIndirectStaticArray<Entry*>>(new_capacity);
            _data = _storage->data();
        }

        GarbageCollectedRobinHoodStaticHashTable() = default;
        
        explicit GarbageCollectedRobinHoodStaticHashTable(size_t new_capacity) {
            clear_and_reserve(new_capacity);
        }
                        
        hash_t _next(hash_t h) const {
            return (h + 1) & _mask;
        }
        
        hash_t _prev(hash_t h) const {
            return (h - 1) & _mask;
        }
        
        
        Entry* begin() {
            return _data;
        }
        
        Entry* end() {
            return _data + capacity();
        }
        
        Entry* data() {
            return _data;
        }
        
        Entry* to(hash_t hashcode) const {
            return _data + (hashcode & _mask);
        }

        size_t displacement(size_t i) const {
            hash_t hashcode = entry_get_hash(_data[i]);
            return (hashcode - i) & _mask;
        }
        
        void _evict_entry_at_index(size_t i) {
            assert(i <= _mask);
            assert(entry_is_occupied(_data[i]));
            size_t j = i;
            // find next vacancy
            for (;;) {
                j = _next(j);
                if (!entry_is_occupied(_data[j]))
                    break;
            }
            // move_backwards to make the hole
            ++_size;
            for (;;) {
                size_t k = _prev(j);
                _data[j] = std::move(k);
                if (k == i)
                    break;
                j = k;
            }
            // _data[i] is still occupied by the moved-from entry
        }
        
        void _erase_occupied_by_index(size_t i) {
            assert(i <= _mask);
            assert(entry_is_occupied(_data[i]));
            for (;;) {
                size_t j = _next(i);
                if (!entry_is_occupied(_data[j]))
                    break;
                size_t d2 = displacement(j);
                if (d2 == 0)
                    break;
                _data[i] = std::move(_data[j]);
                assert(entry_is_occupied(_data[j]));
                i = j;
            }
            entry_erase(_data[i]);
            --_size;
        }
        
        
        template<typename K>
        Entry* find(const K& keylike, hash_t hashcode) const {
            size_t i = hashcode & _mask;
            size_t d1 = 0;
            for (;;) {
                Entry* e = _data + i;
                if (!entry_is_occupied(*e)) {
                    // If the entry existed it would be here
                    return nullptr;
                }
                if (entry_equality(*e, keylike, hashcode)) {
                    // Found
                    return e;
                }
                size_t d2 = displacement(i);
                if (d2 < d1) {
                    // If the entry existed it would have displaced this element
                    return nullptr;
                }
                i = _next(i);
            }
        }
        
        template<typename K>
        void insert_or_assign(hash_t hashcode, K&& k) {
            size_t i = hashcode & _mask;
            size_t dk = 0;
            for (;;) {
                if (!entry_is_occupied(_data[i])) {
                    ++_size;
                    break;
                }
                if (entry_equality(_data[i], k, hashcode))
                    break;
                size_t di = displacement(i);
                if (di < dk) {
                    _evict_entry_at_index(i);
                    break;
                }
                i = _next(i);
                ++dk;
            }
            entry_assign(_data[i], std::forward<K>(k));
        }
        
        size_t maybe_erase_by_index(size_t i) {
            assert(i <= _mask);
            switch (entry_get_tag(_data[i])) {
                case EntryTag::VACANT:
                    return 0;
                case EntryTag::OCCUPIED:
                    _erase_occupied_by_index(i);
                    return 1;
                case EntryTag::TOMBSTONE:
                    abort();
            }
        }
        
    };

    template<typename Entry>
    void object_debug(const GarbageCollectedRobinHoodStaticHashTable<Entry>& x) {
        printf("(GarbageCollectedRobinHoodStaticHashTable)");
        object_debug(x._storage);
    }

    template<typename Entry>
    void object_trace(const GarbageCollectedRobinHoodStaticHashTable<Entry>& x) {
        object_trace(x._storage);
    }

    template<typename Entry>
    void object_shade(const GarbageCollectedRobinHoodStaticHashTable<Entry>& x) {
        object_shade(x._storage);
    }

    
    
    
    template<typename Entry>
    struct RealTimeGarbageCollectedDynamicHashTable {
        
        GarbageCollectedRobinHoodStaticHashTable<Entry> _alpha;
        GarbageCollectedRobinHoodStaticHashTable<Entry> _beta;
        Entry* _partition = 0;
        
        RealTimeGarbageCollectedDynamicHashTable() {
        }
        
        void grow() {
            assert(_alpha.full());
            assert(_beta.empty());
            assert(_partition == _beta.end());
            _beta = std::move(_alpha);
            _alpha = GarbageCollectedRobinHoodStaticHashTable<Entry>(_beta.capacity() << 1);
            _partition = _beta.begin();
        }
        
        template<typename J>
        Entry* find(const J& keylike, hash_t hashcode) {
            if (_alpha.full())
                grow();
            tax();
            Entry* e = _alpha.find(keylike, hashcode);
            if (e->get_tag() == EntryTag::OCCUPIED) {
                return e;
            }
            if (_beta.empty())
                return e;
            Entry* f = _beta.find(keylike, hashcode);
            if (f->get_tag() != EntryTag::OCCUPIED) {
                return e;
            }
            *e = std::move(*f);
            _beta.did_erase(f->erase());
        }
        
       
        
        void tax() {
            if (_partition == _beta.end())
                return;
            if (entry_get_tag(*_partition) == EntryTag::OCCUPIED) {
                // copy one occupant over to the alpha table
                hash_t hashcode = entry_get_hash(*_partition);
                //Entry* a = _alpha.insert_or_assign(<#hash_t hashcode#>, <#K &&k#>)(hashcode);
                //_alpha.did_insert(a->assign(std::move(*_partition)));
                //_beta.did_erase(_partition->erase());
                _alpha.insert_or_assign(hashcode, std::move(*_partition));
                _beta.maybe_erase_by_index(_partition - _beta.begin());
            }
            ++_partition;
        }
    };
    
    
    template<typename Entry>
    void object_debug(const RealTimeGarbageCollectedDynamicHashTable<Entry>& object) {
        printf("(RealTimeGarbageCollectedDynamicHashTable)");
        object_debug(object._alpha);
        object_debug(object._beta);
    }

    template<typename Entry>
    void object_trace(const RealTimeGarbageCollectedDynamicHashTable<Entry>& object) {
        object_trace(object._alpha);
        object_trace(object._beta);
    }
    
    

    
    /*
    
    template<typename Entry>
    struct GarbageCollectedStaticHashTable {
        
        Entry* _data = nullptr;
        hash_t _mask = 0;
        size_t _size = 0;
        ptrdiff_t _grace = 0;
                        
        Traced<GarbageCollectedIndirectStaticArray<Entry>*> _storage;
        
        explicit GarbageCollectedStaticHashTable(size_t new_capacity) {
            assert(_size == 0);
            assert(std::has_single_bit(new_capacity));
            _mask = new_capacity - 1;
            _grace = _mask ^ (_mask >> 2);
            _storage = new GarbageCollectedIndirectStaticArray<Entry>(new_capacity);
            _data = _storage->data();
        }
        
        void swap(GarbageCollectedStaticHashTable& other) {
            using std::swap;
            swap(_data, other._data);
            swap(_mask, other._mask);
            swap(_size, other._size);
            swap(_grace, other._grace);
            swap(_storage, other._storage);
        }
        
        hash_t _next(hash_t h) const {
            return (h + 1) & _mask;
        }

        hash_t _prev(hash_t h) const {
            return (h + 1) & _mask;
        }
        
        size_t size() const {
            return _size;
        }

        size_t capacity() const {
            return _mask + 1;
        }

        size_t grace() const {
            assert(_grace >= 0);
            return _grace;
        }
        
        bool empty() const {
            return !_size;
        }
        
        bool full() const {
            assert(_grace >= 0);
            return !_grace;
        }
        
        Entry* begin() {
            return _data;
        }
        
        Entry* end() {
            return _data + capacity();
        }

        Entry* data() {
            return _data;
        }
        
        Entry* to(hash_t hashcode) const {
            return _data + (hashcode & _mask);
        }
        
        template<typename J>
        Entry* find(const J& keylike, hash_t hashcode) const {
            assert(_data);
            Entry* p = to(hashcode);
            Entry* first_tombstone = nullptr;
            for (;;) {
                switch (entry_get_tag(*p)) {
                    case EntryTag::VACANT:
                        return first_tombstone ? first_tombstone : p;
                    case EntryTag::OCCUPIED:
                        if (entry_equality(*p, keylike, hashcode))
                            return p;
                        break;
                    case EntryTag::TOMBSTONE:
                        if (!first_tombstone)
                            first_tombstone = p;
                        break;
                }
                ++p;
                if (p == end())
                    p = begin();
            }
        }
        
        Entry* find_vacancy(hash_t hashcode) {
            Entry* p = to(hashcode);
            for (;;) {
                switch (entry_get_tag(*p)) {
                    case EntryTag::VACANT:
                        return p;
                    case EntryTag::OCCUPIED:
                        break;
                    case EntryTag::TOMBSTONE:
                        abort();
                }
                ++p;
                if (p == end())
                    p = begin();
            }
        }
        
        Entry* find_unoccupied(hash_t hashcode) {
            Entry* p = to(hashcode & _mask);
            for (;;) {
                switch (entry_get_tag(*p)) {
                    case EntryTag::VACANT:
                        return p;
                    case EntryTag::OCCUPIED:
                        break;
                    case EntryTag::TOMBSTONE:
                        break;
                }
                ++p;
                if (p == end())
                    p = begin();
            }
        }

        
        static size_t compute_capacity_for_size(size_t n) {
            size_t new_capacity = n;
            new_capacity += 1;
            new_capacity += (new_capacity << 1);
            new_capacity = std::bit_ceil(new_capacity);
            return new_capacity;
        }
        
        void did_insert(EntryTag old) {
            switch (old) {
                case EntryTag::VACANT:
                    ++_size;
                    --_grace;
                    break;
                case EntryTag::OCCUPIED:
                    break;
                case EntryTag::TOMBSTONE:
                    ++_size;
                    break;
            }
        }
                
        void did_erase(EntryTag old) {
            switch (old) {
                case EntryTag::VACANT:
                    break;
                case EntryTag::OCCUPIED:
                    --_size;
                    break;
                case EntryTag::TOMBSTONE:
                    break;
            }
        }
                
        void grow() {
            GarbageCollectedStaticHashTable new_(compute_capacity_for_size(_size));
            move_into_clean_table(new_);
            swap(new_);
        }
        
        void move_into_clean_table(GarbageCollectedStaticHashTable& destination) {
            Entry* p = _data;
            Entry* sentinel = _data + capacity();
            for (; p != sentinel; ++p) {
                if (entry_get_tag(*p) != EntryTag::OCCUPIED)
                    continue;
                Entry* q = destination.find_vacancy(entry_get_hash(*p));
                q = std::move(*p);
                ++destination._size;
                --destination._grace;
            }
        }
                        
    };
    
    template<typename Entry>
    void object_trace(const GarbageCollectedStaticHashTable<Entry>& self) {
        object_trace(self._storage);
    }
    
    template<typename Entry>
    struct GarbageCollectedDynamicHashTable {
        
        GarbageCollectedStaticHashTable<Entry> _data;
        
        template<typename J>
        Entry* find(const J& keylike, hash_t hashcode) {
            if (!_data.grace())
                _data.grow();
            return _data.find(keylike, hashcode);
        }
        
    };
    
    template<typename Entry>
    struct RealTimeGarbageCollectedDynamicHashTable {
        
        GarbageCollectedStaticHashTable<Entry> _alpha;
        GarbageCollectedStaticHashTable<Entry> _beta;
        Entry* _partition = 0;
                
        template<typename J>
        Entry* find(const J& keylike, hash_t hashcode) {
            if (_alpha.full())
                grow();
            tax();
            Entry* e = _alpha.find(keylike, hashcode);
            if (e->get_tag() == EntryTag::OCCUPIED) {
                return e;
            }
            if (_beta.empty())
                return e;
            Entry* f = _beta.find(keylike, hashcode);
            if (f->get_tag() != EntryTag::OCCUPIED) {
                return e;
            }
            *e = std::move(*f);
            _beta.did_erase(f->erase());
        }
        
        void grow() {
            assert(_beta.empty());
            assert(_partition == _beta.end());
            _beta = std::move(_alpha);
            size_t n = _beta.size() + (_beta.capacity() >> 2);
            // We ask for enough space for size + capacity / 4 elements
            // We get new capacity that is bit_ceil(3/2 * this)
            // If
            //     (3 / 2)  * (size + capacity / 4) < capacity / 2
            //     (3 / 2) * size + (3 / 8) * capacity < capacity / 2
            //     3 * size + (3/4) * capacity < capacity
            //     3 * size < (1 / 4) capacity
            //     12 * size < capacity
            // that is, we can shrink if the table is at 1/12 of "capacity"
            // == 1/8 of "load"  Since the growth is from 3/8 to 3/4 capacity,
            // the a nice shrink threshold would be 3/16 = 0.1875, wheras 1/12
            // is 0.0833
            //
            //  Suppose we have
            //      16 * size = 3 * capacity
            // Then we have
            //      (size + capacity / k) = (3/4)*(capacity/2)
            //       (3/16) * n + n / k = (3/8)*n
            // So to get 3/16 shrinking the capacity, we need to have a tax rate of
            //       n / k = 3/16 n
            //       k = ceil(16 / 3) = 5.33 -> 6 slots taxed per insert
            //
            // If we instead had a robin hood hash table we would pay the cost
            // of backward-erasing during erases, and would only resize when
            // the table was exactly at the load factor, and never (necessarily)
            // shrink it
            //
            // If we had SIMD buckets the problem gets kicked up a level
            n = GarbageCollectedStaticHashTable<Entry>::compute_capacity_for_size(n);
            _alpha = new GarbageCollectedStaticHashTable<Entry>(n);
            
        }
        
        void tax() {
            if (_partition == _beta.end())
                return;
            assert(_beta.end() - _partition >= 4);
            Entry* sentinel = _partition + 4;
            for (; _partition != sentinel; ++_partition) {
                if (entry_get_tag(*_partition) != EntryTag::OCCUPIED)
                    continue;
                // copy one occupant over to the alpha table
                hash_t hashcode = entry_get_hash(*_partition);
                Entry* a = _alpha.find_unoccupied(hashcode);
                _alpha.did_insert(a->assign(std::move(*_partition)));
                _beta.did_erase(_partition->erase());
            }
        }
    };
    
    
    */

#if 0
    namespace legacy {
        
        struct Entry {
            
            Traced<Value> key;
            Traced<Value> value;
            
            [[nodiscard]] Value entomb() {
                this->key = _value_make_tombstone();
                Value old = this->value;
                this->value = value_make_null();
                return old;
            }
            
        };
        
        void object_trace(const Entry& entry) {
            object_trace(entry.key);
            object_trace(entry.value);
        }
        
        template<typename K, typename KK, typename... Args>
        bool entry_match(const Entry<K, Args...>& entry, const KK& keylike) {
            return entry.key == keylike;
        }
        
        template<typename K, typename V>
        V* entry_pvalue(Entry<K, V>& entry) {
            return &entry.value;
        }
        
        template<typename E>
        struct GarbageCollectedHashMap {
            
            E* _data = nullptr;
            hash_t _mask = 0;
            size_t _count = 0;
            size_t _grace = 0;
            Traced<const GarbageCollectedIndirectStaticArray<E>*> _storage;
            
            hash_t next(hash_t i) const {
                return (i + 1) & _mask;
            }
            
            hash_t prev(hash_t i) const {
                return (i - 1) & _mask;
            }
            
            bool empty() const {
                return !_count;
            }
            
            size_t size() const {
                return _count;
            }
            
            void clear() {
                _data = nullptr;
                _mask = -1;
                _count = 0;
                _grace = 0;
                _storage = nullptr;
            }
            
            template<typename KK>
            E* pfind(hash_t h, KK&& kk) const {
                hash_t i = h & _mask;
                for (;; i = next(i)) {
                    E* p = _data + i;
                    switch (entry_state(p)) {
                        case EntryState::VACANT:
                            return nullptr;
                        case EntryState::OCCUPIED:
                            if (entry_match(p, kk))
                                return p;
                            break;
                        case EntryState::TOMBSTONE:
                            break;
                    }
                }
            }
            
            Value find(std::size_t h, Value k) const {
                E* p = pfind(h, k);
                return p ? *entry_pvalue(*p) : value_make_null();
            }
            
            void clear_n_tombstones_before_i(size_t n, hash_t i) {
                // grace sticks to zero
                if (!_grace)
                    return;
                {
                    // E* pe = _data + i;
                    // [[maybe_unused]] Value ki = pe->key;
                    // assert(value_is_null(ki));
                }
                while (n--) {
                    i = prev(i);
                    E* pe = _data + i;
                    // [[maybe_unused]] Value ki = pe->key;
                    // assert(_value_is_tombstone(ki));
                    // pe->key = value_make_null();
                    entry_exhume(*pe);
                    ++_grace;
                }
            }
            
            Value erase_from(std::size_t h, Value k, std::size_t i) {
                std::size_t tombstones = 0;
                for (;; i = next(i)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (value_is_null(ki)) {
                        clear_n_tombstones_before_i(tombstones, i);
                        return value_make_null();
                    }
                    if (ki == k) {
                        --_count;
                        return pe->entomb();
                    }
                    if (_value_is_tombstone(ki)) {
                        ++tombstones;
                    } else {
                        tombstones = 0;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value erase(std::size_t h, Value k) {
                std::size_t i = h & _mask;
                return erase_from(h, k, i);
            }
            
            Value insert_or_assign(std::size_t h, Value k, Value v) {
                assert(_grace);
                std::size_t i = h & _mask;
                for (;; i = next(i)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (value_is_null(ki)) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        --_grace;
                        return value_make_null();
                    }
                    if (_value_is_tombstone(ki)) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        // we have installed the new key as early as possible
                        // but we must continue scanning and delete the old
                        // one if it exists
                        return erase_from(h, k, next(i));
                    }
                    if (ki == k) {
                        Value u = pe->value;
                        pe->value = v;
                        return u;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value try_assign(std::size_t h, Value k, Value v) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (value_is_null(ki)) {
                        return value_make_null();
                    }
                    if (ki == k) {
                        Value u = pe->value;
                        pe->value = v;
                        return u;
                    }
                    // a different key, or a tombstone
                }
            }
            
            void must_insert(std::size_t h, Value k, Value v) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (value_is_null(ki)) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        --_grace;
                        return;
                    }
                    if (_value_is_tombstone(ki)) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        // Check for the violation of the precondition, that the
                        // key is not later in the table
                        assert(value_is_null(erase_from(h, k, next(i))));
                        return;
                    }
                    // Check for the violation of the precondition, that the
                    // key was not already in the InnerTable
                    assert(ki != k);
                    // a different key was found, continue
                }
            }
            
            void _invariant() const {
                if (!_data)
                    return;
                // scan the whole thing
                std::size_t keys = 0;
                std::size_t nulls = 0;
                std::size_t tombstones = 0;
                for (std::size_t i = 0; i != _mask + 1; ++i) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    Value vi = pe->value;
                    assert(!_value_is_tombstone(vi));
                    if (value_is_null(ki)) {
                        ++nulls;
                        assert(value_is_null(vi));
                    } else if (_value_is_tombstone(ki)) {
                        ++tombstones;
                        assert(value_is_null(vi));
                    } else {
                        ++keys;
                        assert(!value_is_null(vi));
                    }
                }
                assert(keys + nulls + tombstones == _mask + 1);
                assert(keys == _count);
            }
            
        };
        
        
        struct HeapTable : Object {
            
            // Based on a basic open-adressing linear-probing hash table
            //
            // Because of GC, the storage must be GC in its own right, and the
            // entries themselves must be atomic
            //
            // Because of real-time, we perform the resizes incrementally,
            // having an old and a new table, and moving one element over as
            // a tax on each insertion, until the old table is empty, and we
            // release it.
            //
            //
            // When the alpha table reaches its load limit, we allocate a new
            // beta table.  This will have enough capacity to hold twice the
            // elements in the alpha table; in erase-heavy workloads this may
            // actually be the same capacity or even smaller, since the old table
            // may be full of tombstones.  More precisely, the new table has to
            // have initial grace that is at least twice that of the old table
            // count.
            //
            // The old table has been drained up to some index.
            //
            // To insert_or_assign, we look for the entry in the old table.  If it
            // exists, we can replace it without disturbing any metrics.  The
            // drain level may mean we don't need to look.
            //
            // If it does not exist, we look for it in the new table.  If it exists,
            // we can replace it without disturbing any metrics.
            //
            // If it does not exist, we can insert into the new table.  If it
            // overwrites a tombstone, again, we have not made the situation worse.
            // and can terminate.
            //
            // If we have to consume a new element and reduce the grace of the new
            // table, we have to copy over any element of the old table to
            // preserve the invariant of beta_grace >= 2 * alpha_count
            
            // "Grace" is an integer.  For a new table, it is the load limit
            // times the capacity.  Grace is decreased whenever an empty slot is
            // consumed; note that erasure produces tombstones, so empty slots are
            // never recovered.  Thus, insertion may decrease grace, and other
            // operations do not affect it.  It is equivalent to tracking the
            // number of empty slots and comparing it to load limit.
            
            // The old table is, by definition, almost full, so it is not hard to
            // find an element to move over.  Since we write back tombstones as
            // part of a conventional erase, the edge case where the index loops
            // back to the beginning is handled.
            //
            // Interestingly, this strategy allocates new storage on the basis of
            // the current size, not the current capacity, so the table will
            // eventually forget occupancy spikes and right-size itself.  However:
            // When a table has a small count, relative to capacity, and zero grace,
            // a small new table will be allocated, and evacuation will have to
            // scan many elements to find an occupied one.  This is a pain point.
            // Evacuation should instead scan a fixed number of slots, and the
            // new table should be big enough to guarantee completion of that scan
            // before the next resize is needed.  This in turn places a limit on
            // how rapidly the table can shrink.
            
            
            InnerTable _alpha;
            InnerTable _beta;
            std::size_t _partition = 0;
            
            void _invariant() const {
                _alpha._invariant();
                _beta._invariant();
                /*
                 if (_beta._storage) {
                 for (int i = 0; i != _alpha._mask + 1; ++i) {
                 Entry* pe = _alpha._storage + i;
                 Value ki = pe->key;
                 if (is_null(ki) || ki._is_tombstone())
                 continue;
                 Value vj = _beta.find(value_hash(ki), ki);
                 assert(vj.is_null());
                 }
                 for (int i = 0; i != _beta._mask + 1; ++i) {
                 Entry* pe = _beta._storage + i;
                 Value ki = pe->key;
                 if (is_null(ki) || ki._is_tombstone())
                 continue;
                 Value vj = _alpha.find(value_hash(ki), ki);
                 assert(vj.is_null());
                 }
                 }
                 */
            }
            
            
            
            Value find(Value key) const {
                std::size_t h = value_hash(key);
                if (_alpha._count) {
                    Value v = _alpha.find(h, key);
                    if (!value_is_null(v))
                        return v;
                }
                if (_beta._count) {
                    Value v = _beta.find(h, key);
                    if (!value_is_null(v))
                        return v;
                }
                return value_make_null();
            }
            
            Value erase(Value key) {
                //_invariant();
                std::size_t h = value_hash(key);
                if (_alpha._count) {
                    Value v = _alpha.erase(h, key);
                    if (!value_is_null(v)) {
                        return v;
                    }
                }
                if (_beta._count) {
                    Value v = _beta.erase(h, key);
                    if (!value_is_null(v)) {
                        return v;
                    }
                }
                return value_make_null();
                
            }
            
            Value insert_or_assign(Value key, Value value) {
                // _invariant();
                std::size_t h = value_hash(key);
                
                if (_alpha._grace) {
                    Value x = _alpha.insert_or_assign(h, key, value);
                    return x;
                }
                
                {
                    for (std::size_t i = 0; i != _partition; ++i) {
                        Entry* pe = _alpha._storage + i;
                        Value ki = pe->key;
                        assert(value_is_null(ki) || _value_is_tombstone(ki));
                    }
                }
                
                
                if (_alpha._count) {
                    // _alpha is not yet empty
                    Value u = _alpha.try_assign(h, key, value);
                    // _invariant();
                    if (!value_is_null(u))
                        return u;
                    // we have proved that key is not in alpha
                } else {
                    // _alpha is terminal and empty, discard it
                    if (_beta._data) {
                        // swap in _beta
                        _alpha = _beta;
                        _beta._storage = nullptr;
                        _beta._data = nullptr;
                        _beta._count = 0;
                        _beta._grace = 0;
                        _beta._mask = 0;
                        _partition = 0;
                    } else {
                        assert(_alpha._data == nullptr);
                        _alpha._manager = new HeapManaged<Entry>(4);
                        _alpha._data = _alpha._storage->_storage;
                        _alpha._mask = 3;
                        _alpha._grace = 3;
                        _alpha._count = 0;
                        _partition = 0;
                    }
                    return insert_or_assign(key, value);
                }
                if (!_beta._grace) {
                    assert(_beta._data == nullptr);
                    using wry::type_name;
                    std::size_t new_capacity = std::bit_ceil((_alpha._count * 8 + 2) / 3);
                    std::size_t new_grace = new_capacity * 3 / 4;
                    _beta._manager = new HeapManaged<Entry>(new_capacity);
                    _beta._data = _beta._storage->_storage;
                    _beta._count = 0;
                    _beta._grace = new_grace;
                    _beta._mask = new_capacity - 1;
                }
                Value ultimate = _beta.insert_or_assign(h, key, value);
                while (_alpha._count) {
                    Entry* pe = _alpha._storage + (_partition++);
                    Value ki = pe->key;
                    if (value_is_null(ki) || _value_is_tombstone(ki))
                        continue;
                    Value vi = pe->entomb();
                    _alpha._count--;
                    assert(_beta._grace);
                    _beta.must_insert(value_hash(ki), ki, vi);
                    break;
                }
                return ultimate;
            }
            
            bool empty() const {
                return _alpha.empty();
            }
            
            size_t size() const {
                return _alpha._count + _beta._count;
            }
            
            bool contains(Value key) const {
                return !value_is_null(find(key));
            }
            
            Traced<Value>& find_or_insert_null(Value key) {
                std::size_t h = value_hash(key);
                if (_alpha._count) {
                    Entry* p = _alpha.pfind(h, key);
                    if (p)
                        return p->value;
                }
                if (_alpha._grace) {
                    
                }
                abort();
            }
            
            void clear() {
                _alpha.clear();
                _beta.clear();
                _partition = 0;
            }
            
            HeapTable() {}
            virtual ~HeapTable() final = default;
            
            
            virtual bool _value_empty() const override {
                return empty();
            }
            
            virtual size_t _value_size() const override {
                return size();
            }
            
            virtual Value _value_find(Value key) const override {
                return find(key);
            }
            
            virtual Value _value_insert_or_assign(Value key, Value value) override {
                return insert_or_assign(key, value);
            }
            
            virtual bool _value_contains(Value key) const override {
                return contains(key);
            }
            
            virtual Value _value_erase(Value key) override {
                return erase(key);
            }
            
            virtual void _object_scan() const override {
                object_trace(_alpha._storage);
                object_trace(_beta._storage);
            }
            
        }; // struct HeapTable
        
    } // namespace legacy
    
#endif
    
} // namespace wry::gc

// This fundamental table knows very little about its entries and offers
// few amenities.  It provides direct access to Entries and relies on the
// user to update the entry in-place and inform it of the state transition

// At this basic level, we require
//     Entry()
//     Entry::operator=(Entry&&);
//     entry_get_tag(const Entry&)
//     entry_get_hash(const Entry&);
//     entry_equality(const Entry&, const Keylike&, hash_t)
//     object_trace(const Entry&)

// We can use the default Basic? Raw?Entry when we can't map the EntryTag
// to special key bitpatterns, as we can with gc::Value and pointers

// Slightly unconventionally, we track "grace", the number of VACANT entries
// we can consume before we must resize, rather than comparing the number
// of OCCUPIED and TOMBSTONE entries to a load factor.

// The table will not resize itself, but provides machinery to assit in
// doing so.  Notably it resizes based on OCCUPIED, so if a table ends up
// in a state with many TOMBSTONES it may actually resize to a smaller
// capacity.

// TODO: if we resize on the basis of OCCUPANTS not CAPACITY we may not
// get enough headroom to finish copying over the old table before we fill
// up the new one.  This puts some limit on how much we can shrink vs. how
// much work we have to do per "taxed" operation.

// TODO: The Rust Entry isn't just the slot, it's a bundle of pointers to
// the slot and the table


// finds a place for a new key with the given hashcode
// preconditions:
//   - key is not already in the table (though a key with the same
//     hashcode might be)
//   - there are no tombstones in the table; never having perfomed
//     an erase guarantees this

#endif
