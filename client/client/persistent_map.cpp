//
//  persistent_map.cpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#include "persistent_map.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("PersistentMap") {
        mutator_become_with_name("test.PersistentMap");
        
        /*
         {
         for (int i = 0; i != 63; ++i) {
         assert(decode(i) == ((uint64_t)1 << i));
         assert(shift_for_keylike_difference(decode(i)) == (i / 6) * 6);
         assert(encode(decode(i)) == i);
         }
         }
         */
        
        mutator_handshake();
        
        {
            uint64_t k = 6435475;
            int v = 4568;
            // auto p = Node<int>::make_with_key_value(k, v);
            PersistentMap<uint64_t, int> m;
            m.set(k, v);
            
            assert(m.contains(k));
            assert(!m.contains(k+1));
            // auto q = p->clone_and_erase(k);
            //decltype(p) q = nullptr;
            //assert(!q);
            //auto r = Node<int>::make_with_key_value(k+100,v+1);
            /*
            auto s = Node<int>::merge(p, r);
            assert(s->contains(k));
            assert(!s->contains(k+1));
            assert(s->contains(k+100));
            q = Node<int>::make_with_key_value(k+2,v+2);
            s = Node<int>::merge(s, q);
            assert(s->contains(k));
            assert(!s->contains(k+1));
            assert(s->contains(k+2));
            assert(s->contains(k+100));
            int u = 0;
            assert(s->try_get(k, u));
            assert(u == v);
            assert(!s->try_get(k+1, u));
            assert(s->try_get(k+2, u));
            assert(u == v+2);
            assert(s->try_get(k+100, u));
            assert(u == v+1);
             */
        }
        
        mutator_handshake();
        
        // stress test
        /*
         {
         const Node<int>* p = nullptr;
         for (int i = 0; i != 1 << 20; ++i) {
         int a = rand() & 0xFF;
         int b = rand() & 0xFF;
         uint64_t k = (uint64_t)a * (uint64_t)b;
         auto q = Node<int>::make_with_key_value(k, a);
         p = p ? Node<int>::merge(p, q) : q;
         //garbage_collected_shade(p);
         //garbage_collected_shade(q);
         mutator_handshake();
         // garbage_collected_shade p because we use it after the handshake
         p->_garbage_collected_shade();
         //garbage_collected_shade(p);
         //garbage_collected_shade(q);
         }
         }
         */
        {
            PersistentMap<uint64_t, int> p;
            std::map<uint64_t, int> m;
            for (int i = 0; i != 65536; ++i) {
                uint64_t k = rand() & (64 * 1024 - 1);
                int v = rand();
                
                uint64_t h = rand() & (64 * 1024 - 1);
                m.erase(h);
                int _;
                (void) p.try_erase(h, _);
                
                
                m.insert_or_assign(k, v);
                p.set(k, v);
                
                
                int u = {};
                if (!p.try_get(k, u)) {
                    printf("expected to find {%llx, %x}\n", k, v);
                    abort();
                }
                if (u != v) {
                    printf("expected to find {%llx, %x}, found {%llx, %x}\n", k, v, k, u);
                    abort();
                }
                mutator_handshake();
                garbage_collected_shade(p);
                // printf("PMT %d\n", i);
            }
            for (uint64_t k = 0; k != 65536; ++k) {
                if (m.count(k)) {
                    int v = {};
                    bool result = p.try_get(k, v);
                    assert(result);
                    assert(v == m[k]);
                } else {
                    int v = {};
                    assert(!p.try_get(k, v));
                }
                mutator_handshake();
                garbage_collected_shade(p);
                // printf("PMT %llu\n", k);
            }
            
        }
        
        
        mutator_resign();
    };
}



#if 0



namespace _persistent_map {
    
    // TODO: replace with array-mapped trie
    
    
    
    template<typename Key, typename T>
    struct PersistentMap : GarbageCollected {
        
        std::map<Key, T> data;
        
        virtual void _garbage_collected_scan() const override {
            //printf("Was traced\n");
            garbage_collected_scan(data);
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
