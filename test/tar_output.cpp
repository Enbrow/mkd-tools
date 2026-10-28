#include "output/tar_output.hpp"
#include "support/test_dictionary.hpp"

namespace MKD::test
{
    TEST(TarOutputTest, FailedMembersDiscardStagingAndPreservePreviousArchive)
    {
        Workspace workspace;
        const auto target = workspace.path / "birbs.tar";
        writeFile(target, bytes("previous archive"));
        {
            detail::TarOutput output(target, true);
            require(output.prepare());
            EXPECT_FALSE(output.writeStream("Contents/1.xml",
                [](std::ostream& stream) -> Result<void>
                {
                    stream << "partial";
                    return std::unexpected("broken source");
                }));
            EXPECT_FALSE(output.finish({}));
        }
        const auto content = readFile(target);
        EXPECT_EQ(std::string(content.begin(), content.end()), "previous archive");
        EXPECT_EQ(
            std::distance(std::filesystem::directory_iterator(workspace.path), std::filesystem::directory_iterator{}),
            1);
    }

    TEST(TarOutputTest, RefusesInvalidDuplicateAndRacedDestinations)
    {
        Workspace workspace;
        const auto target = workspace.path / "birbs.tar";
        {
            detail::TarOutput output(target, false);
            require(output.prepare());
            auto producer = []() -> Result<RetainedSpan> { return RetainedSpan::copy(bytes("bird")); };
            EXPECT_FALSE(output.writeBytes("../escaped", producer));
            EXPECT_TRUE(output.writeBytes("Graphics/bird", producer));
            EXPECT_FALSE(output.writeBytes("Graphics/bird", producer));
            writeFile(target, bytes("another writer"));
            EXPECT_FALSE(output.finish({}));
        }
        const auto content = readFile(target);
        EXPECT_EQ(std::string(content.begin(), content.end()), "another writer");
        EXPECT_EQ(
            std::distance(std::filesystem::directory_iterator(workspace.path), std::filesystem::directory_iterator{}),
            1);
    }

    TEST(TarOutputTest, StreamWriteFailuresNeverPublishAnArchive)
    {
        Workspace workspace;
        const auto target = workspace.path / "birbs.tar";
        {
            detail::TarOutput output(target, false);
            require(output.prepare());
            EXPECT_FALSE(output.writeStream("empty",
                [](std::ostream& stream) -> Result<void>
                {
                    stream.setstate(std::ios::badbit);
                    return {};
                }));
            EXPECT_FALSE(output.finish({}));
        }
        EXPECT_FALSE(std::filesystem::exists(target));
        EXPECT_TRUE(std::filesystem::is_empty(workspace.path));
    }
}
