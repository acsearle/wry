//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

//#include "adl.hpp"

#include <cstring>

#include "utility.hpp"
#include "ctrie.hpp"
#include "HeapString.hpp"
#include "test.hpp"


namespace wry {
    
    namespace _ctrie {
        
        struct CNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("CNode[bmp=%llx]\n", (unsigned long long)bmp);
            }

            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) CNode` inside `make()`.
            static void* operator new(size_t, void* ptr) noexcept { return ptr; }

            static CNode* make(int num);
            // Two-key constructor: returns a CNode normally, but at the bottom
            // of the trie (lev >= 60, where the next level would exhaust the
            // hash) returns an LNode collision list.  Either way, the result
            // is a MainNode and is wrapped in an INode by the caller.
            static const MainNode* make(const SNode* s1, const SNode* s2, int lev);
            static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);

            uint64_t bmp;
            size_t _debug_size;
            const BranchNode* array[] __counted_by(_debug_size);
            
            CNode();
            virtual ~CNode() override;
            
            const CNode* copy_insert(int pos, uint64_t flag, const BranchNode* bn) const;
            const CNode* copy_assign(int pos, const BranchNode* bn) const;
            const CNode* copy_erase(int pos, uint64_t flag) const;
            const CNode* resurrected() const;
            const MainNode* to_compressed(int level) const;
            const MainNode* to_contracted(int level) const;
            
            
            virtual void _ctrie_mn_clean(int level, const INode* parent) const override;
            virtual bool _ctrie_mn_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            
            virtual void _garbage_collected_scan() const override;
            
        };
        
        // SNode is the per-key leaf wrapper.  HeapString is not itself a
        // Branch -- every reference to a HeapString from inside the trie
        // goes through an SNode.  The atomic WeakState lives on this type
        // (Phase 1+).  See [core/docs/ctrie.md].
        struct SNode final : BranchNode {

            virtual void _garbage_collected_debug() const override {
                printf("SNode\n");
            }

            HeapString const* sn;

            explicit SNode(HeapString const* sn);
            virtual ~SNode() override final;

            virtual void _garbage_collected_scan() const override;

            virtual const HeapString* _ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const override;

            virtual const MainNode* _ctrie_bn_to_contracted(const CNode* cn) const override;
            virtual const HeapString* _ctrie_bn_find_or_emplace(Query query, int lev, const INode* i, const CNode* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(const HeapString* key, int lev, const INode* i, const CNode* cn, int pos, uint64_t flag) const override;
        };

        struct LNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("LNode\n");
            }


            SNode const* sn;
            LNode const* next;

            LNode(SNode const* s, LNode const* n);
            virtual ~LNode() override final;

            const AnyNode* find_or_copy_emplace(Query query) const;
            const LNode* copy_erase(const HeapString* key) const;
            const LNode* copy_erase(const LNode* victim) const;

            virtual void _garbage_collected_scan() const override;

            virtual const HeapString* _ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const override;

            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;

        };


        struct TNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("TNode\n");
            }


            SNode const* sn;

            explicit TNode(SNode const* sn);
            virtual ~TNode() override;

            const BranchNode* _ctrie_mn_resurrect(const INode* i) const override;
            virtual bool _ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev,
                                                const CNode* cn, int pos) const override;
            virtual HeapString const* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
            virtual void _ctrie_mn_erase2(const INode* p, const INode* i, size_t hc, int lev) const override;

            virtual void _garbage_collected_scan() const override;

        };
        
        
        constexpr int W = 6;
       
        
        bool operator==(const Query& left, const HeapString* right) {
            size_t n = left.view.size();
            return ((left.hash == right->_hash)
                    && (n == right->_size)
                    && !__builtin_memcmp(left.view.data(), right->_bytes, n)
                    );
        }
        
        void cleanParent(const INode* p, const INode* i, size_t hc, int lev) {
            for (;;) {
                const MainNode* m = i->load();
                const MainNode* pm = p->load();
                if (pm->_ctrie_mn_cleanParent(p, i, hc, lev, m))
                    break;
            }
        }
        
        
        const HeapString* make_HeapString_from_Query(Query query) {
            // assert(query.view.size() > 7);
            return HeapString::make(query.hash, query.view);
        }
        
        
        const HeapString* AnyNode::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
            abort();
        }

                
        void MainNode::_ctrie_mn_clean(int level, const INode *parent) const {
            // noop
        }
        
        bool MainNode::_ctrie_mn_cleanParent(const INode* parent, const INode* in, size_t hc, int lev, const MainNode* m) const {
            return true;
        }
        
        bool MainNode::_ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const {
            return true;
        }
        
        void MainNode::_ctrie_mn_erase2(const INode* parent, const INode* i, size_t hc, int lev) const {
            // noop
        }

        const BranchNode* MainNode::_ctrie_mn_resurrect(const INode* i) const {
            return i;
        }
        
        
        const BranchNode* BranchNode::_ctrie_bn_resurrect() const {
            return this;
        }
                
        const MainNode* BranchNode::_ctrie_bn_to_contracted(const CNode* cn) const {
            return cn;
        }


        CNode* CNode::make(int num) {
            size_t bytes = sizeof(CNode) + num * sizeof(BranchNode*);
            void* raw = GarbageCollected::operator new(bytes);
            std::memset(raw, 0, bytes);
            return new(raw) CNode;
        }

        CNode::CNode()
        : bmp{0}
        , _debug_size{0} {
        }
        
        CNode::~CNode() {
        }
        
        
        
        const CNode* CNode::copy_assign(int pos, const BranchNode *bn) const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = CNode::make(num);
            ncn->bmp = this->bmp;
            ncn->_debug_size = __builtin_popcountll(ncn->bmp);
            for (int i = 0; i != num; ++i) {
                const BranchNode* sub = (i == pos) ? bn : this->array[i];
                garbage_collected_shade(sub);
                ncn->array[i] = sub;
            }
            return ncn;
        }
        
        std::pair<uint64_t, int> CNode::flagpos(uint64_t hash, int lev, uint64_t bmp) {
            uint64_t a = (hash >> lev) & 63;
            uint64_t flag = ((uint64_t)1 << a);
            int pos = __builtin_popcountll(bmp & (flag - 1));
            return {flag, pos};
        }
        
        const CNode* CNode::resurrected() const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = CNode::make(num);
            ncn->bmp = this->bmp;
            ncn->_debug_size = __builtin_popcountll(ncn->bmp);
            for (int i = 0; i != num; ++i) {
                const BranchNode* bn = this->array[i]->_ctrie_bn_resurrect();
                garbage_collected_shade(bn);
                ncn->array[i] = bn;
            }
            return ncn;
        }
        
        const MainNode* CNode::to_contracted(int level) const {
            if (level == 0)
                return this;
            int num = __builtin_popcountll(this->bmp);
            if (num != 1)
                return this;
            const BranchNode* bn = this->array[0];
            return bn->_ctrie_bn_to_contracted(this);
        }
        
        [[nodiscard]] const MainNode* CNode::to_compressed(int level) const {
            return resurrected()->to_contracted(level);
        }
        
        
        void CNode::_ctrie_mn_clean(int level, const INode* parent) const {
            // INode::clean() is only invoked in contexts where we are already
            // going to RESTART descending the tree, so we don't need to report
            // if this compare_exchange fails
            // SAFETY: Static analysis correctly notes that we sometimes
            // discard the node returned by to_compressed.  This is only OK
            // because the garbage collector saves us.
            parent->compare_exchange(this, this->to_compressed(level));
        }
        
        bool CNode::_ctrie_mn_cleanParent(const INode* parent, const INode* in, size_t hc, int lev, const MainNode* m) const {
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
        
        const HeapString* CNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            const CNode* cn = this;
            const MainNode* mn = this;
            auto [flag, pos] = flagpos(query.hash, lev, cn->bmp);
            if (!(cn->bmp & flag)) {
                const HeapString* nhs = make_HeapString_from_Query(query);
                SNode* nsn = new SNode(nhs);
                const MainNode* nmn = cn->copy_insert(pos, flag, nsn);
                return i->compare_exchange(mn, nmn) ? nhs : nullptr;
            }
            const BranchNode* bn = cn->array[pos];
            return bn->_ctrie_bn_find_or_emplace(query, lev, i, cn, pos);
        }
        

        
        
        
        void INode::clean(int level) const {
            load()->_ctrie_mn_clean(level, this);
        }

        const BranchNode* INode::_ctrie_bn_resurrect() const {
            const MainNode* mn = load();
            return mn->_ctrie_mn_resurrect(this);
        }
        
        const HeapString* INode::find_or_emplace(Query query, int lev, const INode* parent) const {
            return load()->_ctrie_mn_find_or_emplace(query, lev, parent, this);
        }


        
        
        TNode::TNode(const SNode* sn)
        : sn(sn) {
        }

        TNode::~TNode() {
        }

        bool TNode::_ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const {
            return p->compare_exchange(cn,  cn->copy_assign(pos, sn)->to_contracted(lev));
        }

        const BranchNode* TNode::_ctrie_mn_resurrect(const INode*) const {
            return sn;
        }
                
        
        
        
        
        
        
       
        const HeapString* TNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            if (parent)
                parent->clean(lev - W);
            return nullptr;
        }
        
        const HeapString* LNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            return this->find_or_copy_emplace(query)->_ctrie_any_find_or_emplace2(i, this);
        }
                
        const HeapString* LNode::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
            // We are the head of a new LNode list created to contain the new SNode
            const MainNode* mn = ln;
            const MainNode* nmn = this;
            // Try and install the new list
            // On success, return our string
            // On failure, return null to indicate we start over
            return in->compare_exchange(mn, nmn) ? sn->sn : nullptr;
        }
        
        
        
        const HeapString* INode::_ctrie_bn_find_or_emplace(Query query, int lev,
                                                           const INode* i,
                                                           const CNode* cn,
                                                           int pos) const {
            return find_or_emplace(query, lev + W, i);
        }
        
        
        
        
        
        
        EraseResult INode::erase(const HeapString* key, int lev, const INode* parent) const {
            return load()->_ctrie_mn_erase(key, lev, parent, this);
        }
        
        EraseResult CNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
            auto [flag, pos] = flagpos(key->_hash, lev, this->bmp);
            if (!(flag & this->bmp))
                // Key is not present, so postcondition is satisfied
                return EraseResult::NOTFOUND;
            EraseResult result = this->array[pos]->_ctrie_bn_erase(key, lev, i, this, pos, flag);
            if ((result == EraseResult::OK) && parent)
                // Check if the erasure left a TNode to clean up
                i->load()->_ctrie_mn_erase2(parent, i, key->_hash, lev - W);
            return result;
        }
        
        EraseResult TNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
            if (parent)
                parent->clean(lev - W);
            return EraseResult::RESTART;
        }
        
        EraseResult LNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* in) const {
            const LNode* head = this->copy_erase(key);
            if (head == this)
                return EraseResult::NOTFOUND;
            const MainNode* desired = head;
            if (!(head->next)) // if list contains only one element
                desired = new TNode(head->sn);  // sn is SNode*
            // TODO: if EraseResult::OK should we try and clean up any tombstone?
            return (in->compare_exchange(this, desired)
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }
           
        
        EraseResult INode::_ctrie_bn_erase(const HeapString* key, int lev, const INode* i, const CNode* cn, int pos, uint64_t flag) const {
            return this->erase(key, lev + W, i);
        }
        
        
        
        void TNode::_ctrie_mn_erase2(const INode* parent, const INode* i, size_t hc, int lev) const {
            cleanParent(parent, i, hc, lev);
        }
        
        
        
        
        
        const MainNode* CNode::make(const SNode* s1, const SNode* s2, int lev) {

            // Hash bits exhausted: at lev = 60 we've consumed bits 0..59 of
            // the 64-bit hash, leaving only 4 bits in the next chunk.  Two
            // strings whose hashes match for 60 bits are a true collision
            // (or close enough to one that we'd bottom out almost
            // immediately) -- send them to an LNode chain.
            if (lev >= 60) {
                assert(s1->sn != s2->sn);
                return new LNode(s1, new LNode(s2, nullptr));
            }

            uint64_t pos1 = (s1->sn->_hash >> lev) & 63;
            uint64_t pos2 = (s2->sn->_hash >> lev) & 63;

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

            CNode* ncn = CNode::make(num);
            ncn->bmp = bmp;
            ncn->_debug_size = __builtin_popcountll(ncn->bmp);
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
        
        const CNode* CNode::copy_erase(int pos, uint64_t flag) const {
            assert(bmp & flag);
            int num = __builtin_popcountll(bmp);
            CNode* ncn = CNode::make(num-1);
            ncn->bmp = bmp ^ flag;
            ncn->_debug_size = __builtin_popcountll(ncn->bmp);
            const BranchNode** dest = ncn->array;
            for (int i = 0; i != num; ++i) {
                if (i != pos) {
                    garbage_collected_shade(array[i]);
                    *dest++ = array[i];
                }
            }
            assert(dest == ncn->array+(num-1));
            return ncn;
        }
        
        const CNode* CNode::copy_insert(int pos, uint64_t flag, const BranchNode* bn) const {
            assert(!(bmp & flag));
            int num = __builtin_popcountll(bmp);
            CNode* ncn = CNode::make(num+1);
            ncn->bmp = bmp ^ flag;
            ncn->_debug_size = __builtin_popcountll(ncn->bmp);
            const BranchNode* const* src = array;
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
        
        INode::INode(const MainNode* mn)
        : main(mn) {
        }

        SNode::SNode(const HeapString* s) : sn(s) {
        }

        SNode::~SNode() {
        }

        LNode::LNode(const SNode* a, const LNode* b) : sn(a), next(b) {
        }

        LNode::~LNode() {
        }
        
        
        const LNode* LNode::copy_erase(const LNode* victim) const {
            assert(victim);
            // Reuse any nodes after the victim
            const LNode* head = victim->next;
            // Copy any nodes before the victim
            for (const LNode* curr = this; curr != victim; curr = curr->next) {
                assert(curr); // <-- victim was not in the list!
                              // Make a copy
                //LNode* a = new LNode;
                //a->sn = curr->sn;
                // Push onto the list
                //a->next = exchange(head, a);
                head = new LNode(curr->sn, head);
            }
            return head;
        }
        
        

        const AnyNode* LNode::find_or_copy_emplace(Query query) const {

            // Walk the collision list.  If we find the key, return its
            // SNode (which dispatches via _ctrie_any_find_or_emplace2 to
            // the underlying HeapString).  Otherwise prepend a freshly-
            // allocated SNode wrapping a fresh HeapString and return the
            // new chain head.
            //
            // Phase 0: every reached SNode holds a strong reference, so
            // "found" implies "live".  Phase 4 will replace the early
            // return with a WeakSlot::lock() -- on GONE, fall through and
            // install a new entry instead, à la the "key was condemned"
            // path described in [core/docs/ctrie.md].

            for (const LNode* current = this; current; current = current->next) {
                const SNode* s = current->sn;
                assert(s);
                if (query != s->sn)
                    continue;
                return s;
            }

            // Key not present; prepend a fresh entry.
            const HeapString* nhs = make_HeapString_from_Query(query);
            return new LNode(new SNode(nhs), this);
        }
        
        
        const LNode* LNode::copy_erase(const HeapString* key) const {
            // TODO(ctrie.md Phase 2): collector-driven removal of GONE
            // entries.  Will splice (not copy) once `next` becomes atomic
            // and the WEAK_DECISION phase exists.  Currently unreachable.
            abort();
        }
        
        
        
        
        
        void CNode::_garbage_collected_scan() const {
            // Phase 0: strong-trace every entry, including SNode leaves.
            // This keeps interned strings alive as long as the trie holds
            // a reference, which is the conservative behaviour we want
            // before the WeakState protocol arrives in Phase 1+.
            int num = __builtin_popcountll(bmp);
            for (int i = 0; i != num; ++i)
                garbage_collected_scan(array[i]);
        }

        void INode::_garbage_collected_scan() const {
            garbage_collected_scan(main);
        }

        void LNode::_garbage_collected_scan() const {
            garbage_collected_scan(sn);     // SNode (strong; weak in Phase 1+)
            garbage_collected_scan(next);
        }

        void TNode::_garbage_collected_scan() const {
            garbage_collected_scan(sn);     // SNode (strong; weak in Phase 1+)
        }

        void SNode::_garbage_collected_scan() const {
            // Phase 0: strong trace on the underlying HeapString, so the
            // trie holds interned strings alive (belt-and-braces).  Phase
            // 1+ will switch this to weak via the WEAK_DECISION pass.
            garbage_collected_scan(sn);
        }
        
        
        const MainNode* INode::load() const {
            return main.load_acquire();
        }

        bool INode::compare_exchange(const MainNode* expected, const MainNode* desired) const {
            // Safety: we have already ACQUIRED the expected value.  The
            // slot's CAS internally upgrades to acq_rel and shades the
            // displaced MainNode (Yuasa); see GarbageCollectedSlot.
            return main.compare_exchange_strong_release_relaxed(expected, desired);
        }
        
        
        
    } // namespace _ctrie
    
    using namespace _ctrie;

    Ctrie::~Ctrie() {
    }
    
    Ctrie::Ctrie()
    : root(new INode(_ctrie::CNode::make(0))) {
    }
                    
    const HeapString* Ctrie::find_or_emplace(Query query) {
        for (;;) {
            const INode* r = this->root;
            const HeapString* result = r->find_or_emplace(query, 0, nullptr);
            if (result)
                return result;
        }
    }
    
    void Ctrie::erase(const HeapString* hs) {
        while (this->root->erase(hs, 0, nullptr) == EraseResult::RESTART)
            ;
    }

    void Ctrie::_garbage_collected_scan() const {
        garbage_collected_scan(root);
    }

    void Ctrie::_garbage_collected_debug() const {
        printf("Ctrie\n");
    }

    
    namespace _ctrie {

        EraseResult SNode::_ctrie_bn_erase(const HeapString* key,
                                           int lev,
                                           const INode* i,
                                           const CNode* cn,
                                           int pos,
                                           uint64_t flag) const {
            if (this->sn != key)
                return EraseResult::NOTFOUND;
            // SAFETY: We sometimes discard the pointer to the contracted node;
            // this is OK because the garbage collector saves us.
            return (i->compare_exchange(cn, cn->copy_erase(pos, flag)->to_contracted(lev))
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }

        const HeapString* SNode::_ctrie_bn_find_or_emplace(Query query, int lev,
                                                           const INode* i,
                                                           const CNode* cn,
                                                           int pos) const {
            // We have hashed to the bucket holding this SNode.  Either our
            // wrapped HeapString matches the query -- return it -- or we
            // expand the HAMT one level deeper.
            //
            // Phase 0: every reached SNode holds a strong reference, so an
            // exact match means "live".  Phase 4 will replace the early
            // return with a WeakSlot::lock() -- on GONE, fall through and
            // install a fresh SNode/HeapString instead.

            if (query == this->sn) {
                return this->sn;
            }

            // Hash collision at this level (but distinct keys): expand one
            // level of HAMT.  CNode::make may itself bottom out into an
            // LNode collision list if hash bits run out.
            const HeapString* nhs = make_HeapString_from_Query(query);
            SNode* nsn = new SNode(nhs);
            INode* nbn = new INode(CNode::make(this, nsn, lev + W));
            const MainNode* mn = cn;
            const MainNode* nmn = cn->copy_assign(pos, nbn);
            // SAFETY: if we lose the CAS race, the speculative INode and
            // sub-MainNode we built become orphans; the collector will pick
            // them up via the thread-local new-objects bag.
            return i->compare_exchange(mn, nmn) ? nhs : nullptr;
        }

        const MainNode* SNode::_ctrie_bn_to_contracted(const CNode* cn) const {
            return new TNode(this);
        }

        const HeapString* SNode::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
            return this->sn;
        }

    } // namespace _ctrie


    // Phase 0 sanity test.  The collector and worker threads are running
    // by the time tests are dispatched, and the worker threads are
    // mutator-pinned ([global_work_queue.cpp]), so allocation-and-shade
    // operations are well-defined here.
    define_test("ctrie") {
        Root<Ctrie*> trie(new Ctrie());

        auto query_for = [](std::string_view sv) -> Query {
            return Query{std::hash<std::string_view>()(sv), sv};
        };

        Root<HeapString const*> a(trie->find_or_emplace(query_for("hello, world")));
        Root<HeapString const*> b(trie->find_or_emplace(query_for("goodnight, moon")));
        assert(a._ptr != b._ptr);
        assert(a._ptr->as_string_view() == "hello, world");
        assert(b._ptr->as_string_view() == "goodnight, moon");

        // Lookup canonicalises: same content → same pointer.
        Root<HeapString const*> a2(trie->find_or_emplace(query_for("hello, world")));
        assert(a._ptr == a2._ptr);

        // Distinct keys with the same hash bucket at level 0 still
        // canonicalise correctly.  We can't easily force this without
        // collisions, so just exercise more keys to grow the trie.
        std::vector<Root<HeapString const*>> kept;
        for (int i = 0; i != 64; ++i) {
            char buf[24];
            int n = snprintf(buf, sizeof(buf), "key-%d", i);
            kept.emplace_back(trie->find_or_emplace(query_for(std::string_view(buf, n))));
        }
        for (int i = 0; i != 64; ++i) {
            char buf[24];
            int n = snprintf(buf, sizeof(buf), "key-%d", i);
            Root<HeapString const*> again(trie->find_or_emplace(query_for(std::string_view(buf, n))));
            assert(again._ptr == kept[i]._ptr);
        }

        co_return;
    };

} // namespace wry

