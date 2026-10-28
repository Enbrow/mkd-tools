//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "build_directory.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <cerrno>

#if defined(__APPLE__)
#include <fcntl.h>
#include <stdio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace MKD::detail
{
    Result<std::unique_ptr<BuildDirectory>> BuildDirectory::create(const std::filesystem::path& destination,
                                                                   bool overwriteFile)
    {
        std::error_code error;
        const auto target = std::filesystem::absolute(destination, error).lexically_normal();
        if (error || target.filename().empty() || target == target.root_path())
            return std::unexpected("Invalid output destination");

        const auto status = std::filesystem::symlink_status(target, error);
        if (std::filesystem::exists(status) && (!overwriteFile || !std::filesystem::is_regular_file(status)))
            return std::unexpected("Output already exists: " + target.string());

        if (error && error != std::errc::no_such_file_or_directory)
            return std::unexpected(error.message());

        std::filesystem::create_directories(target.parent_path(), error);
        if (error)
            return std::unexpected("Cannot create output parent: " + error.message());

        static std::atomic<uint64_t> serial{0};
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 32; ++attempt)
        {
            auto staging = target.parent_path() / std::format(".mkd-build-{}-{}", seed, serial.fetch_add(1));
            if (std::filesystem::create_directory(staging, error))
                return std::unique_ptr<BuildDirectory>(new BuildDirectory(target, std::move(staging), overwriteFile));

            if (error && error != std::errc::file_exists)
                return std::unexpected("Cannot create build directory: " + error.message());
        }
        return std::unexpected("Cannot allocate a build directory");
    }

    BuildDirectory::BuildDirectory(std::filesystem::path destination, std::filesystem::path staging, bool overwriteFile)
        : destination_(std::move(destination)), staging_(std::move(staging)), overwriteFile_(overwriteFile)
    {
    }

    BuildDirectory::~BuildDirectory()
    {
        if (!finished_)
        {
            std::error_code error;
            std::filesystem::remove_all(staging_, error);
        }
    }

    const std::filesystem::path& BuildDirectory::path() const noexcept { return staging_; }

    Result<void> BuildDirectory::check() const
    {
        if (failure_)
            return std::unexpected(*failure_);
        if (finished_)
            return std::unexpected("Writer has already finished");
        return {};
    }

    Result<void> BuildDirectory::fail(std::string error)
    {
        if (!failure_)
            failure_ = std::move(error);
        return std::unexpected(*failure_);
    }

    Result<void> BuildDirectory::publish()
    {
        return publishPath(staging_);
    }

    Result<void> BuildDirectory::publishFile(const std::filesystem::path& filename)
    {
        return publishPath(staging_ / filename);
    }

    Result<void> BuildDirectory::publishPath(const std::filesystem::path& source)
    {
        if (auto state = check(); !state)
            return state;

        std::error_code error;
        const auto status = std::filesystem::symlink_status(destination_, error);
        if (std::filesystem::exists(status) &&
            (!overwriteFile_ || source == staging_ || !std::filesystem::is_regular_file(status)))
            return fail("Output already exists: " + destination_.string());

        if (error && error != std::errc::no_such_file_or_directory)
            return fail(error.message());

        // File replacement is deferred until the staged output has closed successfully.
        if (overwriteFile_ && source != staging_)
        {
#if defined(_WIN32)
            if (!MoveFileExW(source.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING))
                error = std::error_code(GetLastError(), std::system_category());
            else error.clear();
#else
            std::filesystem::rename(source, destination_, error);
#endif
        }
        else
        {
            // Refuse a destination created by another writer after the initial check.
#if defined(__APPLE__)
            if (renameatx_np(AT_FDCWD, source.c_str(), AT_FDCWD, destination_.c_str(), RENAME_EXCL) != 0)
                error = std::error_code(errno, std::generic_category());
            else error.clear();
#elif defined(__linux__)
            if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination_.c_str(), RENAME_NOREPLACE) != 0)
                error = std::error_code(errno, std::generic_category());
            else error.clear();
#elif defined(_WIN32)
            if (!MoveFileW(source.c_str(), destination_.c_str()))
                error = std::error_code(GetLastError(), std::system_category());
            else error.clear();
#else
            std::filesystem::rename(source, destination_, error);
#endif
        }
        if (error)
            return fail("Cannot publish output: " + error.message());
        if (source != staging_)
            std::filesystem::remove_all(staging_, error);

        finished_ = true;
        return {};
    }
}
