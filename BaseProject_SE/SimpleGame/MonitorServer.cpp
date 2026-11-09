#include "MonitorServer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace Ashen
{
    namespace
    {
        constexpr std::size_t MaximumHeaderBytes = 8192;
        constexpr std::size_t MaximumAssetBytes = 2 * 1024 * 1024;
        constexpr auto ClientDeadline = std::chrono::seconds(2);

        struct Asset
        {
            std::string contentType;
            std::string body;
        };

        std::filesystem::path ExecutableDirectory()
        {
            std::wstring buffer(32768, L'\0');
            DWORD length = GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size())
            {
                return {};
            }

            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        bool ReadAsset(const std::filesystem::path& path, std::string& text)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                return false;
            }

            auto length = stream.tellg();
            if (length < 0 || length > static_cast<std::streamoff>(MaximumAssetBytes))
            {
                return false;
            }

            text.resize(static_cast<std::size_t>(length));
            stream.seekg(0);
            if (!text.empty())
            {
                stream.read(&text[0], static_cast<std::streamsize>(text.size()));
            }

            return static_cast<bool>(stream);
        }

        bool MakeNonblocking(SOCKET socket)
        {
            u_long enabled = 1;
            return ioctlsocket(socket, FIONBIO, &enabled) == 0;
        }

        bool WaitForSocket(SOCKET socket, bool write)
        {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(socket, &ready);
            timeval timeout = {0, 100000};
            return select(0, write ? nullptr : &ready, write ? &ready : nullptr, nullptr, &timeout) > 0;
        }
    } // namespace

    struct MonitorServer::Impl
    {
        std::atomic<bool> running = false;
        SOCKET listener = INVALID_SOCKET;
        bool winsockReady = false;
        std::thread worker;
        std::mutex snapshotMutex;
        std::shared_ptr<const std::string> snapshot;
        std::map<std::string, Asset> assets;

        void LoadAssets()
        {
            const auto executableRoot = ExecutableDirectory() / L"Monitor";
            std::error_code error;
            const auto workingRoot = std::filesystem::current_path(error) / L"Monitor";
            const char* routes[] = {"/index.html", "/monitor.js", "/monitor.css"};
            const char* types[] = {"text/html; charset=utf-8", "text/javascript; charset=utf-8",
                                   "text/css; charset=utf-8"};

            assets.clear();
            for (int index = 0; index < 3; ++index)
            {
                Asset asset = {types[index], {}};
                const auto filename = std::filesystem::path(routes[index] + 1);
                if (ReadAsset(executableRoot / filename, asset.body) ||
                    (!error && ReadAsset(workingRoot / filename, asset.body)))
                {
                    assets.emplace(routes[index], std::move(asset));
                }
                else
                {
                    std::printf(
                        "[monitor] Missing Monitor%s beside the executable or in the working directory.\n",
                        routes[index]);
                }
            }
        }

        bool ReadRequest(SOCKET client, std::string& request)
        {
            const auto deadline = std::chrono::steady_clock::now() + ClientDeadline;
            while (running && std::chrono::steady_clock::now() < deadline)
            {
                if (!WaitForSocket(client, false))
                {
                    continue;
                }

                char buffer[1024];
                const std::size_t remaining = MaximumHeaderBytes - request.size();
                const int count =
                    recv(client, buffer, static_cast<int>(std::min(sizeof(buffer), remaining)), 0);
                if (count <= 0)
                {
                    if (count < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                    {
                        continue;
                    }

                    return false;
                }

                request.append(buffer, static_cast<std::size_t>(count));
                if (request.find("\r\n\r\n") != std::string::npos)
                {
                    return true;
                }

                if (request.size() >= MaximumHeaderBytes)
                {
                    return false;
                }
            }

            return false;
        }

        bool SendBytes(SOCKET client, const std::string& text)
        {
            const auto deadline = std::chrono::steady_clock::now() + ClientDeadline;
            std::size_t sent = 0;
            while (running && sent < text.size() && std::chrono::steady_clock::now() < deadline)
            {
                if (!WaitForSocket(client, true))
                {
                    continue;
                }

                const int count = send(client, text.data() + sent,
                                       static_cast<int>(std::min<std::size_t>(text.size() - sent, 16384)), 0);
                if (count <= 0)
                {
                    if (count < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                    {
                        continue;
                    }

                    return false;
                }

                sent += static_cast<std::size_t>(count);
            }

            return sent == text.size();
        }

        void Respond(SOCKET client, const char* status, const std::string& type, const std::string& body)
        {
            std::string header = "HTTP/1.1 ";
            header += status;
            header += "\r\nContent-Type: " + type;
            header += "\r\nContent-Length: " + std::to_string(body.size());
            header += "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff";
            header +=
                "\r\nContent-Security-Policy: default-src 'self'; object-src 'none'; frame-ancestors 'none'";
            header += "\r\nConnection: close\r\n\r\n";
            if (SendBytes(client, header))
            {
                SendBytes(client, body);
            }
        }

        void HandleClient(SOCKET client)
        {
            std::string request;
            if (!ReadRequest(client, request))
            {
                return;
            }

            std::istringstream line(request.substr(0, request.find("\r\n")));
            std::string method, path, version, extra;
            line >> method >> path >> version;
            if (method.empty() || path.empty() || (version != "HTTP/1.1" && version != "HTTP/1.0") ||
                (line >> extra))
            {
                Respond(client, "400 Bad Request", "text/plain; charset=utf-8", "Invalid HTTP request.\n");
                return;
            }

            if (method != "GET")
            {
                Respond(client, "405 Method Not Allowed", "text/plain; charset=utf-8",
                        "Only GET is supported.\n");
                return;
            }

            path.erase(path.find('?') == std::string::npos ? path.size() : path.find('?'));
            if (path == "/api/state")
            {
                std::shared_ptr<const std::string> current;
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex);
                    current = snapshot;
                }

                if (!current)
                {
                    Respond(client, "503 Service Unavailable", "application/json; charset=utf-8",
                            "{\"schema_version\":1,\"status\":\"starting\"}");
                    return;
                }

                Respond(client, "200 OK", "application/json; charset=utf-8", *current);
                return;
            }

            if (path == "/")
            {
                path = "/index.html";
            }

            auto asset = assets.find(path);
            if (asset != assets.end())
            {
                Respond(client, "200 OK", asset->second.contentType, asset->second.body);
                return;
            }

            if (path == "/index.html" || path == "/monitor.js" || path == "/monitor.css")
            {
                Respond(client, "503 Service Unavailable", "text/plain; charset=utf-8",
                        "Monitor assets are missing. Copy the Monitor folder beside the game executable.\n");
                return;
            }

            Respond(client, "404 Not Found", "text/plain; charset=utf-8", "Unknown monitor route.\n");
        }

        void Serve()
        {
            while (running)
            {
                if (!WaitForSocket(listener, false))
                {
                    continue;
                }

                SOCKET client = accept(listener, nullptr, nullptr);
                if (client == INVALID_SOCKET)
                {
                    continue;
                }

                DWORD timeout = 1000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                           sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                           sizeof(timeout));
                if (MakeNonblocking(client))
                {
                    try
                    {
                        HandleClient(client);
                    }
                    catch (...)
                    {
                        // A failed HTTP allocation/request must not terminate the game thread or worker.
                    }
                }

                shutdown(client, SD_BOTH);
                closesocket(client);
            }
        }
    };

    MonitorServer::MonitorServer() : implementation(std::make_unique<Impl>())
    {
    }

    MonitorServer::~MonitorServer()
    {
        Stop();
    }

    bool MonitorServer::Start(unsigned short port)
    {
        if (implementation->running)
        {
            return true;
        }

        if (port == 0)
        {
            std::printf("[monitor] Disabled (port 0).\n");
            return false;
        }

        WSADATA data = {};
        const int startupError = WSAStartup(MAKEWORD(2, 2), &data);
        if (startupError != 0)
        {
            std::printf("[monitor] WSAStartup failed (%d); game continues without the monitor.\n",
                        startupError);
            return false;
        }

        implementation->winsockReady = true;
        implementation->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int socketError = implementation->listener == INVALID_SOCKET ? WSAGetLastError() : 0;
        if (socketError == 0)
        {
            BOOL exclusive = TRUE;
            if (setsockopt(implementation->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                           reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR)
            {
                socketError = WSAGetLastError();
            }
        }

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (socketError == 0 && bind(implementation->listener, reinterpret_cast<sockaddr*>(&address),
                                     sizeof(address)) == SOCKET_ERROR)
        {
            socketError = WSAGetLastError();
        }

        if (socketError == 0 && listen(implementation->listener, 8) == SOCKET_ERROR)
        {
            socketError = WSAGetLastError();
        }

        if (socketError == 0 && !MakeNonblocking(implementation->listener))
        {
            socketError = WSAGetLastError();
        }

        if (socketError != 0)
        {
            std::printf("[monitor] Cannot listen on http://127.0.0.1:%u/ (Winsock %d); game continues.\n",
                        static_cast<unsigned int>(port), socketError);
            Stop();
            return false;
        }

        try
        {
            implementation->LoadAssets();
            implementation->running = true;
            implementation->worker = std::thread([this] { implementation->Serve(); });
        }
        catch (...)
        {
            std::printf("[monitor] Server setup failed; game continues without the monitor.\n");
            Stop();
            return false;
        }

        std::printf("[monitor] Local dashboard: http://127.0.0.1:%u/ | JSON: http://127.0.0.1:%u/api/state\n",
                    static_cast<unsigned int>(port), static_cast<unsigned int>(port));
        return true;
    }

    void MonitorServer::Publish(std::string json)
    {
        auto next = std::make_shared<const std::string>(std::move(json));
        std::lock_guard<std::mutex> lock(implementation->snapshotMutex);
        implementation->snapshot = std::move(next);
    }

    void MonitorServer::Stop()
    {
        implementation->running = false;
        if (implementation->worker.joinable())
        {
            implementation->worker.join();
        }

        {
            std::lock_guard<std::mutex> lock(implementation->snapshotMutex);
            implementation->snapshot.reset();
        }

        if (implementation->listener != INVALID_SOCKET)
        {
            closesocket(implementation->listener);
            implementation->listener = INVALID_SOCKET;
        }

        if (implementation->winsockReady)
        {
            WSACleanup();
            implementation->winsockReady = false;
        }
    }
} // namespace Ashen
