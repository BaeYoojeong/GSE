#pragma once
#include "WorldTypes.h"
#include <map>
#include <string>

struct sqlite3;

namespace Ashen
{
    class RegionDatabase
    {
      public:
        RegionDatabase() = default;
        ~RegionDatabase();
        RegionDatabase(const RegionDatabase&) = delete;
        RegionDatabase& operator=(const RegionDatabase&) = delete;
        bool Open(const std::wstring& path);
        void Close();

        bool IsOpen() const
        {
            return handle != nullptr;
        }

        bool ReadCore(std::string& text, bool& found);
        bool ReadRegion(ChunkKey key, Region& region, bool& found, double now);
        bool Commit(const std::string& core, const std::map<ChunkKey, Region>& regions, double now);

        std::uint64_t StoredRegions() const
        {
            return stored;
        }

        const std::string& Error() const
        {
            return error;
        }

      private:
        bool Exec(const char* sql);
        sqlite3* handle = nullptr;
        std::uint64_t stored = 0;
        std::string error;
    };
} // namespace Ashen
