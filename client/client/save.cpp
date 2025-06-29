//
//  save.cpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#include <sqlite3.h>

#include "save.hpp"
#include "binary.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

namespace wry::sim {

World* restart_game() {
    return new World;
}

void save_game(World* world) {
        
    const auto& a = world->_value_for_coordinate;
    const size_t n = 0; // a.size();
    
    std::vector<std::pair<Coordinate, Value>> b;
    b.reserve(n);
    
    /*
    for (const auto& e : a._inner._alpha) {
        if (e.occupied()) {
            Coordinate k = e._kv.first;
            Value v = e._kv.second.get();
            if (!_value_is_object(v)) {
                b.emplace_back(k, v);
            } else {
                
            }
        }
    }

    for (const auto& e : a._inner._beta) {
        if (e.occupied()) {
            Coordinate k = e._kv.first;
            Value v = e._kv.second.get();
            if (!_value_is_object(v)) {
                b.emplace_back(k, v);
            } else {
                
            }
        }
    }
     */
     
    std::sort(b.begin(), b.end(), [](auto x, auto y) {
        return x.first < y.first;
    });
    
    {
        
        auto ok = [](int rc) {
            if (rc != SQLITE_OK) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };

        auto done = [](int rc) {
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };
        
        sqlite3* saves = nullptr;
        ok(sqlite3_open_v2("saves.sqlite3", &saves, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr));
        
        {
            sqlite3_stmt* statement = nullptr;
            ok(sqlite3_prepare_v2(saves,
                                  "CREATE TABLE IF NOT EXISTS "
                                  "saves "
                                  "(id INTEGER PRIMARY KEY, t TEXT, value_for_coordinate BLOB)",
                                  -1,
                                  &statement,
                                  nullptr));
            done(sqlite3_step(statement));
            ok(sqlite3_finalize(std::exchange(statement, nullptr)));
        }
        
        {
            sqlite3_stmt* statement = nullptr;
            ok(sqlite3_prepare_v2(saves,
                                  "INSERT INTO saves "
                                  "(t, value_for_coordinate) "
                                  "VALUES(unixepoch('now', 'subsec'), ?)",
                                  -1,
                                  &statement,
                                  nullptr));
            ok(sqlite3_bind_blob64(statement, 1, b.data(), b.size() * 16, nullptr));
            done(sqlite3_step(statement));
            ok(sqlite3_finalize(std::exchange(statement, nullptr)));
        }
    
        ok(sqlite3_close_v2(std::exchange(saves, nullptr)));
        
    }

    
}




World* continue_game() {
        
    gc::HashMap<Coordinate, Scan<Value>> v;

    {
        
        auto ok = [](int rc) {
            if (rc != SQLITE_OK) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };
        
        auto row = [](int rc) {
            if (rc != SQLITE_ROW) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };
        
        sqlite3* saves = nullptr;
        ok(sqlite3_open_v2("saves.sqlite3", &saves, SQLITE_OPEN_READONLY, nullptr));
        
        
        {
            sqlite3_stmt* statement = nullptr;
            ok(sqlite3_prepare_v2(saves,
                                  "SELECT value_for_coordinate "
                                  "FROM saves "
                                  "ORDER BY t DESC "
                                  "LIMIT 1",
                                  -1,
                                  &statement,
                                  nullptr));
            row(sqlite3_step(statement));
            using T = std::pair<Coordinate, Value>;
            const T* p = (const T*)sqlite3_column_blob(statement, 0);
            size_t n = sqlite3_column_bytes(statement, 0) / sizeof(T);
            for (size_t i = 0; i != n; ++i) {
                v.write(p[i].first, p[i].second);
            }             
            ok(sqlite3_finalize(std::exchange(statement, nullptr)));
        }
        
        ok(sqlite3_close_v2(std::exchange(saves, nullptr)));
        
    }
    
    World* w = new World;
    
    // adl::swap(w->_value_for_coordinate, v);
    
    return w;
}




World* load_game(int id) {
    
    gc::HashMap<Coordinate, Scan<Value>> v;
    
    {
        
        auto ok = [](int rc) {
            if (rc != SQLITE_OK) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };
        
        auto row = [](int rc) {
            if (rc != SQLITE_ROW) {
                fprintf(stderr, "%s\n", sqlite3_errstr(rc));
                abort();
            }
        };
        
        sqlite3* saves = nullptr;
        ok(sqlite3_open_v2("saves.sqlite3", &saves, SQLITE_OPEN_READONLY, nullptr));
        
        
        {
            sqlite3_stmt* statement = nullptr;
            ok(sqlite3_prepare_v2(saves,
                                  "SELECT value_for_coordinate "
                                  "FROM saves "
                                  "WHERE id = ?",
                                  -1,
                                  &statement,
                                  nullptr));
            ok(sqlite3_bind_int(statement, 1, id));
            row(sqlite3_step(statement));
            using T = std::pair<Coordinate, Value>;
            const T* p = (const T*)sqlite3_column_blob(statement, 0);
            size_t n = sqlite3_column_bytes(statement, 0) / sizeof(T);
            for (size_t i = 0; i != n; ++i) {
                v.write(p[i].first, p[i].second);
            }
            ok(sqlite3_finalize(std::exchange(statement, nullptr)));
        }
        
        ok(sqlite3_close_v2(std::exchange(saves, nullptr)));
        
    }
    
    World* w = new World;
    
    // adl::swap(w->_value_for_coordinate, v);
    
    return w;
}



std::vector<std::pair<std::string, int>> enumerate_games() {
    
    std::vector<std::pair<std::string, int>> v;
    
    auto ok = [](int rc) {
        if (rc != SQLITE_OK) {
            fprintf(stderr, "%s\n", sqlite3_errstr(rc));
            abort();
        }
    };
    
    auto row = [](int rc) {
        if (rc != SQLITE_ROW) {
            fprintf(stderr, "%s\n", sqlite3_errstr(rc));
            abort();
        }
    };
    
    sqlite3* saves = nullptr;
    ok(sqlite3_open_v2("saves.sqlite3", &saves, SQLITE_OPEN_READONLY, nullptr));
        
    sqlite3_stmt* statement = nullptr;
    ok(sqlite3_prepare_v2(saves,
                          "SELECT datetime(t, 'unixepoch', 'subsec'), id "
                          "FROM saves "
                          "ORDER BY t DESC",
                          -1,
                          &statement,
                          nullptr));
    
    for (;;) {
        int rc = sqlite3_step(statement);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc == SQLITE_ROW) {
            const char* t = (const char*) sqlite3_column_text(statement, 0);
            size_t n = sqlite3_column_bytes(statement, 0);
            int i = sqlite3_column_int(statement, 1);
            v.emplace_back(std::string_view(t, n), i);
        } else {
            fprintf(stderr, "%s\n", sqlite3_errstr(rc));
            abort();
        }
    }
    
