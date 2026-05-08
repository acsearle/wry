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
    // machinery (GCAS / RDCSS / Gen) which we do not implement.  We hew closely
    // to the pseudocode, foregoing normal C++ idioms.
    //
    // K must provide:  size_t hash(K const&) const; (ADL)
    //                  bool operator==(K const&) const;
    //                  garbage_collected_scan(K const&)  (ADL)
    // V must provide:  garbage_collected_scan(V const&)  (ADL)
    //
    // Dispatch model: each concrete node type overrides one `as_*` virtual
    // to return `this`; the base default returns nullptr.  Operations that
    // pattern-match on node type (find/find_or_emplace/erase/cleanParent)
    // are written as one function body using `if (auto x = node->as_x())`,
    // mirroring the paper's pseudocode more closely than M-operations
    // ×N-types of double-dispatch virtuals.
    //
    // Structure: all node types and helper functions are nested inside
    // `_ctrie<K,V>` so that K, V are bound once at the outer level.  Inside
    // `_ctrie<K,V>` every cross-type reference is unqualified.  External
    // users (`Ctrie<K,V>`) reach into `_ctrie<K,V>` through `using` aliases.


    // -- Helpers (independent of K, V) ---------------------------------------

    inline std::pair<uint64_t, int> flagpos(uint64_t hash, int lev, uint64_t bmp) {
        uint64_t a = (hash >> lev) & 63;
        uint64_t flag = ((uint64_t)1 << a);
        int pos = __builtin_popcountll(bmp & (flag - 1));
        return {flag, pos};
    }

    template<typename K, typename V>
    struct _ctrie {

        // Forward declarations (visual hierarchy)
        struct AnyNode;
        struct   MainNode;
        struct     CNode;
        struct     LNode;
        struct     TNode;
        struct   BranchNode;
        struct     INode;
        struct     SNode;

        // -- Result types ----------------------------------------------------

        struct FindResult {
            enum Tag { OK, RESTART, NOT_FOUND };
            Tag tag;
            V value; // Meaningful if OK
        };

        struct FindOrEmplaceResult {
            enum Tag { OK, RESTART };
            Tag tag;
            V value; // Meaningful if OK
        };

        enum class EraseResult {
            OK,
            RESTART,
            NOT_FOUND,
        };

        struct AlterChoice {
            enum Tag { KEEP, REPLACE, ERASE };
            Tag tag;
            V value; // Meaningful if REPLACE
        };

        struct AlterResult {
            enum Tag { OK, RESTART };
            Tag tag;
            std::optional<V> before;
            std::optional<V> after;
        };

        static constexpr int W = 6;

        // -- Helpers (K, V bound from enclosing scope) -----------------------

        // Atomic load-ACQUIRE with read barrier.
        static MainNode const* READ(GarbageCollectedSlot<MainNode const*> const& main) {
            return main.load_acquire();
        }

        // Atomic Compare-And-Swap-RELEASE with write barrier.
        //
        // Safety: we have already ACQUIRED the expected value.  The slot's CAS
        // internally upgrades to acq_rel and shades the displaced MainNode
        // (Yuasa); see GarbageCollectedSlot.
        //
        // We do not expose the unexpected value on failure because the higher
        // level operation must RESTART from the root if this CAS fails.  We
        // use the `strong` form so we only take this expensive path for
        // non-spurious failures.
        static bool CAS(GarbageCollectedSlot<MainNode const*>& main,
                        MainNode const* expected,
                        MainNode const* desired) {
            return main.compare_exchange_strong_release_relaxed(expected, desired);
        }

        // cleanParent: parent INode `p` may still hold an entry (via the CNode
        // at p->main) referring to child INode `i` whose main has tombstoned.
        // Repair by replacing the CNode entry with the tomb's resurrected SNode
        // (and contracting the parent CNode if it now has only one slot).
        static void cleanParent(INode const* p, INode const* i, size_t hc, int lev) {
            auto m  = READ(i->main);
            auto pm = READ(p->main);
            if (auto cn = pm->as_cnode()) {
                auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                if (!(flag & cn->bmp)) return;
                auto sub = cn->array[pos];
                if (sub != i) return;
                if (auto tn = m->as_tnode())
                    if (!CAS(p->main, cn, cn->updated(pos, tn->sn)->to_contracted(lev)))
                        [[clang::musttail]] return cleanParent(p, i, hc, lev);
            }
        }

        // -- Node hierarchy --------------------------------------------------

        // Type-test virtuals.  Default returns nullptr; each concrete type
        // overrides one to return `this`.  All five live on AnyNode so the
        // result of LNode::found_or_prepended (an SNode-or-LNode) can be
        // queried uniformly.  Static type discrimination (MainNode vs
        // BranchNode) is preserved in slot/array element types.
        struct AnyNode : GarbageCollected {
            virtual CNode const* as_cnode() const { return nullptr; }
            virtual TNode const* as_tnode() const { return nullptr; }
            virtual LNode const* as_lnode() const { return nullptr; }
            virtual INode const* as_inode() const { return nullptr; }
            virtual SNode const* as_snode() const { return nullptr; }
        };

        // MainNode: anything that may live in INode::main (CNode/TNode/LNode).
        struct MainNode : AnyNode {
            virtual void _garbage_collected_debug() const override {
                printf("MainNode\n");
            }
        };

        // BranchNode: anything that may live in CNode::array[] (INode/SNode).
        struct BranchNode : AnyNode {
            virtual void _garbage_collected_debug() const override {
                printf("BranchNode\n");
            }
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

            explicit INode(MainNode const* mn) : main(mn) {}

            void clean(int level) const {
                auto mn = READ(main);
                if (auto cn = mn->as_cnode()) {
                    // SAFETY: the result of to_compressed may be discarded if
                    // the CAS loses; the GC retains it via the new-objects bag.
                    CAS(main, cn, cn->to_compressed(level));
                }
                // Anything else (TNode/LNode) needs no compression here.
            }

            FindResult find(K key,std::size_t hc, int lev, INode const* parent) const {
                assert(hc == hash(key));
                auto mn = READ(main);
                if (auto cn = mn->as_cnode()) {
                    auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                    if (!(flag & cn->bmp)) return { FindResult::NOT_FOUND, {} };
                    auto bn = cn->array[pos];
                    if (auto sin = bn->as_inode()) {
                        return sin->find(key, hc, lev + W, this);
                    } else if (auto sn = bn->as_snode()) {
                        if (sn->k == key) return { FindResult::OK,        sn->v };
                        else              return { FindResult::NOT_FOUND, {}    };
                    }
                } else if (mn->as_tnode()) {
                    // Help compress the parent and ask the caller to retry.
                    if (parent) parent->clean(lev - W);
                    return { FindResult::RESTART, {} };
                } else if (auto ln = mn->as_lnode()) {
                    return ln->find(key);
                }
                std::unreachable();
            }

            FindOrEmplaceResult find_or_emplace(K key, std::size_t hc, V default_, int lev, INode const* parent) const {
                assert(hc == hash(key));
                auto mn = READ(main);

                if (auto cn = mn->as_cnode()) {
                    auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                    if (!(flag & cn->bmp)) {
                        // Empty slot: install a fresh SNode.
                        SNode* nsn = new SNode(key, default_);
                        MainNode const* nmn = cn->inserted(pos, flag, nsn);
                        return (CAS(main, mn, nmn)
                                ? FindOrEmplaceResult{ FindOrEmplaceResult::OK, default_ }
                                : FindOrEmplaceResult{ FindOrEmplaceResult::RESTART, {} });
                    }
                    BranchNode const* bn = cn->array[pos];
                    if (auto in = bn->as_inode())
                        return in->find_or_emplace(key, default_, lev + W, this);
                    if (auto sn = bn->as_snode()) {
                        if (key == sn->k)
                            return { FindOrEmplaceResult::OK, sn->v };
                        // Distinct key: expand the HAMT one level.
                        // CNode::make_with_pair bottoms out into an LNode chain
                        // if hash bits run out.
                        SNode* nsn = new SNode(key, default_);
                        INode* nin = new INode(CNode::make_with_pair(sn, nsn, lev + W));
                        MainNode const* nmn = cn->updated(pos, nin);
                        // SAFETY: if we lose the CAS race, the speculative INode
                        // and sub-MainNode become orphans; the collector picks
                        // them up via the thread-local new-objects bag.
                        return CAS(main, mn, nmn)
                            ? FindOrEmplaceResult{ FindOrEmplaceResult::OK,      nsn->v }
                            : FindOrEmplaceResult{ FindOrEmplaceResult::RESTART, {}     };
                    }
                    std::unreachable();
                }

                if (mn->as_tnode()) {
                    if (parent) parent->clean(lev - W);
                    return { FindOrEmplaceResult::RESTART, {} };
                }

                if (auto ln = mn->as_lnode()) {
                    AnyNode const* result = ln->found_or_prepended(key, default_);
                    if (auto sn = result->as_snode()) {
                        // Key already present in the collision list.
                        return { FindOrEmplaceResult::OK, sn->v };
                    }
                    if (auto nln = result->as_lnode()) {
                        // New head: install via CAS.  nln->sn is the freshly-
                        // prepended SNode carrying our (key, default_).
                        return CAS(main, ln, nln)
                            ? FindOrEmplaceResult{ FindOrEmplaceResult::OK,      nln->sn->v }
                            : FindOrEmplaceResult{ FindOrEmplaceResult::RESTART, {}         };
                    }
                    std::unreachable();
                }

                std::unreachable();
            }

            EraseResult erase(K key, std::size_t hc, int lev, INode const* parent) const {
                assert(hc == hash(key));
                auto mn = READ(main);

                if (auto cn = mn->as_cnode()) {
                    auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                    if (!(flag & cn->bmp))
                        return EraseResult::NOT_FOUND;
                    BranchNode const* bn = cn->array[pos];
                    EraseResult result;
                    if (auto in = bn->as_inode()) {
                        result = in->erase(key, lev + W, this);
                    } else if (auto sn = bn->as_snode()) {
                        if (sn->k != key)
                            return EraseResult::NOT_FOUND;
                        // SAFETY: contracted result may be discarded on CAS loss;
                        // the GC retains it via the new-objects bag.
                        result = CAS(main, cn, cn->removed(pos, flag)->to_contracted(lev))
                            ? EraseResult::OK
                            : EraseResult::RESTART;
                    } else {
                        std::unreachable();
                    }
                    // Post-erase: if our main is now a TNode and we have a
                    // parent, try to absorb the tomb into the parent CNode.
                    if (result == EraseResult::OK && parent) {
                        auto mn2 = READ(main);
                        if (mn2->as_tnode())
                            cleanParent(parent, this, hc, lev - W);
                    }
                    return result;
                }

                if (mn->as_tnode()) {
                    if (parent) parent->clean(lev - W);
                    return EraseResult::RESTART;
                }

                if (auto ln = mn->as_lnode()) {
                    LNode const* head = ln->erased(key);
                    if (head == ln)
                        return EraseResult::NOT_FOUND;
                    MainNode const* desired = head;
                    if (!head->next) // collapse single-element list to a tomb
                        desired = new TNode(head->sn);

                    auto result = CAS(main, ln, desired)
                        ? EraseResult::OK
                        : EraseResult::RESTART;
                    // Post-erase: if our main is now a TNode and we have a
                    // parent, try to absorb the tomb into the parent CNode.
                    if (result == EraseResult::OK && parent) {
                        auto mn2 = READ(main);
                        if (mn2->as_tnode())
                            cleanParent(parent, this, hc, lev - W);
                    }
                    return result;
                }

                std::unreachable();
            }

            INode const* as_inode() const override { return this; }
            virtual void _garbage_collected_scan() const override {
                garbage_collected_scan(main);
            }


            template<typename F>
            AlterResult alter(K key, size_t hc, int lev, INode const* parent, F const& fn) const {
                assert(hc == hash(key));
                auto mn = READ(main);

                if (auto cn = mn->as_cnode()) {
                    auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                    if (!(flag & cn->bmp)) {
                        // Slot is empty
                        AlterChoice choice{fn({})};
                        switch (choice.tag) {
                            case AlterChoice::KEEP:
                                return { AlterResult::OK, {}, {} };
                            case AlterChoice::ERASE:
                                return { AlterResult::OK, {}, {} };
                            case AlterChoice::REPLACE: {
                                SNode* nsn = new SNode(key, choice.value);
                                MainNode const* nmn = cn->inserted(pos, flag, nsn);
                                return (CAS(main, mn, nmn)
                                        ? AlterResult{ AlterResult::OK, {}, nsn->v }
                                        : AlterResult{ AlterResult::RESTART, {}, {} });
                            }
                            default:
                                std::unreachable();
                        }
                    }
                    BranchNode const* bn = cn->array[pos];
                    AlterResult result;
                    if (auto in = bn->as_inode()) {
                        result = in->alter(key, hc, lev + W, this, fn);
                        // Post-erase: if our main is now a TNode and we have a
                        // parent, try to absorb the tomb into the parent CNode.
                        if (result.tag == AlterResult::OK && parent) {
                            auto mn2 = READ(main);
                            if (mn2->as_tnode())
                                cleanParent(parent, this, hc, lev - W);
                        }
                        return result;
                    } else if (auto sn = bn->as_snode()) {
                        if (key == sn->k) {
                            // Key found
                            AlterChoice choice{fn(sn->v)};
                            switch (choice.tag) {
                                case AlterChoice::KEEP:
                                    return AlterResult { AlterResult::OK, sn->v, sn->v };
                                case AlterChoice::REPLACE:
                                    return (CAS(main, cn, cn->updated(pos, new SNode(key, choice.value)))
                                            ? AlterResult{ AlterResult::OK, sn->v, choice.value }
                                            : AlterResult{ AlterResult::RESTART, {}, {} });
                                case AlterChoice::ERASE: {
                                    result = (CAS(main, cn, cn->removed(pos, flag)->to_contracted(lev))
                                              ? AlterResult{ AlterResult::OK, sn->v, {} }
                                              : AlterResult{ AlterResult::RESTART, {}, {} });
                                    // Post-erase: if our main is now a TNode and we have a
                                    // parent, try to absorb the tomb into the parent CNode.
                                    if (result.tag == AlterResult::OK && parent) {
                                        auto mn2 = READ(main);
                                        if (mn2->as_tnode())
                                            cleanParent(parent, this, hc, lev - W);
                                    }
                                    return result;
                                }
                                default:
                                    std::unreachable();
                            }
                        } else {
                            // Key not found
                            AlterChoice choice{fn({})};
                            switch (choice.tag) {
                                case AlterChoice::KEEP:
                                    return { AlterResult::OK, {}, {} };
                                case AlterChoice::ERASE:
                                    return { AlterResult::OK, {}, {} };
                                case AlterChoice::REPLACE: {
                                    // Distinct key: expand the HAMT one level.
                                    // CNode::make_with_pair bottoms out into an LNode chain
                                    // if hash bits run out.
                                    SNode* nsn = new SNode(key, choice.value);
                                    INode* nin = new INode(CNode::make_with_pair(sn, nsn, lev + W));
                                    MainNode const* nmn = cn->updated(pos, nin);
                                    // SAFETY: if we lose the CAS race, the speculative INode
                                    // and sub-MainNode become orphans; the collector picks
                                    // them up via the thread-local new-objects bag.
                                    return (CAS(main, mn, nmn)
                                            ? AlterResult{ AlterResult::OK, {}, choice.value }
                                            : AlterResult{ AlterResult::RESTART, {}, {} });
                                }
                                default:
                                    std::unreachable();
                            }
                        }
                    }
                    std::unreachable();
                }

                if (mn->as_tnode()) {
                    if (parent) parent->clean(lev - W);
                    return { AlterResult::RESTART, {} };
                }

                if (auto ln = mn->as_lnode()) {

                    LNode const* target = ln->find_lnode_for_key(key);
                    if (!target) {
                        // Not present
                        AlterChoice choice{fn({})};
                        switch (choice.tag) {
                            case AlterChoice::KEEP:
                                return { AlterResult::OK, {}, {} };
                            case AlterChoice::ERASE:
                                return { AlterResult::OK, {}, {} };
                            case AlterChoice::REPLACE: {
                                // Prepend new LNode
                                auto nln = new LNode(new SNode(key, choice.value), ln);
                                return (CAS(main, ln, nln)
                                        ? AlterResult{ AlterResult::OK, {}, choice.value }
                                        : AlterResult{ AlterResult::RESTART, {}, {} });
                            }
                            default:
                                std::unreachable();
                        }
                    }
                    assert(target->sn->k == key);
                    // Is present
                    AlterChoice choice{fn(target->sn->v)};
                    switch (choice.tag) {
                        case AlterChoice::KEEP:
                            return { AlterResult::OK, target->sn->v, target->sn->v };
                        case AlterChoice::ERASE: {
                            // Gotta erase it
                            LNode const* nln = ln->without(target);
                            MainNode const* nmn = nln;
                            if (!nln->next) // collapse single-element list to a tomb
                                nmn = new TNode(nln->sn);
                            auto result = (CAS(main, ln, nmn)
                                           ? AlterResult{ AlterResult::OK, target->sn->v, choice.value }
                                           : AlterResult{ AlterResult::RESTART, {}, {} });
                            // Post-erase: if our main is now a TNode and we have a
                            // parent, try to absorb the tomb into the parent CNode.
                            if (result.tag == AlterResult::OK && parent) {
                                auto mn2 = READ(main);
                                if (mn2->as_tnode())
                                    cleanParent(parent, this, hc, lev - W);
                            }
                            return result;
                        }
                        case AlterChoice::REPLACE: {
                            // Gotta replace it
                            auto nln = new LNode(new SNode(key, choice.value), ln->without(target));
                            return (CAS(main, ln, nln)
                                    ? AlterResult{ AlterResult::OK, target->sn->v, choice.value }
                                    : AlterResult{ AlterResult::RESTART, {}, {} });
                        }
                        default:
                            std::unreachable();
                    }
                }
                std::unreachable();
            }

        };

        struct CNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("%s {bmp=%" PRIx64 "}\n", __PRETTY_FUNCTION__, bmp);
            }

            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) CNode` inside `make_with_*`.
            static void* operator new(size_t, void* ptr) noexcept { return ptr; }

            static CNode* make_with_bitmap(uint64_t bitmap) {
                int count = __builtin_popcountll(bitmap);
                size_t bytes = sizeof(CNode) + count * sizeof(BranchNode*);
                void* raw = GarbageCollected::operator new(bytes);
                std::memset(raw, 0, bytes);
                return new(raw) CNode(bitmap);
            }

            // Two-key constructor: returns a CNode normally, but at the bottom
            // of the trie (lev >= 60, where the next level would exhaust the
            // hash) returns an LNode collision list.  Either way, the result
            // is a MainNode and is wrapped in an INode by the caller.
            static MainNode const* make_with_pair(SNode const* s1, SNode const* s2, int lev) {

                if (lev >= 64) {
                    // We have exhausted the hash bits
                    assert(hash(s1->k) == hash(s2->k));
                    LNode const* head = nullptr;
                    head = new LNode(s1, head);
                    head = new LNode(s2, head);
                    return head;
                }

                uint64_t pos1 = (hash(s1->k) >> lev) & 63;
                uint64_t pos2 = (hash(s2->k) >> lev) & 63;

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
                        ncn->array[0] = new INode(CNode::make_with_pair(s1, s2, lev + W));
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

            uint64_t bmp;
#ifndef NDEBUG
            size_t _debug_size;
#endif
            BranchNode const* array[]
#ifndef NDEBUG
                __counted_by(_debug_size)
#endif
                ;

            explicit CNode(uint64_t bitmap_)
            : bmp{bitmap_}
#ifndef NDEBUG
            , _debug_size{(size_t)__builtin_popcountll(bitmap_)}
#endif
            {
            }

            // SAFETY: The functions below build new CNodes by mutating their
            // array elements.  At that point, the new CNode is thread-private,
            // so this mutation is thread safe (no other threads know about the
            // memory location).  In particular, no write barrier is required:
            // the new-objects bag is itself thread-private until the thread
            // explicitly transfers it to the collector at the next epoch
            // unpin, and the collector cannot scan the new CNode before then.

            CNode const* updated(int pos, BranchNode const* bn) const {
                int num = __builtin_popcountll(this->bmp);
                CNode* ncn = CNode::make_with_bitmap(this->bmp);
                for (int i = 0; i != num; ++i) {
                    BranchNode const* sub = (i == pos) ? bn : this->array[i];
                    ncn->array[i] = sub;
                }
                return ncn;
            }

            CNode const* removed(int pos, uint64_t flag) const {
                assert(bmp & flag);
                int num = __builtin_popcountll(bmp);
                CNode* ncn = CNode::make_with_bitmap(bmp ^ flag);
                BranchNode const** dest = ncn->array;
                for (int i = 0; i != num; ++i) {
                    if (i != pos) {
                        *dest++ = array[i];
                    }
                }
                assert(dest == ncn->array + (num - 1));
                return ncn;
            }

            CNode const* inserted(int pos, uint64_t flag, BranchNode const* bn) const {
                assert(!(bmp & flag));
                int num = __builtin_popcountll(bmp);
                CNode* ncn = CNode::make_with_bitmap(bmp ^ flag);
                BranchNode const* const* src = array;
                for (int i = 0; i != num + 1; ++i) {
                    if (i != pos) {
                        ncn->array[i] = *src++;
                    } else {
                        ncn->array[i] = bn;
                    }
                }
                assert(src == array + num);
                return ncn;
            }

            // resurrected: copy of this CNode where each I-over-T slot is
            // replaced by the tomb's SNode.  Other slots (live INodes, plain
            // SNodes) are copied as-is.
            CNode const* resurrected() const {
                int num = __builtin_popcountll(this->bmp);
                CNode* ncn = CNode::make_with_bitmap(this->bmp);
                for (int i = 0; i != num; ++i) {
                    BranchNode const* bn = this->array[i];
                    if (auto in = bn->as_inode()) {
                        auto mn = READ(in->main);
                        if (auto tn = mn->as_tnode())
                            bn = tn->sn;
                    }
                    ncn->array[i] = bn;
                }
                return ncn;
            }

            // to_contracted: if this CNode now holds a single SNode at the
            // leaf, collapse to a TNode so cleanParent can absorb us upward.
            // An INode singleton or any non-leaf CNode is left alone.
            MainNode const* to_contracted(int level) const {
                if (level == 0)
                    return this;
                int num = __builtin_popcountll(this->bmp);
                if (num != 1)
                    return this;
                BranchNode const* bn = this->array[0];
                if (auto sn = bn->as_snode())
                    return new TNode(sn);
                return this;
            }

            [[nodiscard]] MainNode const* to_compressed(int level) const {
                return resurrected()->to_contracted(level);
            }

            CNode const* as_cnode() const override { return this; }
            virtual void _garbage_collected_scan() const override {
                int num = __builtin_popcountll(bmp);
                for (int i = 0; i != num; ++i)
                    garbage_collected_scan(array[i]);
            }
        };

        // SNode is the per-key leaf.
        struct SNode final : BranchNode {

            virtual void _garbage_collected_debug() const override {
                printf("SNode\n");
            }

            K k;
            V v;

            explicit SNode(K key, V value) : k(key), v(value) {}

            SNode const* as_snode() const override { return this; }
            virtual void _garbage_collected_scan() const override {
                garbage_collected_scan(k);
                garbage_collected_scan(v);
            }
        };

        struct LNode final : MainNode {

            // INVARIANT: all SNodes should have the same hash(k) value

            virtual void _garbage_collected_debug() const override {
                printf("LNode\n");
            }

            SNode const* sn;
            LNode const* next;

            LNode(SNode const* a, LNode const* b) : sn(a), next(b) {}

            LNode const* find_lnode_for_key(K key) const {
                for (LNode const* current = this; current; current = current->next) {
                    assert(current->sn);
                    assert(hash(current->sn->k) == hash(key));
                    if (current->sn->k == key)
                        return current;
                }
                return nullptr;
            }

            FindResult find(K key) const {
                LNode const* target = find_lnode_for_key(key);
                return (target
                        ? FindResult{ FindResult::OK, target->sn->v }
                        : FindResult{ FindResult::NOT_FOUND, {} });
            }

            // Returns either an SNode (key already present) or a fresh LNode
            // head (new entry prepended); caller dispatches via as_snode /
            // as_lnode.
            AnyNode const* found_or_prepended(K key, V default_) const {
                LNode const* target = find_lnode_for_key(key);
                return (target
                        ? (AnyNode const*)target->sn
                        : (AnyNode const*)new LNode{ new SNode{ key, default_ }, this });
            }

            // NOTE: The new list reverses the order of the elements before
            // victim:  as ++ [b] ++ cs  ->  reverse(as) ++ cs
            LNode const* without(LNode const* victim) const {
                assert(victim);
                LNode const* head = victim->next;
                for (LNode const* curr = this; curr != victim; curr = curr->next) {
                    assert(curr); // victim was not in the list
                    assert(hash(curr->sn->k) == hash(victim->sn->k));
                    head = new LNode(curr->sn, head);
                }
                return head;
            }


            LNode const* erased(K key) const {
                LNode const* victim = find_lnode_for_key(key);
                return victim ? without(victim) : this;
            }

            LNode const* as_lnode() const override { return this; }
            virtual void _garbage_collected_scan() const override {
                garbage_collected_scan(sn);
                garbage_collected_scan(next);
            }
        };

        struct TNode final : MainNode {

            virtual void _garbage_collected_debug() const override {
                printf("TNode\n");
            }

            SNode const* sn;

            explicit TNode(SNode const* sn) : sn(sn) {}

            TNode const* as_tnode() const override { return this; }
            virtual void _garbage_collected_scan() const override {
                garbage_collected_scan(sn);
            }
        };

    }; // struct _ctrie

    // -- Ctrie<K,V> ----------------------------------------------------------

    template<typename K, typename V>
    struct Ctrie final : GarbageCollected {

        using INode               = typename _ctrie<K,V>::INode;
        using CNode               = typename _ctrie<K,V>::CNode;
        using FindResult          = typename _ctrie<K,V>::FindResult;
        using FindOrEmplaceResult = typename _ctrie<K,V>::FindOrEmplaceResult;
        using EraseResult         = typename _ctrie<K,V>::EraseResult;
        using AlterResult         = typename _ctrie<K,V>::AlterResult;
        using AlterChoice         = typename _ctrie<K,V>::AlterChoice;

        INode const* root;

        Ctrie() : root(new INode(CNode::make_with_bitmap(0))) {}

        std::optional<V> find(K key) {
            for (;;) {
                FindResult a = root->find(key, hash(key), 0, nullptr);
                switch (a.tag) {
                    case FindResult::OK:        return a.value;
                    case FindResult::NOT_FOUND: return std::nullopt;
                    case FindResult::RESTART:   break;
                    default:                    std::unreachable();
                }
            }
        }

        V find_or_emplace(K key, V default_) {
            for (;;) {
                FindOrEmplaceResult r = root->find_or_emplace(key, hash(key), default_, 0, nullptr);
                switch (r.tag) {
                    case FindOrEmplaceResult::OK:      return r.value;
                    case FindOrEmplaceResult::RESTART: break;
                    default:                           std::unreachable();
                }
            }
        }

        bool erase(K key) {
            for (;;) {
                switch (root->erase(key, hash(key), 0, nullptr)) {
                    case EraseResult::OK: return true;
                    case EraseResult::RESTART: break;
                    case EraseResult::NOT_FOUND: return false;
                }
            }
        }

        // alter: atomically looks up a key and based on the value if any
        // decides the new value, if any, and returns the old value
        // TBD: return old (like atomic), new (popular) or both (kitchen sink)
        // The provided function f may be evaluated multiple times
        // Analagous to fetch_update for an atomic scalar value.
        template<typename F>
        std::pair<std::optional<V>, std::optional<V>> alter(K key, F const& fn) {
            for (;;) {
                auto r = root->alter(key, hash(key), 0, nullptr, fn);
                switch (r.tag) {
                    case AlterResult::OK:
                        return { r.before, r.after };
                    case AlterResult::RESTART:
                        break;
                }
            }
        }
        // TODO: name this concept
        // requires std::invocable<F&, std::optional<V>> &&
        // std::convertible_to<std::invoke_result_t<F&, std::optional<V>>, std::optional<V>>


        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(root);
        }

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

    }; // struct Ctrie

} // namespace wry

#endif /* ctrie_hpp */
