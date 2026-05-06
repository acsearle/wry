//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include <optional>
#include <cstring>

#include "garbage_collected.hpp"
#include "hash.hpp"
#include "utility.hpp"

namespace wry {

    // Concurrent hash array mapped trie.
    //
    // See core/docs/ctrie.md for the full design.
    //
    // Algorithm: Prokopec, Bagwell, Bronson, Odersky, "Concurrent Tries
    // with Efficient Non-Blocking Snapshots" (PPoPP 2012) -- our SNode /
    // LNode / TNode structure follows that paper, modulo their snapshot
    // machinery (GCAS / RDCSS / Gen) which we do not implement.
    //
    // K must provide:  size_t hash() const;  bool operator==(K const&) const;
    //                  garbage_collected_scan(K const&)  (ADL)
    // V must provide:  garbage_collected_scan(V const&)  (ADL)

    namespace _ctrie {

        template<typename K, typename V> struct AnyNode;
        template<typename K, typename V> struct MainNode;
        template<typename K, typename V> struct BranchNode;
        template<typename K, typename V> struct CNode;
        template<typename K, typename V> struct LNode;
        template<typename K, typename V> struct TNode;
        template<typename K, typename V> struct SNode;
        template<typename K, typename V> struct INode;

        enum class EraseResult {
            RESTART,
            OK,
            NOTFOUND,
        };

        // std::optional<V> is used internally to signal OK or RESTART.
        // The top-level loop retries on nullopt and always returns a value.


        template<typename K, typename V>
        struct AnyNode : GarbageCollected {
            virtual std::optional<V>
            _ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const;
        };

        template<typename K, typename V>
        struct BranchNode : AnyNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("BranchNode\n");
            }

