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
#include <utility>

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
    //
    // Dispatch model: each concrete node type overrides one `as_*` virtual
    // to return `this`; the base default returns nullptr.  Operations that
    // pattern-match on node type (find/find_or_emplace/erase/cleanParent)
    // are written as one function body using `if (auto* x = node->as_x())`,
    // mirroring the paper's pseudocode more closely than M-operations
    // ×N-types of double-dispatch virtuals.

    namespace _ctrie {

        template<typename K, typename V> struct AnyNode;
        template<typename K, typename V> struct MainNode;
        template<typename K, typename V> struct BranchNode;
        template<typename K, typename V> struct CNode;
        template<typename K, typename V> struct LNode;
        template<typename K, typename V> struct TNode;
        template<typename K, typename V> struct SNode;
        template<typename K, typename V> struct INode;

        template<typename V>
        struct FindResult {
            enum Tag { OK, RESTART, NOT_FOUND };
            Tag tag;
            V value;
        };

        template<typename V>
        struct FindOrEmplaceResult {
            enum Tag { OK, RESTART };
            Tag tag;
            V value;
        };

        enum class EraseResult {
            OK,
            RESTART,
            NOT_FOUND,
        };

        // Type-test virtuals.  Default returns nullptr; each concrete type
        // overrides one to return `this`.  All five live on AnyNode so the
        // result of LNode::find_or_copy_emplace (an SNode-or-LNode) can be
        // queried uniformly.  Static type discrimination (MainNode vs
        // BranchNode) is preserved in slot/array element types.
        template<typename K, typename V>
        struct AnyNode : GarbageCollected {
            virtual CNode<K,V> const* as_cnode() const { return nullptr; }
            virtual TNode<K,V> const* as_tnode() const { return nullptr; }
            virtual LNode<K,V> const* as_lnode() const { return nullptr; }
            virtual INode<K,V> const* as_inode() const { return nullptr; }
            virtual SNode<K,V> const* as_snode() const { return nullptr; }
        };

        // MainNode: anything that may live in INode::main (CNode/TNode/LNode).
        template<typename K, typename V>
        struct MainNode : AnyNode<K,V> {
            virtual void _garbage_collected_debug() const override {
                printf("MainNode\n");
            }
        };

        // BranchNode: anything that may live in CNode::array[] (INode/SNode).
        template<typename K, typename V>
        struct BranchNode : AnyNode<K,V> {
            virtual void _garbage_collected_debug() const override {
                printf("BranchNode\n");
            }
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

            FindResult<V>          find(K key,                int level, INode<K,V> const* parent) const;
            FindOrEmplaceResult<V> find_or_emplace(K key, V default_, int level, INode<K,V> const* parent) const;
            EraseResult            erase(K key,               int level, INode<K,V> const* parent) const;
            // TODO: erase by key vs erase by SNode identity

            MainNode<K,V> const* load() const;
            bool compare_exchange(MainNode<K,V> const* expected, MainNode<K,V> const* desired) const;

            INode<K,V> const* as_inode() const override { return this; }
            virtual void _garbage_collected_scan() const override;
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

            CNode<K,V> const* as_cnode() const override { return this; }
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

            SNode<K,V> const* as_snode() const override { return this; }
            virtual void _garbage_collected_scan() const override;
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

            FindResult<V> find(K key) const;
            // Returns either an SNode (key already present) or a fresh LNode
            // head (new entry prepended); caller dispatches via as_snode /
            // as_lnode.
            AnyNode<K,V> const* find_or_copy_emplace(K key, V default_) const;
            LNode<K,V> const* copy_erase(K key) const;
            LNode<K,V> const* copy_erase(LNode<K,V> const* victim) const;

            LNode<K,V> const* as_lnode() const override { return this; }
            virtual void _garbage_collected_scan() const override;
        };

        template<typename K, typename V>
        struct TNode final : MainNode<K,V> {

            virtual void _garbage_collected_debug() const override {
                printf("TNode\n");
            }

            SNode<K,V> const* sn;

            explicit TNode(SNode<K,V> const* sn);
            virtual ~TNode() override;

            TNode<K,V> const* as_tnode() const override { return this; }
            virtual void _garbage_collected_scan() const override;
        };

        constexpr int W = 6;

    } // namespace _ctrie

    template<typename K, typename V>
    struct Ctrie final : GarbageCollected {

        _ctrie::INode<K,V> const* root;

        Ctrie();
        virtual ~Ctrie() override final;

        std::optional<V> find(K key);
        V find_or_emplace(K key, V default_);
        void erase(K key);

        virtual void _garbage_collected_scan() const override;
        virtual void _garbage_collected_debug() const override;

    }; // struct Ctrie



    // ── Inline method definitions ─────────────────────────────────────────────

    namespace _ctrie {

        // cleanParent: parent INode `p` may still hold an entry (via the CNode
        // at p->main) referring to child INode `i` whose main has tombstoned.
        // Repair by replacing the CNode entry with the tomb's resurrected SNode
        // (and contracting the parent CNode if it now has only one slot).
        template<typename K, typename V>
        inline void cleanParent(INode<K,V> const* p, INode<K,V> const* i, size_t hc, int lev) {
            for (;;) {
                MainNode<K,V> const* m  = i->load();
                MainNode<K,V> const* pm = p->load();
                auto* cn = pm->as_cnode();
                if (!cn) return;                            // parent no longer a CNode
                auto [flag, pos] = CNode<K,V>::flagpos(hc, lev, cn->bmp);
                if (!(flag & cn->bmp)) return;              // slot already gone
                if (cn->array[pos] != i) return;            // slot points elsewhere
                auto* tn = m->as_tnode();
                if (!tn) return;                            // child no longer tombed
                if (p->compare_exchange(cn, cn->copy_assign(pos, tn->sn)->to_contracted(lev)))
                    return;                                 // installed
                // CAS lost: someone else moved the parent on; reload and retry.
            }
        }

        // INode -------------------------------------------------------------

        template<typename K, typename V>
        inline INode<K,V>::INode(MainNode<K,V> const* mn) : main(mn) {}

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
        inline void INode<K,V>::clean(int level) const {
            MainNode<K,V> const* mn = load();
            if (auto* cn = mn->as_cnode()) {
                // SAFETY: the result of to_compressed may be discarded if the
                // CAS loses; the GC retains it via the new-objects bag.
                compare_exchange(cn, cn->to_compressed(level));
            }
            // Anything else (TNode/LNode) needs no compression here.
        }

        template<typename K, typename V>
        inline FindResult<V> INode<K,V>::find(K key, int lev, INode<K,V> const* parent) const {
            MainNode<K,V> const* mn = load();

            if (auto* cn = mn->as_cnode()) {
                auto [flag, pos] = CNode<K,V>::flagpos(key.hash(), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return { FindResult<V>::NOT_FOUND, {} };
                BranchNode<K,V> const* bn = cn->array[pos];
                if (auto* in = bn->as_inode())
                    return in->find(key, lev + W, this);
                if (auto* sn = bn->as_snode())
                    return (key == sn->k)
                        ? FindResult<V>{ FindResult<V>::OK,        sn->v }
                        : FindResult<V>{ FindResult<V>::NOT_FOUND, {}    };
                std::unreachable();
            }

            if (mn->as_tnode()) {
                // Help compress the parent and ask the caller to retry.
                if (parent)
                    parent->clean(lev - W);
                return { FindResult<V>::RESTART, {} };
            }

            if (auto* ln = mn->as_lnode())
                return ln->find(key);

            std::unreachable();
        }

        template<typename K, typename V>
        inline FindOrEmplaceResult<V>
        INode<K,V>::find_or_emplace(K key, V default_, int lev, INode<K,V> const* parent) const {
            MainNode<K,V> const* mn = load();

            if (auto* cn = mn->as_cnode()) {
                auto [flag, pos] = CNode<K,V>::flagpos(key.hash(), lev, cn->bmp);
                if (!(flag & cn->bmp)) {
                    // Empty slot: install a fresh SNode.
                    SNode<K,V>* nsn = new SNode<K,V>(key, default_);
                    MainNode<K,V> const* nmn = cn->copy_insert(pos, flag, nsn);
                    return compare_exchange(mn, nmn)
                        ? FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::OK,      default_ }
                        : FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::RESTART, {}       };
                }
                BranchNode<K,V> const* bn = cn->array[pos];
                if (auto* in = bn->as_inode())
                    return in->find_or_emplace(key, default_, lev + W, this);
                if (auto* sn = bn->as_snode()) {
                    if (key == sn->k)
                        return { FindOrEmplaceResult<V>::OK, sn->v };
                    // Distinct key: expand the HAMT one level.  CNode::make
                    // bottoms out into an LNode chain if hash bits run out.
                    SNode<K,V>* nsn = new SNode<K,V>(key, default_);
                    INode<K,V>* nin = new INode<K,V>(CNode<K,V>::make(sn, nsn, lev + W));
                    MainNode<K,V> const* nmn = cn->copy_assign(pos, nin);
                    // SAFETY: if we lose the CAS race, the speculative INode
                    // and sub-MainNode become orphans; the collector picks
                    // them up via the thread-local new-objects bag.
                    return compare_exchange(mn, nmn)
                        ? FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::OK,      nsn->v }
                        : FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::RESTART, {}     };
                }
                std::unreachable();
            }

            if (mn->as_tnode()) {
                if (parent)
                    parent->clean(lev - W);
                return { FindOrEmplaceResult<V>::RESTART, {} };
            }

            if (auto* ln = mn->as_lnode()) {
                AnyNode<K,V> const* result = ln->find_or_copy_emplace(key, default_);
                if (auto* sn = result->as_snode()) {
                    // Key already present in the collision list.
                    return { FindOrEmplaceResult<V>::OK, sn->v };
                }
                if (auto* nln = result->as_lnode()) {
                    // New head: install via CAS.  nln->sn is the freshly-
                    // prepended SNode carrying our (key, default_).
                    return compare_exchange(ln, nln)
                        ? FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::OK,      nln->sn->v }
                        : FindOrEmplaceResult<V>{ FindOrEmplaceResult<V>::RESTART, {}         };
                }
                std::unreachable();
            }

            std::unreachable();
        }

        template<typename K, typename V>
        inline EraseResult INode<K,V>::erase(K key, int lev, INode<K,V> const* parent) const {
            MainNode<K,V> const* mn = load();

            if (auto* cn = mn->as_cnode()) {
                auto [flag, pos] = CNode<K,V>::flagpos(key.hash(), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return EraseResult::NOT_FOUND;
                BranchNode<K,V> const* bn = cn->array[pos];
                EraseResult result;
                if (auto* in = bn->as_inode()) {
                    result = in->erase(key, lev + W, this);
                } else if (auto* sn = bn->as_snode()) {
                    if (sn->k != key)
                        return EraseResult::NOT_FOUND;
                    // SAFETY: contracted result may be discarded on CAS loss;
                    // the GC retains it via the new-objects bag.
                    result = compare_exchange(cn, cn->copy_erase(pos, flag)->to_contracted(lev))
                        ? EraseResult::OK
                        : EraseResult::RESTART;
                } else {
                    std::unreachable();
                }
                // Post-erase: if our main is now a TNode and we have a parent,
                // try to absorb the tomb into the parent CNode.
                if (result == EraseResult::OK && parent) {
                    MainNode<K,V> const* mn2 = load();
                    if (mn2->as_tnode())
                        cleanParent(parent, this, key.hash(), lev - W);
                }
                return result;
            }

            if (mn->as_tnode()) {
                if (parent)
                    parent->clean(lev - W);
                return EraseResult::RESTART;
            }

            if (auto* ln = mn->as_lnode()) {
                LNode<K,V> const* head = ln->copy_erase(key);
                if (head == ln)
                    return EraseResult::NOT_FOUND;
                MainNode<K,V> const* desired = head;
                if (!head->next) // collapse single-element list to a tomb
                    desired = new TNode<K,V>(head->sn);
                // TODO: if EraseResult::OK should we try and clean up any tombstone?
                return compare_exchange(ln, desired)
                    ? EraseResult::OK
                    : EraseResult::RESTART;
            }

            std::unreachable();
        }

        template<typename K, typename V>
        inline void INode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(main);
        }

        // CNode -------------------------------------------------------------

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
        inline std::pair<uint64_t, int> CNode<K,V>::flagpos(uint64_t hash, int lev, uint64_t bmp) {
            uint64_t a = (hash >> lev) & 63;
            uint64_t flag = ((uint64_t)1 << a);
            int pos = __builtin_popcountll(bmp & (flag - 1));
            return {flag, pos};
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
            assert(dest == ncn->array + (num - 1));
            return ncn;
        }

        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::copy_insert(int pos, uint64_t flag, BranchNode<K,V> const* bn) const {
            assert(!(bmp & flag));
            int num = __builtin_popcountll(bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(bmp ^ flag);
            BranchNode<K,V> const* const* src = array;
            for (int i = 0; i != num + 1; ++i) {
                if (i != pos) {
                    ncn->array[i] = *src++;
                } else {
                    ncn->array[i] = bn;
                }
                garbage_collected_shade(ncn->array[i]);
            }
            assert(src == array + num);
            return ncn;
        }

        // resurrected: copy of this CNode where each I-over-T slot is replaced
        // by the tomb's SNode.  Other slots (live INodes, plain SNodes) are
        // copied as-is.
        template<typename K, typename V>
        inline CNode<K,V> const* CNode<K,V>::resurrected() const {
            int num = __builtin_popcountll(this->bmp);
            CNode<K,V>* ncn = CNode<K,V>::make_with_bitmap(this->bmp);
            for (int i = 0; i != num; ++i) {
                BranchNode<K,V> const* bn = this->array[i];
                if (auto* in = bn->as_inode()) {
                    MainNode<K,V> const* mn = in->load();
                    if (auto* tn = mn->as_tnode())
                        bn = tn->sn;
                }
                garbage_collected_shade(bn);
                ncn->array[i] = bn;
            }
            return ncn;
        }

        // to_contracted: if this CNode now holds a single SNode at the leaf,
        // collapse to a TNode so cleanParent can absorb us upward.  An INode
        // singleton or any non-leaf CNode is left alone.
        template<typename K, typename V>
        inline MainNode<K,V> const* CNode<K,V>::to_contracted(int level) const {
            if (level == 0)
                return this;
            int num = __builtin_popcountll(this->bmp);
            if (num != 1)
                return this;
            BranchNode<K,V> const* bn = this->array[0];
            if (auto* sn = bn->as_snode())
                return new TNode<K,V>(sn);
            return this;
        }

        template<typename K, typename V>
        [[nodiscard]] inline MainNode<K,V> const* CNode<K,V>::to_compressed(int level) const {
            return resurrected()->to_contracted(level);
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
                    std::unreachable();
            }
        }

        template<typename K, typename V>
        inline void CNode<K,V>::_garbage_collected_scan() const {
            int num = __builtin_popcountll(bmp);
            for (int i = 0; i != num; ++i)
                garbage_collected_scan(array[i]);
        }

        // SNode -------------------------------------------------------------

        template<typename K, typename V>
        inline SNode<K,V>::SNode(K key, V value) : k(key), v(value) {}

        template<typename K, typename V>
        inline SNode<K,V>::~SNode() {}

        template<typename K, typename V>
        inline void SNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(k);
            garbage_collected_scan(v);
        }

        // LNode -------------------------------------------------------------

        template<typename K, typename V>
        inline LNode<K,V>::LNode(SNode<K,V> const* a, LNode<K,V> const* b)
        : sn(a), next(b) {}

        template<typename K, typename V>
        inline LNode<K,V>::~LNode() {}

        template<typename K, typename V>
        inline FindResult<V> LNode<K,V>::find(K key) const {
            for (LNode<K,V> const* current = this; current; current = current->next) {
                SNode<K,V> const* s = current->sn;
                assert(s);
                if (key == s->k)
                    return { FindResult<V>::OK, s->v };
            }
            return { FindResult<V>::NOT_FOUND, {} };
        }

        template<typename K, typename V>
        inline AnyNode<K,V> const* LNode<K,V>::find_or_copy_emplace(K key, V default_) const {
            // Walk the collision list.  If we find the key return its SNode.
            // Otherwise prepend a freshly-allocated SNode and return the new
            // list head.
            for (LNode<K,V> const* current = this; current; current = current->next) {
                SNode<K,V> const* s = current->sn;
                assert(s);
                if (key == s->k)
                    return s;
            }
            return new LNode<K,V>(new SNode<K,V>(key, default_), this);
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
        inline LNode<K,V> const* LNode<K,V>::copy_erase(K key) const {
            abort();
        }

        template<typename K, typename V>
        inline void LNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
            garbage_collected_scan(next);
        }

        // TNode -------------------------------------------------------------

        template<typename K, typename V>
        inline TNode<K,V>::TNode(SNode<K,V> const* sn) : sn(sn) {}

        template<typename K, typename V>
        inline TNode<K,V>::~TNode() {}

        template<typename K, typename V>
        inline void TNode<K,V>::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
        }

    } // namespace _ctrie

    // ── Ctrie<K,V> ────────────────────────────────────────────────────────────

    template<typename K, typename V>
    inline Ctrie<K,V>::Ctrie()
    : root(new _ctrie::INode<K,V>(_ctrie::CNode<K,V>::make_with_count(0))) {
    }

    template<typename K, typename V>
    inline Ctrie<K,V>::~Ctrie() {
    }

    template<typename K, typename V>
    inline std::optional<V> Ctrie<K,V>::find(K key) {
        for (;;) {
            _ctrie::FindResult<V> a = root->find(key, 0, nullptr);
            switch (a.tag) {
                case _ctrie::FindResult<V>::OK:        return a.value;
                case _ctrie::FindResult<V>::NOT_FOUND: return std::nullopt;
                case _ctrie::FindResult<V>::RESTART:   break;
                default:                               std::unreachable();
            }
        }
    }

    template<typename K, typename V>
    inline V Ctrie<K,V>::find_or_emplace(K key, V default_) {
        for (;;) {
            _ctrie::FindOrEmplaceResult<V> r = root->find_or_emplace(key, default_, 0, nullptr);
            switch (r.tag) {
                case _ctrie::FindOrEmplaceResult<V>::OK:      return r.value;
                case _ctrie::FindOrEmplaceResult<V>::RESTART: break;
                default:                                      std::unreachable();
            }
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
