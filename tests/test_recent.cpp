#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/drop.h"
#include "core/recent.h"
#include "dvv/base/json.h"
#include "dvv/media/types.h"
#include "temporary_directory.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace rawreel;
using V = std::vector<std::string>;

#if !defined(_WIN32)
TEST_CASE("recent entry: a path is its UTF-8 text") {
    CHECK(recentEntryOfPath("/data/clips/clip.yuv") == "/data/clips/clip.yuv");
    CHECK(recentEntryOfPath(std::filesystem::path(u8"/data/\u043a\u043b\u0438\u043f.yuv")) ==
          "/data/\xD0\xBA\xD0\xBB\xD0\xB8\xD0\xBF.yuv");
}
TEST_CASE("recent entry: a path with ? is its file: URI") {
    CHECK(recentEntryOfPath("/data/a?b.yuv") == "file:/data/a%3Fb.yuv");
}
TEST_CASE("recent entry: typed text") {
    const std::filesystem::path work = "/data/work";
    CHECK(recentEntryOfText("clip.yuv", work) == "/data/work/clip.yuv");
    CHECK(recentEntryOfText("../in/clip.yuv", work) ==
          "/data/work/../in/clip.yuv");  // no .. resolved
    CHECK(recentEntryOfText("clip.yuv?fmt=v210&size=8x2", work) ==
          "/data/work/clip.yuv?fmt=v210&size=8x2");
    CHECK(recentEntryOfText("/data/in/clip.yuv", work) == "/data/in/clip.yuv");
    CHECK(recentEntryOfText("synthetic:gradient?fmt=gray&size=16x16", work) ==
          "synthetic:gradient?fmt=gray&size=16x16");
    CHECK(recentEntryOfText("file:clip.yuv", work) == "file:clip.yuv");  // a file: URI as typed
    CHECK(recentEntryOfText("clip.yuv", {}) == "clip.yuv");              // no working folder: kept
}
#else
TEST_CASE("recent entry: Windows paths") {
    CHECK(recentEntryOfPath(L"X:\\clips\\clip.yuv") == "X:\\clips\\clip.yuv");
    CHECK(recentEntryOfText("clip.yuv", L"X:\\work") == "X:\\work\\clip.yuv");
    CHECK(recentEntryOfText("..\\in\\clip.yuv", L"X:\\work") == "X:\\work\\..\\in\\clip.yuv");
    CHECK(recentEntryOfText("X:\\in\\clip.yuv?fmt=v210", L"X:\\work") ==
          "X:\\in\\clip.yuv?fmt=v210");
    CHECK(recentEntryOfText("\\in\\clip.yuv", L"X:\\work") ==
          "X:\\in\\clip.yuv");  // root-relative: the folder's drive
    CHECK(recentEntryOfText("Y:clip.yuv", L"X:\\work") ==
          "Y:clip.yuv");  // drive-relative: the join keeps it
}
#endif
TEST_CASE("recent entry: validity") {
    CHECK_FALSE(recentEntryProblem("/data/clip.yuv"));
    CHECK_FALSE(recentEntryProblem(std::string(4096, 'a')));
    CHECK(recentEntryProblem(std::string(4097, 'a')) == "longer than 4096 bytes");
    CHECK(recentEntryProblem("/data/\xFF.yuv") == "not valid UTF-8");
    CHECK(recentEntryProblem("/data/\xC0\xAF.yuv") == "not valid UTF-8");      // overlong
    CHECK(recentEntryProblem("/data/\xED\xA0\x80.yuv") == "not valid UTF-8");  // surrogate
    CHECK(recentEntryProblem("/data/a\tb.yuv") == "a control character");
    CHECK(recentEntryProblem("/data/a\x7F.yuv") == "a control character");
}
TEST_CASE("recent list: add on top, move to the top, drop the oldest") {
    RecentList list(3);
    CHECK(list.add("a"));
    CHECK(list.add("b"));
    CHECK(list.add("c"));
    CHECK(list.entries() == V{"c", "b", "a"});
    CHECK(list.add("a"));
    CHECK(list.entries() == V{"a", "c", "b"});
    CHECK_FALSE(list.add("a"));  // the top again: unchanged
    CHECK(list.add("d"));
    CHECK(list.entries() == V{"d", "a", "c"});
    CHECK(list.add("D"));  // exact text
    CHECK(list.entries() == V{"D", "d", "a"});
}
TEST_CASE("recent list: remove and contains") {
    RecentList list(5, {"a", "b", "c"});
    CHECK(list.remove("b"));
    CHECK(list.entries() == V{"a", "c"});
    CHECK_FALSE(list.remove("x"));
    CHECK(list.contains("a"));
    CHECK_FALSE(list.contains("b"));
}
TEST_CASE("recent list: capacity cuts for good") {
    CHECK(RecentList(2, {"a", "b", "c"}).entries() == V{"a", "b"});
    RecentList list(5, {"a", "b", "c", "d", "e"});
    CHECK(list.setCapacity(2));
    CHECK(list.entries() == V{"a", "b"});
    CHECK_FALSE(list.setCapacity(10));
    CHECK(list.capacity() == 10);
    CHECK(list.entries() == V{"a", "b"});  // nothing comes back
    CHECK_FALSE(list.setCapacity(2));      // nothing to cut
}
TEST_CASE("recent menu: labels per OS") {
    const V entries{"/data/clips/clip_1920x1080_v210.yuv", "lavfi:testsrc2=size=1280x720",
                    "/data/a&b.yuv", "file:/data/q%3Fx.yuv"};
    auto labels = [&entries](TitleStyle style) {
        V out;
        for (const auto& item : recentMenuItems(entries, style))
            out.push_back(item.label);
        return out;
    };
    CHECK(labels(TitleStyle::MacOs) ==
          V{"clip_1920x1080_v210.yuv", "lavfi:testsrc2=size=1280x720", "a&&b.yuv", "q?x.yuv"});
    CHECK(labels(TitleStyle::Linux) == V{"&1. clip_1920x1080_v210.yuv",
                                         "&2. lavfi:testsrc2=size=1280x720", "&3. a&&b.yuv",
                                         "&4. q?x.yuv"});
    CHECK(labels(TitleStyle::Windows) == V{"&1 /data/clips/clip_1920x1080_v210.yuv",
                                           "&2 lavfi:testsrc2=size=1280x720", "&3 /data/a&&b.yuv",
                                           "&4 file:/data/q%3Fx.yuv"});
    for (const auto style : {TitleStyle::MacOs, TitleStyle::Windows, TitleStyle::Linux}) {
        const auto items = recentMenuItems(entries, style);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            CHECK(items[i].help == entries[i]);
        }
    }
    V ten;
    for (int i = 0; i < 10; ++i) {
        ten.push_back(std::format("/data/{}.yuv", i));
    }
    CHECK(recentMenuItems(ten, TitleStyle::Windows)[8].label == "&9 /data/8.yuv");
    CHECK(recentMenuItems(ten, TitleStyle::Windows)[9].label == "1&0 /data/9.yuv");
    CHECK(recentMenuItems(ten, TitleStyle::Linux)[9].label == "&10. 9.yuv");
}
TEST_CASE("recent menu: a file entry without a file name shows its full text") {
    CHECK(recentMenuItems(V{"/data/clips/"}, TitleStyle::MacOs)[0].label == "/data/clips/");
    CHECK(recentMenuItems(V{"/data/clips/"}, TitleStyle::Linux)[0].label == "&1. /data/clips/");
}
TEST_CASE("recent menu: the 60 code point cut") {
    const std::string long_uri = "lavfi:" + std::string(50, 'a') + std::string(14, 'z');  // 70
    CHECK(recentMenuItems(V{long_uri}, TitleStyle::MacOs)[0].label ==
          "lavfi:" + std::string(23, 'a') + "..." + std::string(14, 'a') + std::string(14, 'z'));
    const std::string sixty = "lavfi:" + std::string(54, 'a');
    CHECK(recentMenuItems(V{sixty}, TitleStyle::MacOs)[0].label == sixty);  // not cut
    std::string de;
    for (int i = 0; i < 60; ++i) {
        de += "\xD0\xB4";
    }  // 60 x U+0434
    const std::string cyrillic = "synthetic:" + de;  // 70 code points
    std::string left = "synthetic:", right;
    for (int i = 0; i < 19; ++i) {
        left += "\xD0\xB4";
    }
    for (int i = 0; i < 28; ++i) {
        right += "\xD0\xB4";
    }
    left += "...";
    left += right;
    CHECK(recentMenuItems(V{cyrillic}, TitleStyle::MacOs)[0].label == left);
    const std::string long_name = std::string(80, 'n') + ".yuv";  // a file name is never cut
    CHECK(recentMenuItems(V{"/data/" + long_name}, TitleStyle::Linux)[0].label ==
          "&1. " + long_name);
    CHECK(recentMenuItems(V{"/data/" + long_name}, TitleStyle::Windows)[0].label.size() == 3 + 60);
}
TEST_CASE("recent menu: the same label shows the full text") {
    const V files{"/data/a/clip.yuv", "/data/b/clip.yuv", "/data/c/other.yuv"};
    CHECK(recentMenuItems(files, TitleStyle::MacOs)[0].label == "/data/a/clip.yuv");
    CHECK(recentMenuItems(files, TitleStyle::MacOs)[1].label == "/data/b/clip.yuv");
    CHECK(recentMenuItems(files, TitleStyle::MacOs)[2].label == "other.yuv");
    CHECK(recentMenuItems(files, TitleStyle::Linux)[1].label == "&2. /data/b/clip.yuv");
    const std::string a = "lavfi:" + std::string(30, 'p') + "A" + std::string(33, 's');
    const std::string b = "lavfi:" + std::string(30, 'p') + "B" + std::string(33, 's');
    CHECK(recentMenuItems(V{a, b}, TitleStyle::Windows)[0].label == "&1 " + a);  // uncut
    CHECK(recentMenuItems(V{a, b}, TitleStyle::Windows)[1].label == "&2 " + b);
}
TEST_CASE("recent file: the check after a failed open") {
    const TemporaryDirectory directory("rawreel-recent");
    const std::filesystem::path file = directory.path() / "clip.yuv";
    std::ofstream(file, std::ios::binary) << "x";
    const std::string text = dvv::media::utf8FromPath(file);
    CHECK(recentFileState(text) == RecentFileState::Readable);
    CHECK(recentFileState(fileUri(file)) == RecentFileState::Readable);
    CHECK(recentFileState(text + "?fmt=v210&size=8x2") ==
          RecentFileState::Readable);  // the path without the query
    CHECK(recentFileState(dvv::media::utf8FromPath(directory.path() / "gone.yuv")) ==
          RecentFileState::Missing);
    CHECK(recentFileState(dvv::media::utf8FromPath(directory.path() / "no" / "gone.yuv")) ==
          RecentFileState::Missing);
    CHECK(recentFileState(dvv::media::utf8FromPath(directory.path())) ==
          RecentFileState::NotAccessible);
    CHECK(recentFileState("synthetic:gradient?fmt=gray&size=16x16") == RecentFileState::NotChecked);
    CHECK(recentFileState("lavfi:testsrc2") == RecentFileState::NotChecked);
#if !defined(_WIN32)
    if (geteuid() != 0) {  // root reads every file
        std::filesystem::permissions(file, std::filesystem::perms::none);
        CHECK(recentFileState(text) == RecentFileState::NotAccessible);
        std::filesystem::permissions(file, std::filesystem::perms::owner_all);
    }
#endif
}

