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

#include "object.hpp"
#include "adl.hpp"

namespace wry {
    
    namespace _persistent_map {

        // These objects will be upgraded to
        // - array-mapped tries
        // - skiplists
        
                
        template<typename Key>
        struct ObjectSet : gc::Object {

            std::set<Key> _set;
                        
            ObjectSet* clone() const {
                return new ObjectSet{_set};
            }
            
            bool contains(Key k) const {
                return _set.contains(k);
            }

            bool try_insert(Key k) {
                return _set.insert(k).second;
            }
            
            virtual void _object_scan() const override {
                for (auto&& x : _set)
                    adl::trace(x);
            }                        
            
        };
                
        
        template<typename Key, typename T>
        struct ObjectMap : gc::Object {
            
            std::map<Key, T> _map;
            
            ObjectMap() = default;
            explicit ObjectMap(std::map<Key, T>&& source) : _map(std::move(source)) {}
            
            virtual void _object_scan() const override {
                for (auto&& [k, v] : _map) {
                    adl::trace(k);
                    adl::trace(v);
                }
            }
            
            bool contains(Key k) const {
                return _map.contains(k);
            }
            
            bool get(Key k, T& victim) const {
                auto a = _map.find(k);
                bool b = a != _map.end();
                if (b)
                    victim = a->second;
                return b;
            }
            
            ObjectMap* clone() const {
                return new ObjectMap{_map};
            }
            
            bool insert_or_assign(Key k, T t) {
                return _map.insert_or_assign(k, t).second;
            }
            
            void strict_insert(Key k, T t) {
                auto [_, success] = _map.insert(k, t);
                assert(success);
            }
            
            void strict_assign(Key k, T t) {
                auto a = _map.find(k);
                assert(a != _map.end());
                *a = std::pair(k, t);
            }
            
        };
        
        
        template<typename Key>
        struct PersistentSet {
            
            const ObjectSet<Key>* _data;
            
            bool contains(Key k) const {
                return _data && _data->contains(k);
            }
            
            bool set(Key k) {
                ObjectSet<Key>* a = _data ? _data->clone() : new ObjectSet<Key>;
                bool result = a->set(k);
                _data = a;
                return result;
            }
            
            template<typename F>
            void parallel_for_each(const F& f) const {
                if (_data)
                    for (Key k : _data->_set)
                        f(k);
            }

        };
        
        template<typename Key>
        void trace(const PersistentSet<Key>& self) {
            adl::trace(self._data);
        }
        
        
        template<typename Key>
        struct EphemeralSet {
            
            ObjectSet<Key>* _data;
            
            bool contains(Key k) const {
                return _data && _data->contains(k);
            }
            
            bool set(Key k) {
                return _data->set(k).second;
            }
            
        };

        template<typename Key>
        void trace(const EphemeralSet<Key>& self) {
            adl::trace(self._data);
        }
        

        template<typename Key, typename T>
        struct PersistentMap {
            
            const ObjectMap<Key, T>* _data = nullptr;
            
            PersistentMap() : _data(nullptr) {}
            explicit PersistentMap(const ObjectMap<Key, T>* a) : _data(a) {}

            bool contains(Key k) const {
                return _data && _data->contains(k);
            }
            
            bool get(Key k, T& victim) const {
                return _data && _data->get(k, victim);
            }
            
            bool set(Key k, T t) {
                ObjectMap<Key, T>* a = _data ?  _data->clone() : new ObjectMap<Key, T>;
                bool result = a->set(k, t);
                _data = a;
                return result;
            }
            
        };
               
        
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
        
        template<typename Key, typename T>
        struct StableConcurrentMap {
            std::map<Key, T> _map;
        };
        
        template<typename Key, typename T, typename U, typename F>
        PersistentMap<Key, T> parallel_rebuild(const PersistentMap<Key, T>& source,
                                               const StableConcurrentMap<Key, U>& modifier,
                                               F&& action) {
            // slow dumb implementation: just iterate over both in one thread

            std::map<Key, T> result;
            if (!source._data) {
                auto modifier_iter = modifier._map.begin();
                auto modifier_end = modifier._map.end();
                for (;;) {
                    if (modifier_iter != modifier_end) {
                        result.emplace(modifier_iter->first, action(*modifier_iter));
                        ++modifier_iter;
                    } else {
                        break;
                    }
                }
                return PersistentMap<Key, T>(new ObjectMap<Key, T>{std::move(result)});;
            }
            
            auto source_iter = source._data->_map.begin();
            auto source_end = source._data->_map.end();
            auto modifier_iter = modifier._map.begin();
            auto modifier_end = modifier._map.end();
                        
            for (;;) {
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
            
            return PersistentMap<Key, T>(new ObjectMap<Key, T>{std::move(result)});
            
        }
        
        

        
    };
    
    using _persistent_map::PersistentMap;
    using _persistent_map::EphemeralMap;
    using _persistent_map::PersistentSet;
    using _persistent_map::EphemeralMap;
    using _persistent_map::StableConcurrentMap;

}

#endif /* PersistentMap_hpp */
