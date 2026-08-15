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

                
        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Spawner");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Spawner* make_mutable_clone() const override {
            return new Spawner(*this);
        }

    };

    struct Source : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        Term _of_this;
        
        virtual int64_t notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_of_this);
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Source");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Source* make_mutable_clone() const override {
            return new Source(*this);
        }

    };

    struct Sink : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

                
        virtual int64_t notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Sink");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Sink* make_mutable_clone() const override {
            return new Sink(*this);
        }

    };
    
    struct Counter : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        virtual int64_t notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }
        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Counter");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Counter* make_mutable_clone() const override {
            return new Counter(*this);
        }

    };
    
    struct Evenator : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual int64_t notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Evenator");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Evenator* make_mutable_clone() const override {
            return new Evenator(*this);
        }

    };

} // namespace wry::sim
#endif /* spawner_hpp */
