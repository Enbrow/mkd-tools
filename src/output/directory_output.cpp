//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "directory_output.hpp"

#include <format>
#include <limits>

namespace MKD::detail
{
    DirectoryOutput::DirectoryOutput(std::filesystem::path root, bool overwrite, bool flat)
        : root_(std::move(root)), overwrite_(overwrite), flat_(flat)
    {
    }

    Result<void> DirectoryOutput::prepare()
    {
        if (root_.empty())
            return std::unexpected("Export destination is empty");

        std::error_code error;
        std::filesystem::create_directories(root_, error);
        if (!error)
            root_ = std::filesystem::canonical(root_, error);

        if (error)
            return std::unexpected(std::format("Cannot prepare export destination: {}", error.message()));

        directories_.insert(root_);
        activeParent_ = root_;
        return {};
    }

    Result<void> DirectoryOutput::ensureDirectory(const std::filesystem::path& relativeDirectory)
    {
        if (relativeDirectory.empty())
            return {};

        std::lock_guard lock(mutex_);
        const auto parent = root_ / relativeDirectory;
        if (directories_.contains(parent))
            return {};

        auto current = root_;
        for (const auto& part : relativeDirectory)
        {
            current /= part;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            if (error == std::errc::no_such_file_or_directory)
                error.clear();
            else if (error)
                return std::unexpected(error.message());

            if (std::filesystem::is_symlink(status))
                return std::unexpected("Output directories cannot be symbolic links");

            std::filesystem::create_directory(current, error);
            if (error)
                return std::unexpected(error.message());
        }
        directories_.insert(parent);
        return {};
    }

    Result<void> DirectoryOutput::enterDirectory(const std::filesystem::path& relativeDirectory)
    {
        if (auto prepared = ensureDirectory(relativeDirectory); !prepared)
            return prepared;

        activeParent_ = relativeDirectory.empty() ? root_ : root_ / relativeDirectory;
        return {};
    }

    Result<std::filesystem::path> DirectoryOutput::preparePath(const std::filesystem::path& relativePath)
    {
        if (auto valid = validateRelativeOutputPath(relativePath); !valid)
            return std::unexpected(valid.error());

        if (flat_)
        {
            std::lock_guard lock(mutex_);
            if (!flatNames_.insert(relativePath).second)
                return std::unexpected("Multiple resources resolve to the same output path");
        }

        const auto path = root_ / relativePath;
        if (path.parent_path() != activeParent_)
            if (auto prepared = ensureDirectory(relativePath.parent_path()); !prepared)
                return std::unexpected(prepared.error());

        std::error_code error;
        if (overwrite_)
        {
            const auto status = std::filesystem::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return std::unexpected(error.message());
            if (std::filesystem::is_symlink(status))
                return std::unexpected("Output files cannot be symbolic links");
        }
        return path;
    }

    Result<bool> DirectoryOutput::existingFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory)
            return false;
        if (error)
            return std::unexpected(error.message());
        if (!std::filesystem::exists(status))
            return false;
        if (!std::filesystem::is_regular_file(status))
            return std::unexpected("Output path exists and is not a regular file");
        return true;
    }

    Result<FileWriteResult> DirectoryOutput::openFailure(const std::filesystem::path& path) const
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (!overwrite_ && !error && std::filesystem::is_regular_file(status))
            return FileWriteResult{true, 0};

        return std::unexpected(std::format("Cannot open output file{}", error ? ": " + error.message() : ""));
    }
}
