//
//  persistent_map.cpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#include "persistent_map.hpp"




#if 0



namespace _persistent_map {
    
    // TODO: replace with array-mapped trie
    
    
    
    template<typename Key, typename T>
    struct PersistentMap : GarbageCollected {
        
        std::map<Key, T> data;
        
        virtual void _garbage_collected_enumerate_fields(TraceContext*p) const override {
            //printf("Was traced\n");
            trace(data,p);
        }
        
        PersistentMap() = default;
        explicit PersistentMap(std::map<Key, T>&& x) : data(std::move(x)) {}
        
        virtual ~PersistentMap() {
            //printf("%s\n", __PRETTY_FUNCTION__);
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
     trace(self._data);
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
     trace(self._data);
     }
     */
    
    
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
            //printf("source_iter != source_end: %d\n", source_iter != source_end);
            //printf("modifier_iter != modifier_end: %d\n", modifier_iter != modifier_end);
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
    
    
    
    

#endif
