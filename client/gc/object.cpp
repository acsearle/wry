//
//  object.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cinttypes>

#include "object.hpp"

#include "ctrie.hpp"
#include "hash.hpp"
#include "value.hpp"
#include "table.hpp"

namespace wry::gc {
    
    std::size_t object_hash(const Object* object) {
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
                return std::hash<const void*>()(object);
            case Class::STRING:
                return ((const HeapString*)object)->_hash;
            case Class::INT64:
                return std::hash<std::int64_t>()(((const HeapInt64*)object)->_integer);
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE:
                abort();
        }
    }
    
    
    void object_debug(const Object* object) {
        if (!object)
            return (void)printf("%#0.12" PRIx64 "\n", (uint64_t)0);
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                return (void)printf("IndirectFixedCapacityValueArray[%zd]\n",
                                    ((const IndirectFixedCapacityValueArray*)object)->_capacity);
            }
            case Class::TABLE: {
                return (void)printf("HeapTable[%zd]\n",
                                    ((const HeapTable*)object)->size());
            }
            case Class::STRING: {
                const HeapString* string = (const HeapString*)object;
                return (void)printf("%#0.12" PRIx64 "\"%.*s\"\n",
                                    (uint64_t)object,
                                    (int)string->_size,
                                    string->_bytes);
            }
            case Class::INT64: {
                return (void)printf("HeapInt64\n");
            }
            case Class::CTRIE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie\n", (uint64_t)object);
            }
            case Class::CTRIE_CNODE: {
                return (void)printf("Ctrie::CNode{.bmp=%llx}\n", ((const Ctrie::CNode*)object)->bmp);
            }
            case Class::CTRIE_INODE: {
                return (void)printf("Ctrie::INode\n");
            }
            case Class::CTRIE_LNODE: {
                return (void)printf("Ctrie::INode\n");
            }
            case Class::CTRIE_TNODE: {
                return (void)printf("Ctrie::TNode\n");
            }
            default: {
                return (void)printf("Object{._class=%x}\n", object->_class);
            }
        }
    }
    

}
