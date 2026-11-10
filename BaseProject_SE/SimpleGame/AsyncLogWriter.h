#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <string>

class AsyncLogWriter
{
  public:
    struct Channel
    {
        std::string name, header;
    };

    AsyncLogWriter();
    ~AsyncLogWriter();
    void Start(const std::filesystem::path& directory, const std::array<Channel, 3>& channels);
    void Write(unsigned channel, std::string row);
    void Stop();
    std::size_t Dropped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
