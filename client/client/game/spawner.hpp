//
//  spawner.hpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#ifndef spawner_hpp
#define spawner_hpp

#include "entity.hpp"

namespace wry {

    struct LocalizedEntity : Entity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        Coordinate _location = {};
        virtual void _garbage_collected_scan() const override {
        }

    };
    
    struct Spawner : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

                
        virtual void notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Spawner");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final { abort(); }


    };

    struct Source : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        Value _of_this;
        
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_of_this);
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Source");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final { abort(); }


    };

    struct Sink : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

                
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Sink");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final { abort(); }


    };
    
    struct Counter : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }
        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Counter");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final { abort(); }

    };
    
    struct Evenator : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Evenator");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final { abort(); }

    };

} // namespace wry::sim
#endif /* spawner_hpp */
