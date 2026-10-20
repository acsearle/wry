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

        Coordinate _location = {};

        template<typename V>
        void _fields(V& v) const {
            Entity::_fields(v);
            v.state("_location", _location);
        }

    };
    
    struct Spawner : Fields<Spawner, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Spawner");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Spawner* make_mutable_clone() const override {
            return new Spawner(*this);
        }

    };

    struct Source : Fields<Source, LocalizedEntity> {

        Term _of_this;

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
            v.state("_of_this", _of_this);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Source");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Source* make_mutable_clone() const override {
            return new Source(*this);
        }

    };

    struct Sink : Fields<Sink, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Sink");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Sink* make_mutable_clone() const override {
            return new Sink(*this);
        }

    };
    
    struct Counter : Fields<Counter, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;
        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Counter");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Counter* make_mutable_clone() const override {
            return new Counter(*this);
        }

    };
    
    struct Evenator : Fields<Evenator, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Evenator");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Evenator* make_mutable_clone() const override {
            return new Evenator(*this);
        }

    };

} // namespace wry::sim
#endif /* spawner_hpp */
