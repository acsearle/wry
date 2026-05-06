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

    namespace _ctrie {

        struct AnyNode;
        struct MainNode;
        struct BranchNode;
        struct CNode;
        struct LNode;
        struct TNode;
        struct SNode;
        struct INode;

        // TODO: turn into generic parameters
        struct KeyType {
            std::size_t data;
            std::size_t hash() const { return wry::hash(data); }
            bool operator==(KeyType const&) const = default;
        };

        inline void garbage_collected_scan(KeyType const& k) {}

        struct ValueType {
            std::size_t data;
            bool operator==(ValueType const&) const = default;
        };

        inline void garbage_collected_scan(ValueType const& k) {}

        enum class EraseResult {
            RESTART,
            OK,
            NOTFOUND,
        };

        // FindOrEmplaceResult is used internally to signal OK or RESTART.
        // The top-level loop retries on nullopt and always returns a value.
        using FindOrEmplaceResult = std::optional<ValueType>;


        struct AnyNode : GarbageCollected {
            virtual FindOrEmplaceResult
            _ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const;
        };

        struct BranchNode : AnyNode {

            virtual void _garbage_collected_debug() const override {
                printf("BranchNode\n");
            }

            virtual EraseResult
            _ctrie_bn_erase(KeyType key, int level, INode const* in,
                            CNode const* cn, int pos, uint64_t flag) const = 0;
            virtual FindOrEmplaceResult
            _ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int level, INode const* in,
                                      CNode const* cn, int pos) const = 0;
            virtual BranchNode const* _ctrie_bn_resurrect() const;
            virtual MainNode const* _ctrie_bn_to_contracted(CNode const* cn) const;
        };

        struct MainNode : AnyNode {

            virtual void _garbage_collected_debug() const override {
                printf("MainNode\n");
            }

            virtual void _ctrie_mn_clean(int level, INode const* parent) const;
            virtual bool _ctrie_mn_cleanParent(INode const* p, INode const* i, size_t hc, int lev, MainNode const* m) const;
            virtual bool _ctrie_mn_cleanParent2(INode const* p, INode const* i, size_t hc, int lev, CNode const* cn, int pos) const;
            virtual EraseResult _ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const = 0;
            virtual void _ctrie_mn_erase2(INode const* p, INode const* i, size_t hc, int lev) const;
            virtual FindOrEmplaceResult _ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const = 0;
            virtual BranchNode const* _ctrie_mn_resurrect(INode const* i) const;
        };

        struct INode final : BranchNode {

            virtual void _garbage_collected_debug() const override {
                printf("INode\n");
            }

            // Slot rather than bare Atomic so that CAS-replacement of
            // INode::main automatically Yuasa-shades the displaced
            // MainNode (CNode/TNode/LNode), preserving any in-flight
            // tracing.  See [garbage_collected.hpp:608] for the slot.
            mutable GarbageCollectedSlot<MainNode const*> main;

            explicit INode(MainNode const*);
            virtual ~INode() final = default;

            void clean(int lev) const;
            FindOrEmplaceResult find_or_emplace(KeyType key, ValueType default_, int level, INode const* parent) const;
            // TODO: erase by key vs erase by SNode identity
            EraseResult erase(KeyType key, int level, INode const* parent) const;
            MainNode const* load() const;
            bool compare_exchange(MainNode const* expected, MainNode const* desired) const;

            virtual void _garbage_collected_scan() const override;

            virtual BranchNode const* _ctrie_bn_resurrect() const override;
            virtual FindOrEmplaceResult _ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int level,
                                                                   INode const* in, CNode const* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(KeyType key, int level, INode const* in,
                                                CNode const* cn, int pos, uint64_t flag) const override;
        };

        struct CNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("CNode[bmp=%llx]\n", (unsigned long long)bmp);
            }

            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) CNode` inside `make()`.
            static void* operator new(size_t, void* ptr) noexcept { return ptr; }

            static CNode* make_with_count(int count);
            static CNode* make_with_bitmap(uint64_t bitmap);
            // Two-key constructor: returns a CNode normally, but at the bottom
            // of the trie (lev >= 60, where the next level would exhaust the
            // hash) returns an LNode collision list.  Either way, the result
            // is a MainNode and is wrapped in an INode by the caller.
            static MainNode const* make(SNode const* s1, SNode const* s2, int lev);
            static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);

            uint64_t bmp;
#ifndef NDEBUG
            size_t _debug_size;
#endif
            BranchNode const* array[]
#ifndef NDEBUG
                __counted_by(_debug_size)
#endif
                ;

            CNode();
            virtual ~CNode() override;

            CNode const* copy_insert(int pos, uint64_t flag, BranchNode const* bn) const;
            CNode const* copy_assign(int pos, BranchNode const* bn) const;
            CNode const* copy_erase(int pos, uint64_t flag) const;
            CNode const* resurrected() const;
            MainNode const* to_compressed(int level) const;
            MainNode const* to_contracted(int level) const;

            virtual void _ctrie_mn_clean(int level, INode const* parent) const override;
            virtual bool _ctrie_mn_cleanParent(INode const* p, INode const* i, size_t hc, int lev, MainNode const* m) const override;
            virtual EraseResult _ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const override;
            virtual FindOrEmplaceResult _ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const override;

            virtual void _garbage_collected_scan() const override;
        };

        // SNode is the per-key leaf.
        struct SNode final : BranchNode {

            virtual void _garbage_collected_debug() const override {
                printf("SNode\n");
            }

            KeyType k;
            ValueType v;

            explicit SNode(KeyType, ValueType);
            virtual ~SNode() override final;

            virtual void _garbage_collected_scan() const override;

            virtual FindOrEmplaceResult _ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const override;

            virtual MainNode const* _ctrie_bn_to_contracted(CNode const* cn) const override;
            virtual FindOrEmplaceResult _ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* i, CNode const* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(KeyType key, int lev, INode const* i, CNode const* cn, int pos, uint64_t flag) const override;
        };

        struct LNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("LNode\n");
            }

            SNode const* sn;
            LNode const* next;

            LNode(SNode const* s, LNode const* n);
            virtual ~LNode() override final;

            AnyNode const* find_or_copy_emplace(KeyType key, ValueType default_) const;
            LNode const* copy_erase(KeyType key) const;
            LNode const* copy_erase(LNode const* victim) const;

            virtual void _garbage_collected_scan() const override;

            virtual FindOrEmplaceResult _ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const override;

            virtual FindOrEmplaceResult _ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const override;
            virtual EraseResult _ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const override;
        };

        struct TNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("TNode\n");
            }

            SNode const* sn;

            explicit TNode(SNode const* sn);
            virtual ~TNode() override;

            BranchNode const* _ctrie_mn_resurrect(INode const* i) const override;
            virtual bool _ctrie_mn_cleanParent2(INode const* p, INode const* i, size_t hc, int lev,
                                                CNode const* cn, int pos) const override;
            virtual FindOrEmplaceResult _ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const override;
            virtual EraseResult _ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const override;
            virtual void _ctrie_mn_erase2(INode const* p, INode const* i, size_t hc, int lev) const override;

            virtual void _garbage_collected_scan() const override;
        };

        constexpr int W = 6;

    } // namespace _ctrie



    struct Ctrie final : GarbageCollected {

        _ctrie::INode const* root;

        Ctrie();
        virtual ~Ctrie() override final;

        _ctrie::ValueType find_or_emplace(_ctrie::KeyType key, _ctrie::ValueType default_);
        void erase(_ctrie::KeyType key);

        virtual void _garbage_collected_scan() const override;
        virtual void _garbage_collected_debug() const override;

    }; // struct Ctrie



    // ── Inline method definitions ─────────────────────────────────────────────

    namespace _ctrie {

        inline void cleanParent(INode const* p, INode const* i, size_t hc, int lev) {
            for (;;) {
                MainNode const* m = i->load();
                MainNode const* pm = p->load();
                if (pm->_ctrie_mn_cleanParent(p, i, hc, lev, m))
                    break;
            }
        }

        // TODO: Should be abstract?
        inline FindOrEmplaceResult AnyNode::_ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const {
            abort();
        }

        inline void MainNode::_ctrie_mn_clean(int level, INode const* parent) const {
            // noop
        }

        inline bool MainNode::_ctrie_mn_cleanParent(INode const* parent, INode const* in, size_t hc, int lev, MainNode const* m) const {
            return true;
        }

        inline bool MainNode::_ctrie_mn_cleanParent2(INode const* p, INode const* i, size_t hc, int lev, CNode const* cn, int pos) const {
            return true;
        }

        inline void MainNode::_ctrie_mn_erase2(INode const* parent, INode const* i, size_t hc, int lev) const {
            // noop
        }

        inline BranchNode const* MainNode::_ctrie_mn_resurrect(INode const* i) const {
            return i;
        }

        inline BranchNode const* BranchNode::_ctrie_bn_resurrect() const {
            return this;
        }

        inline MainNode const* BranchNode::_ctrie_bn_to_contracted(CNode const* cn) const {
            return cn;
        }


        inline CNode* CNode::make_with_count(int count) {
            size_t bytes = sizeof(CNode) + count * sizeof(BranchNode*);
            void* raw = GarbageCollected::operator new(bytes);
            std::memset(raw, 0, bytes);
            CNode* result = new(raw) CNode;
#ifndef NDEBUG
            result->_debug_size = count;
#endif
            return result;
        }

        inline CNode* CNode::make_with_bitmap(uint64_t bitmap) {
            int count = __builtin_popcountll(bitmap);
            CNode* result = make_with_count(count);
            result->bmp = bitmap;
            return result;
        }

        inline CNode::CNode()
        : bmp{0}
#ifndef NDEBUG
        , _debug_size{0}
#endif
        {
        }

        inline CNode::~CNode() {
        }

        inline CNode const* CNode::copy_assign(int pos, BranchNode const* bn) const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = CNode::make_with_bitmap(this->bmp);
            for (int i = 0; i != num; ++i) {
                BranchNode const* sub = (i == pos) ? bn : this->array[i];
                garbage_collected_shade(sub);
                ncn->array[i] = sub;
            }
            return ncn;
        }

        inline std::pair<uint64_t, int> CNode::flagpos(uint64_t hash, int lev, uint64_t bmp) {
            uint64_t a = (hash >> lev) & 63;
            uint64_t flag = ((uint64_t)1 << a);
            int pos = __builtin_popcountll(bmp & (flag - 1));
            return {flag, pos};
        }

        inline CNode const* CNode::resurrected() const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = CNode::make_with_bitmap(this->bmp);
            for (int i = 0; i != num; ++i) {
                BranchNode const* bn = this->array[i]->_ctrie_bn_resurrect();
                garbage_collected_shade(bn);
                ncn->array[i] = bn;
            }
            return ncn;
        }

        inline MainNode const* CNode::to_contracted(int level) const {
            if (level == 0)
                return this;
            int num = __builtin_popcountll(this->bmp);
            if (num != 1)
                return this;
            BranchNode const* bn = this->array[0];
            return bn->_ctrie_bn_to_contracted(this);
        }

        [[nodiscard]] inline MainNode const* CNode::to_compressed(int level) const {
            return resurrected()->to_contracted(level);
        }

        inline void CNode::_ctrie_mn_clean(int level, INode const* parent) const {
            // INode::clean() is only invoked in contexts where we are already
            // going to RESTART descending the tree, so we don't need to report
            // if this compare_exchange fails
            // SAFETY: Static analysis correctly notes that we sometimes
            // discard the node returned by to_compressed.  This is only OK
            // because the garbage collector saves us.
            parent->compare_exchange(this, this->to_compressed(level));
        }

        inline bool CNode::_ctrie_mn_cleanParent(INode const* parent, INode const* in, size_t hc, int lev, MainNode const* m) const {
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

        inline FindOrEmplaceResult CNode::_ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const {
            CNode const* cn = this;
            MainNode const* mn = this;
            auto [flag, pos] = flagpos(key.hash(), lev, cn->bmp);
            if (!(cn->bmp & flag)) {
                SNode* nsn = new SNode(key, default_);
                MainNode const* nmn = cn->copy_insert(pos, flag, nsn);
                return i->compare_exchange(mn, nmn) ? FindOrEmplaceResult{default_} : std::nullopt;
            }
            BranchNode const* bn = cn->array[pos];
            return bn->_ctrie_bn_find_or_emplace(key, default_, lev, i, cn, pos);
        }

        inline void INode::clean(int level) const {
            load()->_ctrie_mn_clean(level, this);
        }

        inline BranchNode const* INode::_ctrie_bn_resurrect() const {
            MainNode const* mn = load();
            return mn->_ctrie_mn_resurrect(this);
        }

        inline FindOrEmplaceResult INode::find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent) const {
            return load()->_ctrie_mn_find_or_emplace(key, default_, lev, parent, this);
        }

        inline TNode::TNode(SNode const* sn)
        : sn(sn) {
        }

        inline TNode::~TNode() {
        }

        inline bool TNode::_ctrie_mn_cleanParent2(INode const* p, INode const* i, size_t hc, int lev, CNode const* cn, int pos) const {
            return p->compare_exchange(cn, cn->copy_assign(pos, sn)->to_contracted(lev));
        }

        inline BranchNode const* TNode::_ctrie_mn_resurrect(INode const*) const {
            return sn;
        }

        inline FindOrEmplaceResult TNode::_ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const {
            if (parent)
                parent->clean(lev - W);
            return std::nullopt;
        }

        inline FindOrEmplaceResult LNode::_ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, INode const* parent, INode const* i) const {
            return this->find_or_copy_emplace(key, default_)->_ctrie_any_find_or_emplace2(i, this);
        }

        inline FindOrEmplaceResult LNode::_ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const {
            // We are the head of a new LNode list created to contain the new SNode.
            MainNode const* mn = ln;
            MainNode const* nmn = this;
            // On success, return our value; on failure, return nullopt to restart.
            return in->compare_exchange(mn, nmn) ? FindOrEmplaceResult{sn->v} : std::nullopt;
        }

        inline FindOrEmplaceResult INode::_ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int lev,
                                                                     INode const* i,
                                                                     CNode const* cn,
                                                                     int pos) const {
            return find_or_emplace(key, default_, lev + W, i);
        }

        inline EraseResult INode::erase(KeyType key, int lev, INode const* parent) const {
            return load()->_ctrie_mn_erase(key, lev, parent, this);
        }

        inline EraseResult CNode::_ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const {
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

        inline EraseResult TNode::_ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* i) const {
            if (parent)
                parent->clean(lev - W);
            return EraseResult::RESTART;
        }

        inline EraseResult LNode::_ctrie_mn_erase(KeyType key, int lev, INode const* parent, INode const* in) const {
            LNode const* head = this->copy_erase(key);
            if (head == this)
                return EraseResult::NOTFOUND;
            MainNode const* desired = head;
            if (!(head->next)) // if list contains only one element
                desired = new TNode(head->sn);
            // TODO: if EraseResult::OK should we try and clean up any tombstone?
            return (in->compare_exchange(this, desired)
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }

        inline EraseResult INode::_ctrie_bn_erase(KeyType key, int lev, INode const* i, CNode const* cn, int pos, uint64_t flag) const {
            return this->erase(key, lev + W, i);
        }

        inline void TNode::_ctrie_mn_erase2(INode const* parent, INode const* i, size_t hc, int lev) const {
            cleanParent(parent, i, hc, lev);
        }

        inline MainNode const* CNode::make(SNode const* s1, SNode const* s2, int lev) {

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

            CNode* ncn = CNode::make_with_bitmap(bmp);

            switch (num) {
                case 1: {
                    // Both keys land in the same bucket at this level.
                    // Recurse one level deeper inside an INode wrapper.
                    ncn->array[0] = new INode(CNode::make(s1, s2, lev + W));
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

        inline CNode const* CNode::copy_erase(int pos, uint64_t flag) const {
            assert(bmp & flag);
            int num = __builtin_popcountll(bmp);
            CNode* ncn = CNode::make_with_bitmap(bmp ^ flag);
            BranchNode const** dest = ncn->array;
            for (int i = 0; i != num; ++i) {
                if (i != pos) {
                    garbage_collected_shade(array[i]);
                    *dest++ = array[i];
                }
            }
            assert(dest == ncn->array+(num-1));
            return ncn;
        }

        inline CNode const* CNode::copy_insert(int pos, uint64_t flag, BranchNode const* bn) const {
            assert(!(bmp & flag));
            int num = __builtin_popcountll(bmp);
            CNode* ncn = CNode::make_with_bitmap(bmp ^ flag);
            BranchNode const* const* src = array;
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

        inline INode::INode(MainNode const* mn)
        : main(mn) {
        }

        inline SNode::SNode(KeyType key, ValueType value)
        : k(key)
        , v(value) {
        }

        inline SNode::~SNode() {
        }

        inline LNode::LNode(SNode const* a, LNode const* b)
        : sn(a)
        , next(b) {
        }

        inline LNode::~LNode() {
        }

        // NOTE: The new list reverses the order of the elements before victim:
        //    as ++ [b] ++ cs  ->  reverse(as) ++ cs
        inline LNode const* LNode::copy_erase(LNode const* victim) const {
            assert(victim);
            LNode const* head = victim->next;
            for (LNode const* curr = this; curr != victim; curr = curr->next) {
                assert(curr); // victim was not in the list
                head = new LNode(curr->sn, head);
            }
            return head;
        }

        inline AnyNode const* LNode::find_or_copy_emplace(KeyType key, ValueType default_) const {
            // Walk the collision list.  If we find the key return its SNode.
            // Otherwise prepend a freshly-allocated SNode and return the new
            // list head.
            for (LNode const* current = this; current; current = current->next) {
                SNode const* s = current->sn;
                assert(s);
                if (key != s->k)
                    continue;
                return s;
            }
            // Key not present; prepend a fresh entry.
            return new LNode(new SNode(key, default_), this);
        }

        inline LNode const* LNode::copy_erase(KeyType key) const {
            abort();
        }

        inline void CNode::_garbage_collected_scan() const {
            int num = __builtin_popcountll(bmp);
            for (int i = 0; i != num; ++i)
                garbage_collected_scan(array[i]);
        }

        inline void INode::_garbage_collected_scan() const {
            garbage_collected_scan(main);
        }

        inline void LNode::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
            garbage_collected_scan(next);
        }

        inline void TNode::_garbage_collected_scan() const {
            garbage_collected_scan(sn);
        }

        inline void SNode::_garbage_collected_scan() const {
            garbage_collected_scan(k);
            garbage_collected_scan(v);
        }

        inline MainNode const* INode::load() const {
            return main.load_acquire();
        }

        inline bool INode::compare_exchange(MainNode const* expected, MainNode const* desired) const {
            // Safety: we have already ACQUIRED the expected value.  The
            // slot's CAS internally upgrades to acq_rel and shades the
            // displaced MainNode (Yuasa); see GarbageCollectedSlot.
            return main.compare_exchange_strong_release_relaxed(expected, desired);
        }

        inline EraseResult SNode::_ctrie_bn_erase(KeyType key,
                                                   int lev,
                                                   INode const* i,
                                                   CNode const* cn,
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

        inline FindOrEmplaceResult SNode::_ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int lev,
                                                                     INode const* i,
                                                                     CNode const* cn,
                                                                     int pos) const {
            // We have hashed to the bucket holding this SNode.  Either the
            // key matches or we expand the HAMT one level deeper.
            if (key == this->k) {
                return this->v;
            }

            // Hash collision at this level (but distinct keys): expand one
            // level of HAMT.  CNode::make may itself bottom out into an
            // LNode collision list if hash bits run out.
            SNode* nsn = new SNode(key, default_);
            INode* nbn = new INode(CNode::make(this, nsn, lev + W));
            MainNode const* mn = cn;
            MainNode const* nmn = cn->copy_assign(pos, nbn);
            // SAFETY: if we lose the CAS race, the speculative INode and
            // sub-MainNode we built become orphans; the collector will pick
            // them up via the thread-local new-objects bag.
            return i->compare_exchange(mn, nmn) ? FindOrEmplaceResult{nsn->v} : std::nullopt;
        }

        inline MainNode const* SNode::_ctrie_bn_to_contracted(CNode const* cn) const {
            return new TNode(this);
        }

        inline FindOrEmplaceResult SNode::_ctrie_any_find_or_emplace2(INode const* in, LNode const* ln) const {
            return this->v;
        }

    } // namespace _ctrie

    inline Ctrie::Ctrie()
    : root(new _ctrie::INode(_ctrie::CNode::make_with_count(0))) {
    }

    inline Ctrie::~Ctrie() {
    }

    inline _ctrie::ValueType Ctrie::find_or_emplace(_ctrie::KeyType key, _ctrie::ValueType default_) {
        for (;;) {
            _ctrie::FindOrEmplaceResult result = root->find_or_emplace(key, default_, 0, nullptr);
            if (result)
                return *result;
        }
    }

    inline void Ctrie::erase(_ctrie::KeyType key) {
        while (root->erase(key, 0, nullptr) == _ctrie::EraseResult::RESTART)
            ;
    }

    inline void Ctrie::_garbage_collected_scan() const {
        garbage_collected_scan(root);
    }

    inline void Ctrie::_garbage_collected_debug() const {
        printf("Ctrie\n");
    }

} // namespace wry

#endif /* ctrie_hpp */
