//
//  PersistentMap.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef PersistentMap_hpp
#define PersistentMap_hpp

#include <map>
#include <set>

#include <iostream>

#include "object.hpp"
#include "adl.hpp"

namespace wry {
    
    template<typename T>
    struct ImmutableGarbageCollected : gc::Object {
        
        T data;
        
        virtual void _object_scan() const override {
            adl::trace(data);
        }
        
        static const ImmutableGarbageCollected* make(auto&&... parts) {
            return new ImmutableGarbageCollected{T(std::forward<decltype(parts)>(parts)...)};
        }
        
        const ImmutableGarbageCollected* clone_with_mutation(auto&& f) const {
            T mutable_copy{data};
            (void) std::forward<decltype(f)>(f)(mutable_copy);
            return from(std::move(mutable_copy));
        }
        
        
        
    };
    
    namespace _persistent_map {

        // These objects will be upgraded to
        // - array-mapped tries
        // - skiplists
        
        template<typename Key>
        struct PersistentSet : gc::Object {
            
            std::set<Key> data;
            
            virtual void _object_scan() const override {
                printf("%s\n", __PRETTY_FUNCTION__);
                for (const auto& k : data)
                    adl::trace(k);
            }
            
            virtual ~PersistentSet() {
                printf("%s\n", __PRETTY_FUNCTION__);
            }
            
            bool contains(Key k) const {
                return data.contains(k);
            }
            
            template<typename K>
            [[nodiscard]] const PersistentSet* clone_and_erase(K&& k) const {
                std::set<Key> a{data}; // deep copy
                a.erase(std::forward<K>(k));
                return new PersistentSet{std::move(a)};
            }
            
            template<typename K>
            [[nodiscard]] const PersistentSet* clone_and_insert(K&& k) const {
                std::set<Key> a{data}; // deep copy
                a.insert(std::forward<K>(k));
                return new PersistentSet{std::move(a)};
            }
                        
            template<typename F>
            void parallel_for_each(const F& f) const {
                for (const auto& k : data)
                    f(k);
            }

        };
        
        template<typename Key, typename T>
        struct PersistentMap : gc::Object {
            
            std::map<Key, T> data;
            
            virtual void _object_scan() const override {
                printf("Was traced\n");
                for (const auto& [k, v] : data) {
                    adl::trace(k);
                    adl::trace(v);
                }
            }
            
            PersistentMap() = default;
            explicit PersistentMap(std::map<Key, T>&& x) : data(std::move(x)) {}
            
            virtual ~PersistentMap() {
                printf("PersistentMap %zd was deleted\n", data.size());
            }

            bool contains(Key k) const {
                return data.contains(k);
            }
            
            template<typename K>
            bool try_get(K&& k, T& victim) const {
                auto it = data.find(std::forward<K>(k));
                bool result = it != data.end();
                if (result) {
                    victim = it->second;
                }
                return result;
            }
            
            template<typename K>
            [[nodiscard]] const PersistentMap* clone_and_erase(K&& k) const {
                std::map<Key, T> a{data}; // deep copy
                a.erase(std::forward<K>(k));
                return new PersistentMap{std::move(a)};
            }
            
            
            template<typename K, typename V>
            [[nodiscard]] const PersistentMap* clone_and_insert_or_assign(K&& k, V&& v) const {
                std::map<Key, T> a{data}; // deep copy
                a.insert_or_assign(std::forward<K>(k), std::forward<V>(v));
                return new PersistentMap{std::move(a)};
            }

        };
               
        
        /*
        template<typename Key, typename T>
        void trace(const PersistentMap<Key, T>& self) {
            adl::trace(self._data);
        }
        
        template<typename Key, typename T>
        struct EphemeralMap {
            
            ObjectMap<Key, T>* _data = nullptr;
            
            bool contains(Key k) const {
                return _data && _data->contains(k);
            }
            
            bool get(Key k, T& victim) const {
                return _data && _data->get(k, victim);
            }
            
            bool set(Key k, T t) {
                if (!_data)
                    _data = new ObjectMap<Key, T>;
                return _data->set(k, t);
            }
            
        };
        
        template<typename Key, typename T>
        void trace(const EphemeralMap<Key, T>& self) {
            adl::trace(self._data);
        }
         */
        
        template<typename Key, typename T>
        struct StableConcurrentMap {
            std::map<Key, T> _map;
        };
        
        template<typename Key, typename T, typename U, typename F>
        const PersistentMap<Key, T>* parallel_rebuild(const PersistentMap<Key, T>* source,
                                               const StableConcurrentMap<Key, U>& modifier,
                                               F&& action) {
            // slow dumb implementation: just iterate over both in one thread
            // printf("source._data: %p\n", source->_data);

            std::map<Key, T> result;
            
            auto source_iter = source->data.begin();
            auto source_end  = source->data.end();
            auto modifier_iter = modifier._map.begin();
            auto modifier_end = modifier._map.end();
            
            
            for (;;) {
                printf("source_iter != source_end: %d\n", source_iter != source_end);
                printf("modifier_iter != modifier_end: %d\n", modifier_iter != modifier_end);
                if (source_iter != source_end) {
                    if (modifier_iter != modifier_end) {
                        if (source_iter->first < modifier_iter->first) {
                            result.emplace(*source_iter);
                            ++source_iter;
                        } else {
                            result.emplace(modifier_iter->first, action(*modifier_iter));
                            if (!(modifier_iter->first < source_iter->first)) {
                                ++source_iter;
                            }
                            ++modifier_iter;
                        }
                    } else {
                        result.emplace(*source_iter);
                        ++source_iter;
                    }
                } else {
                    if (modifier_iter != modifier_end) {
                        result.emplace(modifier_iter->first, action(*modifier_iter));
                        ++modifier_iter;
                    } else {
                        break;
                    }
                }
            }
            
            return new PersistentMap<Key, T>{std::move(result)};
            
        }
        
        

        
    };
    
    using _persistent_map::PersistentMap;
    //using _persistent_map::EphemeralMap;
    using _persistent_map::PersistentSet;
    //using _persistent_map::EphemeralMap;
    using _persistent_map::StableConcurrentMap;

}

#endif /* PersistentMap_hpp */
