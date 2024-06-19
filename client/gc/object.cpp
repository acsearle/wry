//
//  object.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cinttypes>

#include "object.hpp"

#include "array.hpp"
#include "ctrie.hpp"
#include "hash.hpp"
#include "value.hpp"
#include "table.hpp"

namespace wry::gc {
    
    std::size_t object_hash(const Object* object) {
        switch (object->_class) {
            case Class::ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
                return std::hash<const void*>()(object);
            case Class::STRING:
                return ((const HeapString*)object)->_hash;
            case Class::INT64:
                return std::hash<std::int64_t>()(((const HeapInt64*)object)->_integer);
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE:
                abort();
        }
    }
    
    
    void object_debug(const Object* object) {
        if (!object)
            return (void)printf("0x000000000000\n");
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                return (void)printf("%#0.12" PRIx64 " IndirectFixedCapacityValueArray[%zd]\n",
                                    (uint64_t)object,
                                    ((const IndirectFixedCapacityValueArray*)object)->_capacity);
            }
            case Class::ARRAY: {
                return (void)printf("%#0.12" PRIx64 " HeapArray[%zd]\n",
                                    (uint64_t)object,
                                    ((const HeapArray*)object)->size());
            }
            case Class::TABLE: {
                return (void)printf("%#0.12" PRIx64 " HeapTable[%zd]\n",
                                    (uint64_t)object,
                                    ((const HeapTable*)object)->size());
            }
            case Class::STRING: {
                const HeapString* string = (const HeapString*)object;
                return (void)printf("%#0.12" PRIx64 " \"%.*s\"\n",
                                    (uint64_t)object,
                                    (int)string->_size,
                                    string->_bytes);
            }
            case Class::INT64: {
                return (void)printf("%#0.12" PRIx64 " HeapInt64{%" PRId64 "}\n",
                                    (uint64_t)object,
                                    ((const HeapInt64*)object)->_integer);
            }
            case Class::CTRIE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie\n",
                                    (uint64_t)object);
            }
            case Class::CTRIE_CNODE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie::CNode{.bmp=%llx}\n",
                                    (uint64_t)object,
                                    ((const Ctrie::CNode*)object)->bmp);
            }
            case Class::CTRIE_INODE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie::INode\n",
                                    (uint64_t)object);
            }
            case Class::CTRIE_LNODE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie::INode\n",
                                    (uint64_t)object);
            }
            case Class::CTRIE_TNODE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie::TNode\n",
                                    (uint64_t)object);
            }
            default: {
                return (void)printf("%#0.12" PRIx64 " Object{._class=%x}\n",
                                    (uint64_t)object,
                                    object->_class);
            }
        }
    }
    

}
