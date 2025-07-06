//
//  persistent_map.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef persistent_map_hpp
#define persistent_map_hpp

#include <os/lock.h>

#include <iostream>
#include <map>
#include <mutex>
#include <set>

#include "adl.hpp"
#include "garbage_collected.hpp"

namespace wry::orphan {
    
    template<typename A, typename B>
    void trace(const std::pair<A, B>& p) {
        adl::trace(p.first);
        adl::trace(p.second);
    }
    
    template<typename Key, typename Compare>
    void trace(const std::set<Key, Compare>& s) {
        for (const Key& k : s)
            adl::trace(k);
    }
    
    template<typename Key, typename T, typename Compare>
    void trace(const std::map<Key, T, Compare>& m) {
        for (const auto& p : m)
            orphan::trace(p);
    }
    
} // namespace wry::orphan

namespace wry {

    // Two ways of hoisting ordinary objects into a concurrent garbage collected
    // context: make them copy-on-write or make them
    
    template<typename T>
    struct ImmutableGarbageCollected : GarbageCollected {
                
        T data;
        
        explicit ImmutableGarbageCollected(auto&&... args) : data(FORWARD(args)...) {}
        
        virtual ~ImmutableGarbageCollected() {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual void _garbage_collected_scan() const override {
            adl::trace(data);
        }

        static const ImmutableGarbageCollected* make(auto&&... args) {
            return new ImmutableGarbageCollected{T(FORWARD(args)...)};
        }
        
        T copy_inner() const {
            return T{data};
        }
        
        const ImmutableGarbageCollected* copy_with_mutation(auto&& f) const {
            T mutable_copy{data};
            (void) FORWARD(f)(mutable_copy);
            return make(std::move(mutable_copy));
        }
        
    }; // ImmutableGarbageCollected
    
    struct PlatformFastBasicLockable {
        os_unfair_lock _lock = OS_UNFAIR_LOCK_INIT;
        void lock() {
#ifndef NDEBUG
            os_unfair_lock_assert_not_owner(&_lock);
#endif
            os_unfair_lock_lock(&_lock);
        }
        void unlock() {
#ifndef NDEBUG
            os_unfair_lock_assert_owner(&_lock);
#endif
            os_unfair_lock_unlock(&_lock);
        }
    };
    
    template<typename T>
    struct SynchronizedGarbageCollected : GarbageCollected {
        
        T _data;
        mutable PlatformFastBasicLockable _lock;
        
        explicit SynchronizedGarbageCollected(auto&&... args)
        : _data(FORWARD(args)...) {
        }
        
        virtual void _garbage_collected_scan() const override {
            std::unique_lock guard(_lock);
            adl::trace(_data);
        }
        
        static SynchronizedGarbageCollected* make(auto&&... args) {
            return new SynchronizedGarbageCollected{FORWARD(args)...};
        }

        decltype(auto) access(auto&& f) const {
            std::unique_lock guard(_lock);
            return FORWARD(f)(_data);
        }

        decltype(auto) access(auto&& f) {
            std::unique_lock guard(_lock);
            return FORWARD(f)(_data);
        }
        
    };
   
    
    namespace _persistent_map {

        // These objects will be upgraded to
        // - array-mapped tries
        // - skiplists
        
        template<typename Key>
        struct PersistentSet : GarbageCollected {
            
            std::set<Key> data;
            
            virtual void _garbage_collected_scan() const override {
                printf("%s\n", __PRETTY_FUNCTION__);
                orphan::trace(data);
            }
            
            explicit PersistentSet(auto&&... args) : data(FORWARD(args)...) {}
            
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
        struct PersistentMap : GarbageCollected {
            
            std::map<Key, T> data;
            
            virtual void _garbage_collected_scan() const override {
                printf("Was traced\n");
                orphan::trace(data);
            }
            
            PersistentMap() = default;
            explicit PersistentMap(std::map<Key, T>&& x) : data(std::move(x)) {}
            
            virtual ~PersistentMap() {
                printf("%s\n", __PRETTY_FUNCTION__);
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
            std::mutex _mutex;
            std::map<Key, T> _map;
            
            bool insert_or_assign(auto&& k, auto&& v) {
                std::unique_lock lock{_mutex};
                return _map.insert_or_assign(FORWARD(k),
                                             FORWARD(v)).first;
            }
            
            decltype(auto) subscript_and_mutate(auto&& k, auto&& f) {
                std::unique_lock lock{_mutex};
                return std::forward<decltype(f)>(f)(_map[FORWARD(k)]);
            }
            
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

#endif /* persistent_map_hpp */
