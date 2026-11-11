#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsqlite/winsqlite3.h>
#pragma comment(lib, "winsqlite3.lib")
#include "RegionDatabase.h"
#include "PlanetWorld.h"
#include "RenderProfiler.h"
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

namespace Ashen
{
    namespace
    {
        using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

        Statement Prepare(sqlite3* db, const char* sql)
        {
            sqlite3_stmt* statement = nullptr;
            sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
            return Statement(statement, sqlite3_finalize);
        }

        std::string Encode(const Region& r)
        {
            std::ostringstream out;
            out << std::setprecision(9) << r.lit << ' ' << r.harvested << ' ' << r.defeated << ' ' << r.aid
                << ' ' << r.lastGiftLife << ' ' << r.lastTalkLife << ' ' << r.layoutVersion << ' '
                << r.lootCollected << ' ' << r.enemyLoot[0].x << ' ' << r.enemyLoot[0].y << ' '
                << r.enemyLoot[1].x << ' ' << r.enemyLoot[1].y << ' ' << r.enemyRespawn[0] << ' '
                << r.enemyRespawn[1];
            return out.str();
        }

        bool Decode(const std::string& text, Region& r)
        {
            std::istringstream in(text);
            int lit = 0;
            if (!(in >> lit >> r.harvested >> r.defeated >> r.aid >> r.lastGiftLife >> r.lastTalkLife >>
                  r.layoutVersion >> r.lootCollected >> r.enemyLoot[0].x >> r.enemyLoot[0].y >>
                  r.enemyLoot[1].x >> r.enemyLoot[1].y >> r.enemyRespawn[0] >> r.enemyRespawn[1]) ||
                lit < 0 || lit > 1 || r.harvested > 63 || r.defeated > 3 || r.aid < 0 ||
                r.layoutVersion < 1 || r.layoutVersion > 2)
            {
                return false;
            }
            r.lit = lit != 0;
            for (unsigned i = 0; i < 2; ++i)
            {
                if (!std::isfinite(r.enemyLoot[i].x) || !std::isfinite(r.enemyLoot[i].y) ||
                    r.enemyLoot[i].x < 0 || r.enemyLoot[i].x >= ChunkSize || r.enemyLoot[i].y < 0 ||
                    r.enemyLoot[i].y >= ChunkSize || !std::isfinite(r.enemyRespawn[i]) ||
                    r.enemyRespawn[i] < 0 || r.enemyRespawn[i] > 12 ||
                    (((r.defeated >> i) & 1u) != static_cast<unsigned>(r.enemyRespawn[i] > 0)))
                {
                    return false;
                }
            }
            for (unsigned source = 0; source < 8; ++source)
            {
                const bool cleared = source < 6 ? (r.harvested & (1u << source)) != 0
                                                : (r.defeated & (1u << (source - 6))) != 0;
                if (!cleared && (r.lootCollected & (15u << (source * 4))))
                {
                    return false;
                }
            }
            in >> std::ws;
            return in.eof();
        }
    } // namespace

    RegionDatabase::~RegionDatabase()
    {
        Close();
    }

    void RegionDatabase::Close()
    {
        if (handle)
        {
            sqlite3_close_v2(handle);
            handle = nullptr;
        }
        stored = 0;
    }

    bool RegionDatabase::Exec(const char* sql)
    {
        if (sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        {
            return true;
        }
        error = sqlite3_errmsg(handle);
        return false;
    }

    bool RegionDatabase::Open(const std::wstring& path)
    {
        Close();
        if (sqlite3_open16(path.c_str(), &handle) != SQLITE_OK)
        {
            error = handle ? sqlite3_errmsg(handle) : "database_open_failed";
            Close();
            return false;
        }
        sqlite3_busy_timeout(handle, 1500);
        {
            auto version = Prepare(handle, "PRAGMA user_version;");
            if (!version || sqlite3_step(version.get()) != SQLITE_ROW ||
                sqlite3_column_int(version.get(), 0) > 1)
            {
                error = "unsupported_region_database_version";
                Close();
                return false;
            }
        }
        if (!Exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA cache_size=-4096;"
                  "CREATE TABLE IF NOT EXISTS core(id INTEGER PRIMARY KEY CHECK(id=1),data BLOB NOT NULL);"
                  "CREATE TABLE IF NOT EXISTS regions(x INTEGER NOT NULL,y INTEGER NOT NULL,px INTEGER NOT "
                  "NULL,py INTEGER NOT NULL,state TEXT NOT NULL,sim REAL NOT NULL,PRIMARY KEY(x,y)) WITHOUT "
                  "ROWID;"
                  "CREATE INDEX IF NOT EXISTS region_planet ON regions(px,py); PRAGMA user_version=1;"))
        {
            Close();
            return false;
        }
        auto count = Prepare(handle, "SELECT count(*) FROM regions;");
        if (!count || sqlite3_step(count.get()) != SQLITE_ROW)
        {
            error = "region_count_failed";
            Close();
            return false;
        }
        stored = static_cast<std::uint64_t>(sqlite3_column_int64(count.get(), 0));
        return true;
    }

    bool RegionDatabase::Backup(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if (attributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                error = "backup_target_is_directory";
                return false;
            }
            return true;
        }
        if (!handle)
        {
            error = "backup_source_not_open";
            return false;
        }
        const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                               std::to_wstring(GetTickCount64()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "backup_temporary_create_failed";
            return false;
        }
        CloseHandle(file);
        sqlite3* destination = nullptr;
        bool success = sqlite3_open16(temporary.c_str(), &destination) == SQLITE_OK;
        if (success)
        {
            sqlite3_backup* copy = sqlite3_backup_init(destination, "main", handle, "main");
            success = copy != nullptr;
            if (copy)
            {
                const int step = sqlite3_backup_step(copy, -1);
                const int finish = sqlite3_backup_finish(copy);
                success = step == SQLITE_DONE && finish == SQLITE_OK;
            }
        }
        if (destination)
        {
            success = sqlite3_close_v2(destination) == SQLITE_OK && success;
        }
        if (success)
        {
            success = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (!success)
        {
            DeleteFileW(temporary.c_str());
            error = "inventory_migration_backup_failed";
            return false;
        }
        RenderProfiler::Event("inventory_migration_backup", "world_core_and_regions_saved_before_v7");
        return true;
    }

