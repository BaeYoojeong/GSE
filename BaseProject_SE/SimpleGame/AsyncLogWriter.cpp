#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AsyncLogWriter.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

struct AsyncLogWriter::Impl
{
    struct Row
    {
        unsigned channel;
        std::string text;
    };

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Row> rows;
    std::size_t queuedBytes = 0;
    std::atomic<std::size_t> dropped{0};
    bool stopping = false;
    std::thread thread;
};

AsyncLogWriter::AsyncLogWriter() : impl(std::make_unique<Impl>())
{
}

AsyncLogWriter::~AsyncLogWriter()
{
    Stop();
}

std::size_t AsyncLogWriter::Dropped() const
{
    return impl->dropped;
}

void AsyncLogWriter::Start(const std::filesystem::path& directory, const std::array<Channel, 3>& channels)
{
    Stop();
    impl->stopping = false;
    impl->dropped = 0;
    impl->thread = std::thread(
        [this, directory, channels]
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            struct File
            {
                std::ofstream stream;
                std::array<char, 64 * 1024> buffer;
                std::size_t bytes = 0;
                unsigned segment = 0;
                bool failed = false;
            };
            std::array<File, 3> files;
            auto open = [&](unsigned index)
            {
                auto& file = files[index];
                file.stream.close();
                file.stream.clear();
                file.stream.rdbuf()->pubsetbuf(file.buffer.data(), file.buffer.size());
                file.stream.open(directory /
                                     (channels[index].name + "-" + std::to_string(file.segment) + ".csv"),
                                 std::ios::binary | std::ios::trunc);
                file.failed = !file.stream;
                if (file.failed)
                {
                    std::cerr << "[Profile] Cannot open " << channels[index].name << '\n';
                    return;
                }
                file.stream << channels[index].header << '\n';
                file.bytes = channels[index].header.size() + 1;
            };
            for (unsigned index = 0; index < files.size(); ++index)
            {
                open(index);
            }
            for (;;)
            {
                std::deque<Impl::Row> batch;
                {
                    std::unique_lock<std::mutex> lock(impl->mutex);
                    impl->ready.wait(lock, [&] { return impl->stopping || !impl->rows.empty(); });
                    if (impl->stopping && impl->rows.empty())
                    {
                        break;
                    }
                    batch.swap(impl->rows);
                    impl->queuedBytes = 0;
                }
                for (const auto& row : batch)
                {
                    auto& file = files[row.channel];
                    if (file.failed)
                    {
                        ++impl->dropped;
                        continue;
                    }
                    if (file.bytes + row.text.size() + 1 > 16 * 1024 * 1024)
                    {
                        file.segment = (file.segment + 1) % 4;
                        open(row.channel);
                    }
                    if (file.failed)
                    {
                        ++impl->dropped;
                        continue;
                    }
                    file.stream << row.text << '\n';
                    file.bytes += row.text.size() + 1;
                    if (!file.stream)
                    {
                        file.failed = true;
                        ++impl->dropped;
                    }
                }
            }
            // Closing/draining is outside the frame loop; every accepted row is persisted on clean shutdown.
            for (auto& file : files)
            {
                file.stream.close();
            }
        });
}

void AsyncLogWriter::Write(unsigned channel, std::string row)
{
    if (channel >= 3 || !impl->thread.joinable())
    {
        return;
    }
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        constexpr std::size_t Limit = 8 * 1024 * 1024;
        if (impl->stopping || row.size() + 1 + impl->queuedBytes > Limit)
        {
            ++impl->dropped;
            return;
        }
        notify = impl->rows.empty();
        impl->queuedBytes += row.size() + 1;
        impl->rows.push_back({channel, std::move(row)});
    }
    if (notify)
    {
        impl->ready.notify_one();
    }
}

void AsyncLogWriter::Stop()
{
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->stopping = true;
    }
    impl->ready.notify_all();
    if (impl->thread.joinable())
    {
        impl->thread.join();
    }
}
