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
            _ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const;
        };

        struct BranchNode : AnyNode {

            virtual void _garbage_collected_debug() const override {
                printf("BranchNode\n");
            }

            virtual EraseResult
            _ctrie_bn_erase(KeyType key, int level, const INode* in,
                            const CNode* cn, int pos, uint64_t flag) const = 0;
            virtual FindOrEmplaceResult
            _ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int level, const INode* in,
                                      const CNode* cn, int pos) const = 0;
            virtual const BranchNode* _ctrie_bn_resurrect() const;
            virtual const MainNode*
            _ctrie_bn_to_contracted(const CNode* cn) const;
        };
        
        struct MainNode : AnyNode {

            virtual void _garbage_collected_debug() const override {
                printf("MainNode\n");
            }

            virtual void _ctrie_mn_clean(int level, const INode* parent) const;
            virtual bool _ctrie_mn_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const;
            virtual bool _ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const;
            virtual EraseResult _ctrie_mn_erase(KeyType key, int lev, const INode* parent, const INode* i) const = 0;
            virtual void _ctrie_mn_erase2(const INode* p, const INode* i, size_t hc, int lev) const;
            virtual FindOrEmplaceResult _ctrie_mn_find_or_emplace(KeyType key, ValueType default_, int lev, const INode* parent, const INode* i) const = 0;
            virtual const BranchNode* _ctrie_mn_resurrect(const INode* i) const;
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

            explicit INode(const MainNode*);
            virtual ~INode() final = default;
            
            void clean(int lev) const;
            FindOrEmplaceResult find_or_emplace(KeyType key, ValueType default_, int level, const INode* parent) const;
            // TODO: erase by key vs erase by SNode identity
            EraseResult erase(KeyType key, int level, const INode* parent) const;
            const MainNode* load() const;
            bool compare_exchange(const MainNode* expected, const MainNode* desired) const;
            
            
            virtual void _garbage_collected_scan() const override;
            
            virtual const BranchNode* _ctrie_bn_resurrect() const override;
            virtual FindOrEmplaceResult _ctrie_bn_find_or_emplace(KeyType key, ValueType default_, int level,
                                                                const INode* in, const CNode* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(KeyType key, int level, const INode* in,
                                                const CNode* cn, int pos, uint64_t flag) const override;
            
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
