#pragma once
#include <filesystem>

namespace Ashen
{
    class AcceptanceChecks
    {
      public:
        // Explicit opt-in; refuses an existing directory and never opens the normal game profile.
        static int Run(const std::filesystem::path& directory);
    };
} // namespace Ashen
