//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "MKD/result.hpp"

#include <filesystem>
#include <memory>
#include <optional>

namespace MKD::detail
{
    /** Publishes a completed directory or file and removes an abandoned build. */
    class BuildDirectory
    {
    public:
        static Result<std::unique_ptr<BuildDirectory>> create(const std::filesystem::path& destination,
                                                              bool overwriteFile = false);
        ~BuildDirectory();
        BuildDirectory(const BuildDirectory&) = delete;
        BuildDirectory& operator=(const BuildDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept;
        [[nodiscard]] Result<void> check() const;
        Result<void> fail(std::string error);
        Result<void> publish();
        Result<void> publishFile(const std::filesystem::path& filename);

    private:
        BuildDirectory(std::filesystem::path destination, std::filesystem::path staging, bool overwriteFile);
        Result<void> publishPath(const std::filesystem::path& source);
        std::filesystem::path destination_;
        std::filesystem::path staging_;
        std::optional<std::string> failure_;
        bool finished_ = false;
        bool overwriteFile_ = false;
    };
}
