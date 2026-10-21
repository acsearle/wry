//
//  world_dump.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include "world_dump.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "HeapString.hpp"
#include "command_types.hpp"
#include "fields.hpp"
#include "machine.hpp"
#include "persistent_stack.hpp"
#include "player.hpp"
#include "save.hpp"
#include "spawner.hpp"
#include "term.hpp"
#include "test.hpp"
#include "waitable_map.hpp"

namespace wry {

    namespace {

        struct Dumper {

            std::string out;
            std::unordered_map<const HeapTerm*, size_t> numbers;
            std::deque<const HeapTerm*> queue;

            // The record number of a referenced object, assigned at its
            // first reference, which queues it for printing
            size_t number_of(const HeapTerm* p) {
                auto [it, inserted] = numbers.try_emplace(p, numbers.size() + 1);
                if (inserted)
                    queue.push_back(p);
                return it->second;
            }

            void reference(const HeapTerm* p) {
                if (!p) {
                    out += "null";
                    return;
                }
                out += '#';
                out += std::to_string(number_of(p));
            }

            // ---- Values, inline ----

            void value(bool x) {
                out += x ? "true" : "false";
            }

            template<typename T> requires std::is_enum_v<T>
            void value(T x) {
                out += std::to_string((long long)x);
            }

            template<std::integral T> requires (!std::is_same_v<T, bool>)
            void value(T x) {
                if constexpr (std::is_signed_v<T>)
                    out += std::to_string((long long)x);
                else
                    out += std::to_string((unsigned long long)x);
            }

            void value(EntityID x) {
                out += "EntityID(";
                out += std::to_string((unsigned long long)x.data);
                out += ')';
            }

            void value(Coordinate x) {
                out += "Coordinate(";
                out += std::to_string((long long)x.x);
                out += ", ";
                out += std::to_string((long long)x.y);
                out += ')';
            }

            void value(std::pair<Time, EntityID> const& x) {
                out += '(';
                value(x.first);
                out += ", ";
                value(x.second);
                out += ')';
            }

            void value(std::monostate) {
            }

            void value(std::string_view x) {
                out += '"';
                for (unsigned char ch : x) {
                    switch (ch) {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\t': out += "\\t"; break;
                        default:
                            if ((ch < 0x20) || (ch == 0x7f)) {
                                char buffer[8];
                                std::snprintf(buffer, sizeof buffer, "\\x%02x", ch);
                                out += buffer;
                            } else {
                                out += (char)ch;
                            }
                    }
                }
                out += '"';
            }

            // A Term with a literal form prints as it (the console's own
            // language, command_types.hpp); a heap object without one is
            // a record; anything else shows its bits
            void value(Term x) {
                if (term_is_null(x)) {
                    out += "null";
                    return;
                }
                if (command::printable(x)) {
                    String text = command::to_string(x);
                    out.append(text.data(), text.chars.size());
                    return;
                }
                if (_term_is_object(x)) {
                    reference(_term_as_object(x));
                    return;
                }
                char buffer[32];
                std::snprintf(buffer, sizeof buffer, "Term(0x%016llx)", (unsigned long long)x._data);
                out += buffer;
            }

            void value(const Entity* p) {
                reference(p);
            }

            void value(const PersistentStack<Term>* top) {
                out += '[';
                for (const PersistentStack<Term>* n = top; n; n = n->_next) {
                    if (n != top)
                        out += ", ";
                    value(n->_payload);
                }
                out += ']';
            }

            template<typename Key, typename H, typename D>
            void value(PersistentSet<Key, H, D> const& s) {
                out += '{';
                bool first = true;
                s.for_each([&](Key k) {
                    if (!first)
                        out += ", ";
                    first = false;
                    value(k);
                });
                out += '}';
            }

            // ---- Fields: one line, or a header and an entry per line ----

            template<typename T>
            void field(const char* name, T const& x) {
                out += "  ";
                out += name;
                out += " = ";
                value(x);
                out += '\n';
            }

            void entries_header(const char* name, size_t count) {
                out += "  ";
                out += name;
                out += ": ";
                out += std::to_string(count);
                out += (count == 1) ? " entry\n" : " entries\n";
            }

            template<typename Key, typename T, typename H, typename D>
            void field(const char* name, PersistentMap<Key, T, H, D> const& m) {
                size_t count = 0;
                m.for_each([&](Key, T) { ++count; });
                entries_header(name, count);
                m.for_each([&](Key k, T v) {
                    out += "    ";
                    value(k);
                    out += ": ";
                    value(v);
                    out += '\n';
                });
            }