    bool RegionDatabase::ReadCore(std::string& text, bool& found)
    {
        found = false;
        auto statement = Prepare(handle, "SELECT data FROM core WHERE id=1;");
        if (!statement)
        {
            error = sqlite3_errmsg(handle);
            return false;
        }
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE)
        {
            return true;
        }
        const int length = result == SQLITE_ROW ? sqlite3_column_bytes(statement.get(), 0) : -1;
        if (length <= 0 || length > 64 * 1024 * 1024)
        {
            error = "invalid_core_blob";
            return false;
        }
        text.assign(static_cast<const char*>(sqlite3_column_blob(statement.get(), 0)),
                    static_cast<size_t>(length));
        found = true;
        return true;
    }

    bool RegionDatabase::ReadRegion(ChunkKey key, Region& region, bool& found, double now)
    {
        found = false;
        auto statement = Prepare(handle, "SELECT state,sim FROM regions WHERE x=?1 AND y=?2;");
        if (!statement)
        {
            error = sqlite3_errmsg(handle);
            return false;
        }
        sqlite3_bind_int64(statement.get(), 1, key.x);
        sqlite3_bind_int64(statement.get(), 2, key.y);
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE)
        {
            return true;
        }
        const int length = result == SQLITE_ROW ? sqlite3_column_bytes(statement.get(), 0) : -1;
        if (length <= 0 || length > 4096 ||
            !Decode(
                std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0)), length),
                region))
        {
            error = "invalid_region_record";
            return false;
        }
        const double saved = sqlite3_column_double(statement.get(), 1);
        if (!std::isfinite(saved) || saved < 0 || saved > now + .01)
        {
            error = "invalid_region_clock";
            return false;
        }
        for (unsigned slot = 0; slot < 2; ++slot)
        {
            if (!(region.defeated & (1u << slot)))
            {
                continue;
            }
            if (now - saved >= region.enemyRespawn[slot])
            {
                region.enemyRespawn[slot] = 0;
                region.defeated &= ~(1u << slot);
                region.lootCollected &= ~(15u << ((6 + slot) * 4));
            }
            else
            {
                region.enemyRespawn[slot] -= static_cast<float>(now - saved);
            }
        }
        found = true;
        return true;
    }

    bool RegionDatabase::Commit(const std::string& core, const std::map<ChunkKey, Region>& regions,
                                double now)
    {
        if (!handle || core.empty() || core.size() > 64 * 1024 * 1024 || !Exec("BEGIN IMMEDIATE;"))
        {
            return false;
        }
        std::uint64_t added = 0;
        bool good = true;
        {
            auto insert = Prepare(
                handle, "INSERT OR IGNORE INTO regions(x,y,px,py,state,sim) VALUES(?1,?2,?3,?4,?5,?6);");
            auto update = Prepare(handle, "UPDATE regions SET state=?3,sim=?4 WHERE x=?1 AND y=?2;");
            auto save = Prepare(handle, "INSERT OR REPLACE INTO core(id,data) VALUES(1,?1);");
            good = insert && update && save;
            for (const auto& pair : regions)
            {
                if (!good)
                {
                    break;
                }
                const auto text = Encode(pair.second);
                const auto address = PlanetWorld::Locate({pair.first, {0, 0}});
                sqlite3_reset(insert.get());
                sqlite3_clear_bindings(insert.get());
                sqlite3_bind_int64(insert.get(), 1, pair.first.x);
                sqlite3_bind_int64(insert.get(), 2, pair.first.y);
                sqlite3_bind_int64(insert.get(), 3, address.planetX);
                sqlite3_bind_int64(insert.get(), 4, address.planetY);
                sqlite3_bind_text(insert.get(), 5, text.data(), static_cast<int>(text.size()),
                                  SQLITE_TRANSIENT);
                sqlite3_bind_double(insert.get(), 6, now);
                good = sqlite3_step(insert.get()) == SQLITE_DONE;
                added += good ? sqlite3_changes(handle) : 0;
                if (good)
                {
                    sqlite3_reset(update.get());
                    sqlite3_clear_bindings(update.get());
                    sqlite3_bind_int64(update.get(), 1, pair.first.x);
                    sqlite3_bind_int64(update.get(), 2, pair.first.y);
                    sqlite3_bind_text(update.get(), 3, text.data(), static_cast<int>(text.size()),
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_double(update.get(), 4, now);
                    good = sqlite3_step(update.get()) == SQLITE_DONE;
                }
            }
            if (good)
            {
                sqlite3_bind_blob(save.get(), 1, core.data(), static_cast<int>(core.size()),
                                  SQLITE_TRANSIENT);
                good = sqlite3_step(save.get()) == SQLITE_DONE;
            }
        }
        if (good && Exec("COMMIT;"))
        {
            stored += added;
            RenderProfiler::Event("region_database_commit",
                                  "cached=" + std::to_string(regions.size()) +
                                      " stored=" + std::to_string(stored),
                                  core.size());
            return true;
        }
        error = sqlite3_errmsg(handle);
        Exec("ROLLBACK;");
        return false;
    }
} // namespace Ashen