namespace {
std::string sizeWarning(const std::string& v) {
    return "gui.recentSize: expected a whole number from 1 to 10, not " + v + "; using 5";
}
}  // namespace

TEST_CASE("recent config: the size") {
    CHECK(parseRecent(R"({"gui":{"recentSize":3}})").size == 3);
    CHECK(parseRecent(R"({"gui":{"recentSize":3}})").warnings.empty());
    CHECK(parseRecent("{}").size == 5);
    CHECK(parseRecent("{}").warnings.empty());
    for (const std::string bad :
         {R"("5")", "true", "null", "5.5", "5.0", "1e30", "0", "11", "-1"}) {
        const RecentConfig parsed = parseRecent(R"({"gui":{"recentSize":)" + bad + "}}");
        CHECK(parsed.size == 5);
        CHECK(parsed.warnings == V{sizeWarning(dvv::json::dump(*dvv::json::parse(bad)))});
    }
}
TEST_CASE("recent config: the list's shape") {
    CHECK(parseRecent(R"({"gui":{}})").entries.empty());
    CHECK(parseRecent(R"({"gui":{}})").warnings.empty());
    for (const std::string bad : {R"("x")", "3", "null", "{}"}) {
        const RecentConfig parsed = parseRecent(R"({"gui":{"recent":)" + bad + "}}");
        CHECK(parsed.entries.empty());
        CHECK(parsed.warnings ==
              V{"gui.recent: expected an array of strings, not " + bad + "; the list is empty"});
    }
}
TEST_CASE("recent config: entries dropped, the newest duplicate kept") {
    const RecentConfig parsed =
        parseRecent(R"({"gui":{"recent":["/a", 1, true, "", "/b\u0001", "/a", "/c"]}})");
    CHECK(parsed.entries == V{"/a", "/c"});
    CHECK(parsed.warnings ==
          V{"gui.recent: ignored 2 entries that are not strings, 1 empty, 1 invalid, 1 duplicate"});
    CHECK(parseRecent(R"({"gui":{"recent":["/a","/a","/a"]}})").warnings ==
          V{"gui.recent: ignored 2 duplicates"});
    CHECK(parseRecent(R"({"gui":{"recent":[1]}})").warnings ==
          V{"gui.recent: ignored 1 entry that is not a string"});
    const std::string long_entry(4097, 'a');
    CHECK(parseRecent(R"({"gui":{"recent":[")" + long_entry + R"("]}})").warnings ==
          V{"gui.recent: ignored 1 invalid"});
    CHECK(parseRecent(R"({"gui":{"recent":["clip.yuv","bad:","/data/clips/"]}})").entries ==
          V{"clip.yuv", "bad:", "/data/clips/"});  // kept
}
TEST_CASE("recent config: more entries than the size, rules in order") {
    const RecentConfig seven = parseRecent(R"({"gui":{"recent":["1","2","3","4","5","6","7"]}})");
    CHECK(seven.entries == V{"1", "2", "3", "4", "5"});
    CHECK(seven.warnings ==
          V{"gui.recent: 7 entries, more than gui.recentSize 5; the first 5 kept"});
    const RecentConfig mixed =
        parseRecent(R"({"gui":{"recentSize":2,"recent":["/a",1,"/a","/b","/c"]}})");
    CHECK(mixed.entries == V{"/a", "/b"});
    CHECK(mixed.warnings ==
          V{"gui.recent: ignored 1 entry that is not a string, 1 duplicate",
            "gui.recent: 3 entries, more than gui.recentSize 2; the first 2 kept"});
    const RecentConfig both =
        parseRecent(R"({"gui":{"recentSize":"x","recent":["1","2","3","4","5","6"]}})");
    CHECK(both.warnings ==
          V{sizeWarning(R"("x")"),
            "gui.recent: 6 entries, more than gui.recentSize 5; the first 5 kept"});
}
TEST_CASE("recent config: a gui that is no object gives the defaults") {
    const RecentConfig parsed = parseRecent(R"({"gui":3})");
    CHECK(parsed.size == 5);
    CHECK(parsed.entries.empty());
    CHECK(parsed.warnings.empty());
}