    ok(sqlite3_finalize(std::exchange(statement, nullptr)));
    ok(sqlite3_close_v2(std::exchange(saves, nullptr)));
        
    return v;
    
}


} // namespace wry







#if 0


{
    
#define WRY_SQLITE3(X) if (int rc = sqlite3_##X ; rc != SQLITE_OK) { fprintf(stderr, "sqlite3_%s -> %s\n", #X, sqlite3_errstr(rc)); }
    
    auto ok = [](int rc) {
        if (rc != SQLITE_OK) {
            fprintf(stderr, "%s\n", sqlite3_errstr(rc));
            abort();
        }
    };
    
    int rc;
    
    sqlite3* assets = nullptr;
    ok(sqlite3_open_v2("assets.sqlite3", &assets, SQLITE_OPEN_READONLY, nullptr));
    
    sqlite3* saves = nullptr;
    ok(sqlite3_open_v2("saves.sqlite3", &saves, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr));
    
    sqlite3_stmt* statement = nullptr;
    ok(sqlite3_prepare_v2(saves,
                          "SELECT datetime(t, 'unixepoch', 'subsec'), name "
                          "FROM saves "
                          "ORDER BY t DESC "
                          "LIMIT 8 OFFSET ?",
                          -1,
                          &statement,
                          nullptr)
       );
    
    ok(sqlite3_bind_int(statement, 1, 0));
    
    for (;;) {
        rc = sqlite3_step(statement);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc == SQLITE_ROW) {
            auto n = sqlite3_column_count(statement);
            for (auto i = 0; i != n; ++i) {
                const unsigned char* p = sqlite3_column_text(statement, i);
                printf("'%s', ", p);
            }
            printf("\n");
        } else {
            fprintf(stderr, "%s\n", sqlite3_errstr(rc)); abort();
        }
    }
    
    ok(sqlite3_finalize(std::exchange(statement, nullptr)));
    ok(sqlite3_close_v2(std::exchange(saves, nullptr)));
    
    
    sqlite3_close_v2(assets);
    assets = nullptr;
}



#endif
