//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include <optional>

#include "garbage_collected.hpp"
#include "hash.hpp"

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

        struct Query;

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


            std::size_t hash() const {
                return wry::hash(data);
            }

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

        struct FindResult {
            enum class Tag {
                RESTART,
                OK,
                NOTFOUND
            };
            Tag tag;
            ValueType found;
            // TODO: Upgrade to a proper sum type that doesn't force a default-constructed ValueType
        };

        // FindOrEmplaceResult is used internally to indicate OK or RESTART.
        // The top-level loop restarts until it is successful, and always returns
        // a (found or emplaced) value
        using FindOrEmplaceResult = std::optional<ValueType>;
        // TODO: Consistency of these result types.  Make


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
            virtual MainNode const*
            _ctrie_bn_to_contracted(CNode const* cn) const;
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
    
    
    
} // namespace wry

#endif /* ctrie_hpp */