            template<typename Key, typename T>
            void field(const char* name, WaitableMap<Key, T> const& m) {
                field(name, m.kv);
                std::string waiters = std::string(name) + " waiters";
                field(waiters.c_str(), m.ki);
            }

            // The schedule as the save and the hash see it (world.hpp):
            // the ready set as the bucket at the current time, then the
            // wheel
            void field(const char* name, World::TimeWheelFold const& x) {
                size_t count = 0;
                for (auto [entity_id, _] : x.ready) {
                    (void) entity_id;
                    ++count;
                }
                x.wheel.for_each([&](std::pair<Time, EntityID>) { ++count; });
                entries_header(name, count);
                for (auto [entity_id, _] : x.ready) {
                    out += "    ";
                    value(std::pair<Time, EntityID>{x.time, entity_id});
                    out += '\n';
                }
                x.wheel.for_each([&](std::pair<Time, EntityID> e) {
                    out += "    ";
                    value(e);
                    out += '\n';
                });
            }

        };

        struct DumpFieldVisitor : FieldVisitor<DumpFieldVisitor> {
            Dumper& d;
            explicit DumpFieldVisitor(Dumper& dumper) : d(dumper) {}
            template<FieldRole R, typename T>
            void visit(const char* name, T const& x) {
                if constexpr (R == FieldRole::state)
                    d.field(name, x);
            }
        };

        // The concrete classes, by save type tag: the closed set
        // io/save_format.cpp loads
        template<typename T>
        bool dump_record_as(Dumper& d, const HeapTerm* p, const char* name) {
            if (p->_save_type_tag() != T::SAVE_TYPE_TAG)
                return false;
            d.out += name;
            d.out += '\n';
            DumpFieldVisitor v{d};
            static_cast<const T*>(p)->_fields(v);
            return true;
        }

        void dump_record(Dumper& d, const HeapTerm* p) {
            const bool known = dump_record_as<World>(d, p, "World")
                || dump_record_as<Machine>(d, p, "Machine")
                || dump_record_as<Spawner>(d, p, "Spawner")
                || dump_record_as<Source>(d, p, "Source")
                || dump_record_as<Sink>(d, p, "Sink")
                || dump_record_as<Counter>(d, p, "Counter")
                || dump_record_as<Evenator>(d, p, "Evenator")
                || dump_record_as<Player>(d, p, "Player")
                || dump_record_as<HeapInt64>(d, p, "HeapInt64")
                || dump_record_as<HeapString>(d, p, "HeapString");
            if (!known) {
                char buffer[48];
                std::snprintf(buffer, sizeof buffer, "? (type tag 0x%016llx)\n",
                              (unsigned long long)p->_save_type_tag());
                d.out += buffer;
            }
        }

    } // namespace

    std::string dump_world(World const* world) {
        Dumper d;
        d.number_of(world);
        while (!d.queue.empty()) {
            const HeapTerm* p = d.queue.front();
            d.queue.pop_front();
            d.out += '#';
            d.out += std::to_string(d.numbers[p]);
            d.out += ' ';
            dump_record(d, p);
        }
        return std::move(d.out);
    }