            virtual EraseResult
            _ctrie_bn_erase(K key, int level, INode<K,V> const* in,
                            CNode<K,V> const* cn, int pos, uint64_t flag) const = 0;
            virtual std::optional<V>
            _ctrie_bn_find_or_emplace(K key, V default_, int level, INode<K,V> const* in,
                                      CNode<K,V> const* cn, int pos) const = 0;
            virtual BranchNode<K,V> const* _ctrie_bn_resurrect() const;
            virtual MainNode<K,V> const* _ctrie_bn_to_contracted(CNode<K,V> const* cn) const;
        };

        template<typename K, typename V>
        struct MainNode : AnyNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("MainNode\n");
            }

            virtual void _ctrie_mn_clean(int level, INode<K,V> const* parent) const;
            virtual bool _ctrie_mn_cleanParent(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev, MainNode<K,V> const* m) const;
            virtual bool _ctrie_mn_cleanParent2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev, CNode<K,V> const* cn, int pos) const;
            virtual EraseResult _ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const = 0;
            virtual void _ctrie_mn_erase2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev) const;
            virtual std::optional<V> _ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const = 0;
            virtual BranchNode<K,V> const* _ctrie_mn_resurrect(INode<K,V> const* i) const;
        };

        template<typename K, typename V>
        struct INode final : BranchNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("INode\n");
            }

            // Slot rather than bare Atomic so that CAS-replacement of
            // INode::main automatically Yuasa-shades the displaced
            // MainNode (CNode/TNode/LNode), preserving any in-flight
            // tracing.  See [garbage_collected.hpp:608] for the slot.
            mutable GarbageCollectedSlot<MainNode<K,V> const*> main;

            explicit INode(MainNode<K,V> const*);
            virtual ~INode() final = default;

            void clean(int lev) const;
            std::optional<V> find_or_emplace(K key, V default_, int level, INode<K,V> const* parent) const;
            // TODO: erase by key vs erase by SNode identity
            EraseResult erase(K key, int level, INode<K,V> const* parent) const;
            MainNode<K,V> const* load() const;
            bool compare_exchange(MainNode<K,V> const* expected, MainNode<K,V> const* desired) const;

            virtual void _garbage_collected_scan() const override;

            virtual BranchNode<K,V> const* _ctrie_bn_resurrect() const override;
            virtual std::optional<V> _ctrie_bn_find_or_emplace(K key, V default_, int level,
                                                                INode<K,V> const* in, CNode<K,V> const* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(K key, int level, INode<K,V> const* in,
                                                CNode<K,V> const* cn, int pos, uint64_t flag) const override;
        };

        template<typename K, typename V>
        struct CNode final : MainNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("CNode[bmp=%llx]\n", (unsigned long long)bmp);
            }

            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) CNode` inside `make()`.
            static void* operator new(size_t, void* ptr) noexcept { return ptr; }

            static CNode<K,V>* make_with_count(int count);
            static CNode<K,V>* make_with_bitmap(uint64_t bitmap);
            // Two-key constructor: returns a CNode normally, but at the bottom
            // of the trie (lev >= 60, where the next level would exhaust the
            // hash) returns an LNode collision list.  Either way, the result
            // is a MainNode and is wrapped in an INode by the caller.
            static MainNode<K,V> const* make(SNode<K,V> const* s1, SNode<K,V> const* s2, int lev);
            static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);

            uint64_t bmp;
#ifndef NDEBUG
            size_t _debug_size;
#endif
            BranchNode<K,V> const* array[]
#ifndef NDEBUG
                __counted_by(_debug_size)
#endif
                ;

            CNode();
            virtual ~CNode() override;

            CNode<K,V> const* copy_insert(int pos, uint64_t flag, BranchNode<K,V> const* bn) const;
            CNode<K,V> const* copy_assign(int pos, BranchNode<K,V> const* bn) const;
            CNode<K,V> const* copy_erase(int pos, uint64_t flag) const;
            CNode<K,V> const* resurrected() const;
            MainNode<K,V> const* to_compressed(int level) const;
            MainNode<K,V> const* to_contracted(int level) const;

            virtual void _ctrie_mn_clean(int level, INode<K,V> const* parent) const override;
            virtual bool _ctrie_mn_cleanParent(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev, MainNode<K,V> const* m) const override;
            virtual EraseResult _ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;
            virtual std::optional<V> _ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;

            virtual void _garbage_collected_scan() const override;
        };

        // SNode is the per-key leaf.
        template<typename K, typename V>
        struct SNode final : BranchNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("SNode\n");
            }

            K k;
            V v;

            explicit SNode(K, V);
            virtual ~SNode() override final;

            virtual void _garbage_collected_scan() const override;

            virtual std::optional<V> _ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const override;

            virtual MainNode<K,V> const* _ctrie_bn_to_contracted(CNode<K,V> const* cn) const override;
            virtual std::optional<V> _ctrie_bn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* i, CNode<K,V> const* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(K key, int lev, INode<K,V> const* i, CNode<K,V> const* cn, int pos, uint64_t flag) const override;
        };

        template<typename K, typename V>
        struct LNode final : MainNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("LNode\n");
            }

            SNode<K,V> const* sn;
            LNode<K,V> const* next;

            LNode(SNode<K,V> const* s, LNode<K,V> const* n);
            virtual ~LNode() override final;

            AnyNode<K,V> const* find_or_copy_emplace(K key, V default_) const;
            LNode<K,V> const* copy_erase(K key) const;
            LNode<K,V> const* copy_erase(LNode<K,V> const* victim) const;

            virtual void _garbage_collected_scan() const override;

            virtual std::optional<V> _ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const override;

            virtual std::optional<V> _ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;
            virtual EraseResult _ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;
        };

        template<typename K, typename V>
        struct TNode final : MainNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("TNode\n");
            }

            SNode<K,V> const* sn;

            explicit TNode(SNode<K,V> const* sn);
            virtual ~TNode() override;

            BranchNode<K,V> const* _ctrie_mn_resurrect(INode<K,V> const* i) const override;
            virtual bool _ctrie_mn_cleanParent2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev,
                                                CNode<K,V> const* cn, int pos) const override;
            virtual std::optional<V> _ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;
            virtual EraseResult _ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const override;
            virtual void _ctrie_mn_erase2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev) const override;

            virtual void _garbage_collected_scan() const override;
        };

        constexpr int W = 6;

    } // namespace _ctrie



    template<typename K, typename V>
    struct Ctrie final : GarbageCollected {

        _ctrie::INode<K,V> const* root;

        using FindResult = std::optional<V>;

        Ctrie();
        virtual ~Ctrie() override final;

        V find_or_emplace(K key, V default_);
        void erase(K key);

        virtual void _garbage_collected_scan() const override;
        virtual void _garbage_collected_debug() const override;

    }; // struct Ctrie



    // ── Inline method definitions ─────────────────────────────────────────────

    namespace _ctrie {

        template<typename K, typename V>
        inline void cleanParent(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev) {
            for (;;) {
                MainNode<K,V> const* m = i->load();
                MainNode<K,V> const* pm = p->load();
                if (pm->_ctrie_mn_cleanParent(p, i, hc, lev, m))
                    break;
            }
        }

        // TODO: Should be abstract?
        template<typename K, typename V>
        inline std::optional<V> AnyNode<K,V>::_ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const {
            abort();
        }

        template<typename K, typename V>
        inline void MainNode<K,V>::_ctrie_mn_clean(int level, INode<K,V> const* parent) const {
            // noop
        }

        template<typename K, typename V>
        inline bool MainNode<K,V>::_ctrie_mn_cleanParent(INode<K,V> const* parent, INode<K,V> const* in, size_t hc, int lev, MainNode<K,V> const* m) const {
            return true;
        }

        template<typename K, typename V>
        inline bool MainNode<K,V>::_ctrie_mn_cleanParent2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev, CNode<K,V> const* cn, int pos) const {
            return true;
        }

        template<typename K, typename V>
        inline void MainNode<K,V>::_ctrie_mn_erase2(INode<K,V> const* parent, INode<K,V> const* i, size_t hc, int lev) const {
            // noop
        }

        template<typename K, typename V>
        inline BranchNode<K,V> const* MainNode<K,V>::_ctrie_mn_resurrect(INode<K,V> const* i) const {
            return i;
        }

        template<typename K, typename V>
        inline BranchNode<K,V> const* BranchNode<K,V>::_ctrie_bn_resurrect() const {
            return this;
        }

        template<typename K, typename V>
        inline MainNode<K,V> const* BranchNode<K,V>::_ctrie_bn_to_contracted(CNode<K,V> const* cn) const {
            return cn;
        }


        template<typename K, typename V>
        inline CNode<K,V>* CNode<K,V>::make_with_count(int count) {
            size_t bytes = sizeof(CNode<K,V>) + count * sizeof(BranchNode<K,V>*);
            void* raw = GarbageCollected::operator new(bytes);
            std::memset(raw, 0, bytes);
            CNode<K,V>* result = new(raw) CNode<K,V>;
#ifndef NDEBUG
            result->_debug_size = count;
#endif
            return result;
        }

        template<typename K, typename V>
        inline CNode<K,V>* CNode<K,V>::make_with_bitmap(uint64_t bitmap) {
            int count = __builtin_popcountll(bitmap);
            CNode<K,V>* result = make_with_count(count);
            result->bmp = bitmap;
            return result;
        }

        template<typename K, typename V>
        inline CNode<K,V>::CNode()
        : bmp{0}
#ifndef NDEBUG
        , _debug_size{0}
#endif
        {
        }

        template<typename K, typename V>
        inline CNode<K,V>::~CNode() {
        }

        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::copy_assign(int pos, BranchNode<K,V> const* bn) const {
            int num = __builtin_popcountll(this->bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(this->bmp);
            for (int i = 0; i != num; ++i) {
                BranchNode<K,V> const* sub = (i == pos) ? bn : this->array[i];
                garbage_collected_shade(sub);
                ncn->array[i] = sub;
            }
            return ncn;
        }

        template<typename K, typename V>
        inline std::pair<uint64_t, int> CNode<K,V>::flagpos(uint64_t hash, int lev, uint64_t bmp) {
            uint64_t a = (hash >> lev) & 63;
            uint64_t flag = ((uint64_t)1 << a);
            int pos = __builtin_popcountll(bmp & (flag - 1));
            return {flag, pos};
        }

        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::resurrected() const {
            int num = __builtin_popcountll(this->bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(this->bmp);
            for (int i = 0; i != num; ++i) {
                BranchNode<K,V> const* bn = this->array[i]->_ctrie_bn_resurrect();
                garbage_collected_shade(bn);
                ncn->array[i] = bn;
            }
            return ncn;
        }

        template<typename K, typename V>
        inline MainNode<K,V> const* CNode<K,V>::to_contracted(int level) const {
            if (level == 0)
                return this;
            int num = __builtin_popcountll(this->bmp);
            if (num != 1)
                return this;
            BranchNode<K,V> const* bn = this->array[0];
            return bn->_ctrie_bn_to_contracted(this);
        }

        template<typename K, typename V>
        [[nodiscard]] inline MainNode<K,V> const* CNode<K,V>::to_compressed(int level) const {
            return resurrected()->to_contracted(level);
        }

        template<typename K, typename V>
        inline void CNode<K,V>::_ctrie_mn_clean(int level, INode<K,V> const* parent) const {
            // INode::clean() is only invoked in contexts where we are already
            // going to RESTART descending the tree, so we don't need to report
            // if this compare_exchange fails.
            // SAFETY: Static analysis correctly notes that we sometimes
            // discard the node returned by to_compressed.  This is only OK
            // because the garbage collector saves us.
            parent->compare_exchange(this, this->to_compressed(level));
        }

        template<typename K, typename V>
        inline bool CNode<K,V>::_ctrie_mn_cleanParent(INode<K,V> const* parent, INode<K,V> const* in, size_t hc, int lev, MainNode<K,V> const* m) const {
            // this == READ(p->main)
            //    m == READ(i->main)
            auto [flag, pos] = flagpos(hc, lev, bmp);
            if (!(flag & bmp))
                return true;
            if (array[pos] != in)
                return true;
            // INode parent -> CNode this -> INode in
            return m->_ctrie_mn_cleanParent2(parent, in, hc, lev, this, pos);
        }

        template<typename K, typename V>
        inline std::optional<V> CNode<K,V>::_ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const {
            CNode<K,V> const* cn = this;
            MainNode<K,V> const* mn = this;
            auto [flag, pos] = flagpos(key.hash(), lev, cn->bmp);
            if (!(cn->bmp & flag)) {
                SNode<K,V>* nsn = new SNode<K,V>(key, default_);
                MainNode<K,V> const* nmn = cn->copy_insert(pos, flag, nsn);
                return i->compare_exchange(mn, nmn) ? std::optional<V>{default_} : std::nullopt;
            }
            BranchNode<K,V> const* bn = cn->array[pos];
            return bn->_ctrie_bn_find_or_emplace(key, default_, lev, i, cn, pos);
        }

        template<typename K, typename V>
        inline void INode<K,V>::clean(int level) const {
            load()->_ctrie_mn_clean(level, this);
        }

        template<typename K, typename V>
        inline BranchNode<K,V> const* INode<K,V>::_ctrie_bn_resurrect() const {
            MainNode<K,V> const* mn = load();
            return mn->_ctrie_mn_resurrect(this);
        }

        template<typename K, typename V>
        inline std::optional<V> INode<K,V>::find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent) const {
            return load()->_ctrie_mn_find_or_emplace(key, default_, lev, parent, this);
        }

        template<typename K, typename V>
        inline TNode<K,V>::TNode(SNode<K,V> const* sn)
        : sn(sn) {
        }

        template<typename K, typename V>
        inline TNode<K,V>::~TNode() {
        }

        template<typename K, typename V>
        inline bool TNode<K,V>::_ctrie_mn_cleanParent2(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev, CNode<K,V> const* cn, int pos) const {
            return p->compare_exchange(cn, cn->copy_assign(pos, sn)->to_contracted(lev));
        }

        template<typename K, typename V>
        inline BranchNode<K,V> const* TNode<K,V>::_ctrie_mn_resurrect(INode<K,V> const*) const {
            return sn;
        }

        template<typename K, typename V>
        inline std::optional<V> TNode<K,V>::_ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const {
            if (parent)
                parent->clean(lev - W);
            return std::nullopt;
        }

        template<typename K, typename V>
        inline std::optional<V> LNode<K,V>::_ctrie_mn_find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent, INode<K,V> const* i) const {
            return this->find_or_copy_emplace(key, default_)->_ctrie_any_find_or_emplace2(i, this);
        }

        template<typename K, typename V>
        inline std::optional<V> LNode<K,V>::_ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const {
            // We are the head of a new LNode list created to contain the new SNode.
            MainNode<K,V> const* mn = ln;
            MainNode<K,V> const* nmn = this;
            // On success, return our value; on failure, return nullopt to restart.
            return in->compare_exchange(mn, nmn) ? std::optional<V>{sn->v} : std::nullopt;
        }

        template<typename K, typename V>
        inline std::optional<V> INode<K,V>::_ctrie_bn_find_or_emplace(K key, V default_, int lev,
                                                                       INode<K,V> const* i,
                                                                       CNode<K,V> const* cn,
                                                                       int pos) const {
            return find_or_emplace(key, default_, lev + W, i);
        }

        template<typename K, typename V>
        inline EraseResult INode<K,V>::erase(K key, int lev, INode<K,V> const* parent) const {
            return load()->_ctrie_mn_erase(key, lev, parent, this);
        }

        template<typename K, typename V>
        inline EraseResult CNode<K,V>::_ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const {
            auto [flag, pos] = flagpos(key.hash(), lev, this->bmp);
            if (!(flag & this->bmp))
                // Key is not present, so postcondition is satisfied
                return EraseResult::NOTFOUND;
            EraseResult result = this->array[pos]->_ctrie_bn_erase(key, lev, i, this, pos, flag);
            if ((result == EraseResult::OK) && parent)
                // Check if the erasure left a TNode to clean up
                i->load()->_ctrie_mn_erase2(parent, i, key.hash(), lev - W);
            return result;
        }

        template<typename K, typename V>
        inline EraseResult TNode<K,V>::_ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* i) const {
            if (parent)
                parent->clean(lev - W);
            return EraseResult::RESTART;
        }

        template<typename K, typename V>
        inline EraseResult LNode<K,V>::_ctrie_mn_erase(K key, int lev, INode<K,V> const* parent, INode<K,V> const* in) const {
            LNode<K,V> const* head = this->copy_erase(key);
            if (head == this)
                return EraseResult::NOTFOUND;
            MainNode<K,V> const* desired = head;
            if (!(head->next)) // if list contains only one element
                desired = new TNode<K,V>(head->sn);
            // TODO: if EraseResult::OK should we try and clean up any tombstone?
            return (in->compare_exchange(this, desired)
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }

        template<typename K, typename V>
        inline EraseResult INode<K,V>::_ctrie_bn_erase(K key, int lev, INode<K,V> const* i, CNode<K,V> const* cn, int pos, uint64_t flag) const {
            return this->erase(key, lev + W, i);
        }

        template<typename K, typename V>
        inline void TNode<K,V>::_ctrie_mn_erase2(INode<K,V> const* parent, INode<K,V> const* i, size_t hc, int lev) const {
            cleanParent(parent, i, hc, lev);
        }

        template<typename K, typename V>
        inline MainNode<K,V> const* CNode<K,V>::make(SNode<K,V> const* s1, SNode<K,V> const* s2, int lev) {

            // Hash bits exhausted: at lev = 60 we've consumed bits 0..59
            // of the 64-bit hash, leaving only 4 bits in the next chunk.
            // Two keys whose hashes match for 60 bits are a true
            // collision -- send them to an LNode chain.
            // TODO: Just use the last 4 bits
            if (lev >= 60) {
                // TODO: build LNode chain here.
                abort();
            }

            uint64_t pos1 = (s1->k.hash() >> lev) & 63;
            uint64_t pos2 = (s2->k.hash() >> lev) & 63;

            // Order by level-position so array[pos] indexes correctly via
            // flagpos.  An earlier version swapped on full hash, which is
            // wrong: the relative order of full hashes does not in general
            // match the relative order of any particular bit-chunk.
            if (pos2 < pos1) {
                std::swap(s1, s2);
                std::swap(pos1, pos2);
            }

            uint64_t flag1 = (uint64_t)1 << pos1;
            uint64_t flag2 = (uint64_t)1 << pos2;
            uint64_t bmp = flag1 | flag2;
            int num = __builtin_popcountll(bmp);

            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(bmp);

            switch (num) {
                case 1: {
                    // Both keys land in the same bucket at this level.
                    // Recurse one level deeper inside an INode wrapper.
                    ncn->array[0] = new INode<K,V>(CNode<K,V>::make(s1, s2, lev + W));
                    return ncn;
                }
                case 2: {
                    ncn->array[0] = s1;  // pos1 < pos2; lower position first
                    ncn->array[1] = s2;
                    return ncn;
                }
                default:
                    abort();
            }
        }

        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::copy_erase(int pos, uint64_t flag) const {
            assert(bmp & flag);
            int num = __builtin_popcountll(bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(bmp ^ flag);
            BranchNode<K,V> const** dest = ncn->array;
            for (int i = 0; i != num; ++i) {
                if (i != pos) {
                    garbage_collected_shade(array[i]);
                    *dest++ = array[i];
                }
            }
            assert(dest == ncn->array+(num-1));
            return ncn;
        }

        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::copy_insert(int pos, uint64_t flag, BranchNode<K,V> const* bn) const {
            assert(!(bmp & flag));
            int num = __builtin_popcountll(bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(bmp ^ flag);
            BranchNode<K,V> const* const* src = array;
            for (int i = 0; i != num+1; ++i) {
                if (i != pos) {
                    ncn->array[i] = *src++;
                } else {
                    ncn->array[i] = bn;
                }
                garbage_collected_shade(ncn->array[i]);
            }
            assert(src == array+num);
            return ncn;
        }

        template<typename K, typename V>
        inline INode<K,V>::INode(MainNode<K,V> const* mn)
        : main(mn) {
        }

        template<typename K, typename V>
        inline SNode<K,V>::SNode(K key, V value)
        : k(key)
        , v(value) {
        }

        template<typename K, typename V>
        inline SNode<K,V>::~SNode() {
        }

        template<typename K, typename V>
        inline LNode<K,V>::LNode(SNode<K,V> const* a, LNode<K,V> const* b)
        : sn(a)
        , next(b) {
        }

        template<typename K, typename V>
        inline LNode<K,V>::~LNode() {
        }

        // NOTE: The new list reverses the order of the elements before victim:
        //    as ++ [b] ++ cs  ->  reverse(as) ++ cs
        template<typename K, typename V>
        inline LNode<K,V> const* LNode<K,V>::copy_erase(LNode<K,V> const* victim) const {
            assert(victim);
            LNode<K,V> const* head = victim->next;
            for (LNode<K,V> const* curr = this; curr != victim; curr = curr->next) {
                assert(curr); // victim was not in the list
                head = new LNode<K,V>(curr->sn, head);
            }
            return head;
        }

        template<typename K, typename V>
        inline AnyNode<K,V> const* LNode<K,V>::find_or_copy_emplace(K key, V default_) const {
            // Walk the collision list.  If we find the key return its SNode.
            // Otherwise prepend a freshly-allocated SNode and return the new
            // list head.
            for (LNode<K,V> const* current = this; current; current = current->next) {
                SNode<K,V> const* s = current->sn;
                assert(s);
                if (key != s->k)
                    continue;
                return s;
            }
            // Key not present; prepend a fresh entry.
            return new LNode<K,V>(new SNode<K,V>(key, default_), this);
        }

        template<typename K, typename V>
        inline LNode<K,V> const* LNode<K,V>::copy_erase(K key) const {
            abort();
        }

        template<typename K, typename V>
        inline void CNode<K,V>::_garbage_collected_scan() const {
            int num = __builtin_popcountll(bmp);
            for (int i = 0; i != num; ++i)
                garbage_collected_scan(array[i]);
        }

        template<typename K, typename V>
        inline void INode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(main);
        }

        template<typename K, typename V>
        inline void LNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
            garbage_collected_scan(next);
        }

        template<typename K, typename V>
        inline void TNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
        }

        template<typename K, typename V>
        inline void SNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(k);
            garbage_collected_scan(v);
        }

        template<typename K, typename V>
        inline MainNode<K,V> const* INode<K,V>::load() const {
            return main.load_acquire();
        }

        template<typename K, typename V>
        inline bool INode<K,V>::compare_exchange(MainNode<K,V> const* expected, MainNode<K,V> const* desired) const {
            // Safety: we have already ACQUIRED the expected value.  The
            // slot's CAS internally upgrades to acq_rel and shades the
            // displaced MainNode (Yuasa); see GarbageCollectedSlot.
            return main.compare_exchange_strong_release_relaxed(expected, desired);
        }

        template<typename K, typename V>
        inline EraseResult SNode<K,V>::_ctrie_bn_erase(K key,
                                                       int lev,
                                                       INode<K,V> const* i,
                                                       CNode<K,V> const* cn,
                                                       int pos,
                                                       uint64_t flag) const {
            if (this->k != key)
                return EraseResult::NOTFOUND;
            // SAFETY: We sometimes discard the pointer to the contracted node;
            // this is OK because the garbage collector saves us.
            return (i->compare_exchange(cn, cn->copy_erase(pos, flag)->to_contracted(lev))
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }

        template<typename K, typename V>
        inline std::optional<V> SNode<K,V>::_ctrie_bn_find_or_emplace(K key, V default_, int lev,
                                                                       INode<K,V> const* i,
                                                                       CNode<K,V> const* cn,
                                                                       int pos) const {
            // We have hashed to the bucket holding this SNode.  Either the
            // key matches or we expand the HAMT one level deeper.
            if (key == this->k) {
                return this->v;
            }

            // Hash collision at this level (but distinct keys): expand one
            // level of HAMT.  CNode::make may itself bottom out into an
            // LNode collision list if hash bits run out.
            SNode<K,V>* nsn = new SNode<K,V>(key, default_);
            INode<K,V>* nbn = new INode<K,V>(CNode<K,V>::make(this, nsn, lev + W));
            MainNode<K,V> const* mn = cn;
            MainNode<K,V> const* nmn = cn->copy_assign(pos, nbn);
            // SAFETY: if we lose the CAS race, the speculative INode and
            // sub-MainNode we built become orphans; the collector will pick
            // them up via the thread-local new-objects bag.
            return i->compare_exchange(mn, nmn) ? std::optional<V>{nsn->v} : std::nullopt;
        }

        template<typename K, typename V>
        inline MainNode<K,V> const* SNode<K,V>::_ctrie_bn_to_contracted(CNode<K,V> const* cn) const {
            return new TNode<K,V>(this);
        }

        template<typename K, typename V>
        inline std::optional<V> SNode<K,V>::_ctrie_any_find_or_emplace2(INode<K,V> const* in, LNode<K,V> const* ln) const {
            return this->v;
        }

    } // namespace _ctrie

    template<typename K, typename V>
    inline Ctrie<K,V>::Ctrie()
    : root(new _ctrie::INode<K,V>(_ctrie::CNode<K,V>::make_with_count(0))) {
    }

    template<typename K, typename V>
    inline Ctrie<K,V>::~Ctrie() {
    }

    template<typename K, typename V>
    inline V Ctrie<K,V>::find_or_emplace(K key, V default_) {
        for (;;) {
            std::optional<V> result = root->find_or_emplace(key, default_, 0, nullptr);
            if (result)
                return *result;
        }
    }

    template<typename K, typename V>
    inline void Ctrie<K,V>::erase(K key) {
        while (root->erase(key, 0, nullptr) == _ctrie::EraseResult::RESTART)
            ;
    }

    template<typename K, typename V>
    inline void Ctrie<K,V>::_garbage_collected_scan() const {
        garbage_collected_scan(root);
    }

    template<typename K, typename V>
    inline void Ctrie<K,V>::_garbage_collected_debug() const {
        printf("Ctrie\n");
    }

} // namespace wry

#endif /* ctrie_hpp */