namespace {
using O = std::optional<std::string>;
std::vector<StreamInfo> one(const std::string& uri) {
    StreamInfo stream;
    stream.requestedUri = uri;
    return {stream};
}
}  // namespace

TEST_CASE("recent pending: the open's stream records its entry once") {
    RecentPending pending;
    pending.opened("e", "u");
    CHECK(pending.streamsChanged(one("u")) == O{"e"});
    CHECK_FALSE(pending.entry());
    CHECK_FALSE(pending.streamsChanged(one("u")));
}
TEST_CASE("recent pending: a stale stream of an older open records nothing") {
    RecentPending pending;
    pending.opened("old", "u1");
    pending.opened("new", "u2");  // the older open's streamsChanged is still queued
    CHECK_FALSE(pending.streamsChanged(one("u1")));
    CHECK(pending.entry() == O{"new"});
    CHECK(pending.streamsChanged(one("u2")) == O{"new"});
}
TEST_CASE("recent pending: the open's own close keeps it") {
    RecentPending pending;
    pending.opened("e", "u");
    CHECK_FALSE(pending.streamsChanged({}));  // closeStreams' empty streamsChanged
    CHECK(pending.entry() == O{"e"});
}
TEST_CASE("recent pending: an open without an entry clears it") {
    RecentPending pending;
    pending.opened("e", "u");
    pending.opened(std::nullopt, "u2");  // View > Format...
    CHECK_FALSE(pending.entry());
    CHECK_FALSE(pending.streamsChanged(one("u")));
    CHECK_FALSE(pending.streamsChanged(one("u2")));
}
TEST_CASE("recent pending: format needed moves the entry to its dialog") {
    RecentPending pending;
    pending.opened("e", "u");
    CHECK_FALSE(pending.formatNeeded("other"));  // an older open's
    CHECK(pending.entry() == O{"e"});
    CHECK(pending.formatNeeded("u") == O{"e"});
    CHECK_FALSE(pending.entry());
}
TEST_CASE("recent pending: close stream clears it") {
    RecentPending pending;
    pending.opened("e", "u");
    pending.closed();
    CHECK_FALSE(pending.entry());
    CHECK_FALSE(pending.streamsChanged(one("u")));
}
TEST_CASE("recent config: a large list with many duplicates") {
    std::string text = R"({"gui":{"recentSize":10,"recent":[)";
    for (int i = 0; i < 20000; ++i) {
        text += std::format("{}\"/d/{}\"", i == 0 ? "" : ",", i);
    }
    for (int i = 0; i < 20000; ++i) {
        text += std::format(",\"/d/{}\"", i);
    }
    text += "]}}";
    const RecentConfig parsed = parseRecent(text);
    CHECK(parsed.entries.size() == 10);
    CHECK(parsed.entries.front() == "/d/0");
    CHECK(parsed.warnings ==
          V{"gui.recent: ignored 20000 duplicates",
            "gui.recent: 20000 entries, more than gui.recentSize 10; the first 10 kept"});
}