    // A hand-built World with every record kind dumps deterministically,
    // names every record and value, dumps the same after a save and load
    // (content, not identity), and one changed cell is one changed line.
    define_test("world_dump") {
        World* w = new World;
        w->_time = Time{3};

        HeapInt64* shared = new HeapInt64(9001);
        Term shared_term = Term((const HeapTerm*)shared);

        Player* player = new Player;
        player->_entity_id = w->generate_entity_id();
        Spawner* spawner = new Spawner;
        spawner->_location = Coordinate{0, 0};
        spawner->_entity_id = w->generate_entity_id();
        Source* source = new Source;
        source->_location = Coordinate{2, 2};
        source->_of_this = shared_term;
        source->_entity_id = w->generate_entity_id();
        Sink* sink = new Sink;
        sink->_location = Coordinate{4, 2};
        sink->_entity_id = w->generate_entity_id();
        Machine* machine = new Machine;
        machine->_entity_id = w->generate_entity_id();
        machine->_phase = Machine::PHASE_TRAVELLING;
        machine->_old_heading = HEADING_EAST;
        machine->_new_heading = HEADING_NORTH;
        machine->_old_location = Coordinate{5, 5};
        machine->_new_location = Coordinate{6, 5};
        machine->_old_time = Time{2};
        machine->_new_time = Time{5};
        machine->push(term_make_integer_with(7));
        machine->push(term_make_string_with("a longer heap string"));
        machine->push(term_make_opcode(OPCODE_FLIP_FLOP));

        const Entity* entities[] = { player, spawner, source, sink, machine };
        for (const Entity* e : entities) {
            e->seal();
            w->_entity_for_entity_id.set(e->_entity_id, e);
        }
        w->_entity_id_for_coordinate.set(spawner->_location, spawner->_entity_id);
        w->_entity_id_for_coordinate.set(source->_location, source->_entity_id);
        w->_entity_id_for_coordinate.set(machine->_new_location, machine->_entity_id);
        {
            EntityIDSet s;
            s.set(spawner->_entity_id);
            s.set(machine->_entity_id);
            w->_located_for_coordinate.set(spawner->_location, s);
        }
        w->_term_for_coordinate.set(Coordinate{1, 2}, term_make_string_with("hello"));
        w->_term_for_coordinate.set(Coordinate{-1, 0}, term_make_opcode(OPCODE_SWAP));
        w->_term_for_coordinate.set(Coordinate{3, 3}, shared_term);
        w->_waiting_on_time.set({Time{5}, machine->_entity_id});
        w->_player_for_client.set(1, player->_entity_id);
        w->hack_repair_invariant();
        Root<World const*> root{w};

        const std::string text = dump_world(w);
        auto contains = [&](std::string const& s, const char* what) {
            return s.find(what) != std::string::npos;
        };
        assert(text == dump_world(w));
        assert(!contains(text, "? ("));
        assert(text.starts_with("#1 World\n  _time = 3\n"));
        assert(contains(text, " Machine\n"));
        assert(contains(text, " Source\n"));
        assert(contains(text, " Spawner\n"));
        assert(contains(text, " Sink\n"));
        assert(contains(text, " Player\n"));
        assert(contains(text, "_stack = [OPCODE::FLIP_FLOP, \"a longer heap string\", 7]"));
        assert(contains(text, "Coordinate(1, 2): \"hello\""));
        assert(contains(text, "Coordinate(-1, 0): OPCODE::SWAP"));
        assert(contains(text, "Coordinate(3, 3): 9001"));
        assert(contains(text, "_of_this = 9001"));
        assert(contains(text, "(5, EntityID(5))"));
        assert(contains(text, "_player_for_client: 1 entry\n    1: EntityID(1)"));
        assert(contains(text, "{EntityID(2), EntityID(5)}"));
        assert(contains(text, "_entity_for_entity_id: 5 entries"));
        assert(contains(text, "_new_location = Coordinate(6, 5)"));

        // Through a save: the same text
        {
            World* loaded = load_world_from_bytes(serialize_world(w));
            assert(loaded);
            Root<World const*> keep{loaded};
            assert(dump_world(loaded) == text);
        }

        // One changed cell: one line out, one line in
        {
            WaitableMap<Coordinate, Term> terms = w->_term_for_coordinate;
            terms.set(Coordinate{1, 2}, term_make_string_with("goodbye"));
            World* changed = new World(w->_time, w->_entity_id_source, w->_ready,
                                       w->_entity_id_for_coordinate, w->_located_for_coordinate,
                                       w->_entity_for_entity_id, terms, w->_terrain_for_coordinate,
                                       w->_waiting_on_time, w->_player_for_client);
            changed->seal();
            Root<World const*> keep{changed};
            const std::string other = dump_world(changed);
            auto lines = [](std::string const& s) {
                std::vector<std::string> v;
                size_t at = 0;
                while (at < s.size()) {
                    size_t nl = s.find('\n', at);
                    if (nl == std::string::npos)
                        nl = s.size();
                    v.push_back(s.substr(at, nl - at));
                    at = nl + 1;
                }
                std::sort(v.begin(), v.end());
                return v;
            };
            std::vector<std::string> a = lines(text), b = lines(other);
            std::vector<std::string> only_a, only_b;
            std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(only_a));
            std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(only_b));
            assert(only_a.size() == 1 && only_a[0] == "    Coordinate(1, 2): \"hello\"");
            assert(only_b.size() == 1 && only_b[0] == "    Coordinate(1, 2): \"goodbye\"");
        }
        co_return;
    };

} // namespace wry
