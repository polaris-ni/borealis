/// 测试类型: unit
/// 目标单元: src/ui/keys_format.{h,cpp}
/// 测试说明: SPEC.FEAT.CONN.10 批 1 的映射层（稿 §4 给这一件的判据＝「枚举→词条 key 的唯一映射」，
///           无头、无 IO）：类型徽标三档、加密两态、公钥缺失那一档、次行数据段（路径恒在）、
///           名称与注释校验留痕的**逐值互异**（两个问题共用一枚词条就等于面板自己再措辞一次）、
///           目录不可达留痕。推送四态与生成三态随批 2 的枚举到货，本件不预先造第二真值源。

#include "ui/keys_format.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "conn/key_model.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_keys_format {

namespace {

using namespace borealis;

[[nodiscard]] auto candidate(std::string path, std::string basename, conn::KeyType type,
                             std::uint16_t bits = 0U, bool has_public = true, bool encrypted = false,
                             std::string fingerprint = "SHA256:AAAA1111BBBB2222") -> conn::KeyCandidate {
    auto row = conn::KeyCandidate{};
    row.path = std::move(path);
    row.basename = std::move(basename);
    row.type = type;
    row.bits = bits;
    row.has_public = has_public;
    row.encrypted = encrypted;
    row.fingerprint = std::move(fingerprint);
    return row;
}

/// @brief 全量枚举值：新增一档却忘了配词条，会在这里以「两档共用一 key」或「回空串」现形。
[[nodiscard]] auto all_name_issues() -> std::vector<conn::NameIssue> {
    return {
        conn::NameIssue::None,        conn::NameIssue::Empty,     conn::NameIssue::TooLong,
        conn::NameIssue::StartsDot,   conn::NameIssue::HasSeparator,
        conn::NameIssue::HasControlChar, conn::NameIssue::Reserved,
    };
}

[[nodiscard]] auto all_comment_issues() -> std::vector<conn::CommentIssue> {
    return {conn::CommentIssue::None, conn::CommentIssue::TooLong, conn::CommentIssue::HasControlChar};
}

}  // namespace

AURORA_TEST_CASE(badge_separates_the_three_rsa_gears_and_says_nothing_for_unknown) {
    AURORA_TEST_CHECK_MSG(ui::key_type_badge(candidate("/k", "k", conn::KeyType::Ed25519)) == "ed25519",
                          "ed25519 carries no empty bits field");
    AURORA_TEST_CHECK_MSG(
        ui::key_type_badge(candidate("/k", "k", conn::KeyType::Rsa, 2048U)) == "rsa 2048", "rsa 2048");
    AURORA_TEST_CHECK_MSG(
        ui::key_type_badge(candidate("/k", "k", conn::KeyType::Rsa, 4096U)) == "rsa 4096", "rsa 4096");
    // 三档徽标必须互不相同——同一列表里分不开就等于没有这个下拉。
    auto badges = std::set<std::string>{};
    for (const auto bits : {2048U, 3072U, 4096U}) {
        badges.insert(ui::key_type_badge(candidate("/k", "k", conn::KeyType::Rsa, static_cast<std::uint16_t>(bits))));
    }
    AURORA_TEST_CHECK_EQ(badges.size(), 3U);
    // 认不出算法族的行本就不该进行表；这里不留一套自造措辞。
    AURORA_TEST_CHECK_TRUE(ui::key_type_badge(candidate("/k", "k", conn::KeyType::Unknown)).empty());
    // 扫盘没给出位数时不留尾随空格。
    AURORA_TEST_CHECK_MSG(ui::key_type_badge(candidate("/k", "k", conn::KeyType::Rsa)) == "rsa",
                          "no dangling space");
}

AURORA_TEST_CASE(both_passphrase_states_get_their_own_word) {
    // D3①：两档都得说得出口，「看不出哪把有口令」正是这条裁决要消灭的那件事。
    const auto locked = ui::key_passphrase_key(candidate("/k", "k", conn::KeyType::Ed25519, 0U, true, true));
    const auto open = ui::key_passphrase_key(candidate("/k", "k", conn::KeyType::Ed25519, 0U, true, false));
    AURORA_TEST_CHECK_MSG(locked == "keys.row.has_passphrase", "with passphrase");
    AURORA_TEST_CHECK_MSG(open == "keys.row.no_passphrase", "without passphrase");
    AURORA_TEST_CHECK_MSG(locked != open, "the two states are not the same key");
}

AURORA_TEST_CASE(only_a_missing_pub_asks_for_a_row_note) {
    AURORA_TEST_CHECK_MSG(
        ui::key_public_state_key(candidate("/k", "k", conn::KeyType::Ed25519, 0U, /*has_public=*/false)) ==
            "keys.row.public_missing",
        "orphan private key row");
    AURORA_TEST_CHECK_TRUE(
        ui::key_public_state_key(candidate("/k", "k", conn::KeyType::Ed25519, 0U, /*has_public=*/true)).empty());
}

AURORA_TEST_CASE(the_secondary_line_always_carries_the_full_path) {
    const auto full = ui::key_secondary_line(
        candidate("/home/dev/.ssh/id_ed25519", "id_ed25519", conn::KeyType::Ed25519));
    AURORA_TEST_CHECK_MSG(full == "SHA256:AAAA1111BBBB2222 /home/dev/.ssh/id_ed25519", "fingerprint then path");
    AURORA_TEST_CHECK_MSG(full.back() != ' ', "no trailing blank");
    // 指纹探测失败 ⇒ 只交路径：不留一枚空徽标，也不把唯一键那一半一起丢掉。
    const auto nofp = ui::key_secondary_line(
        candidate("/srv/keys/id_rsa", "id_rsa", conn::KeyType::Rsa, 3072U, true, false, {}));
    AURORA_TEST_CHECK_MSG(nofp == "/srv/keys/id_rsa", "path survives without a fingerprint");
}

AURORA_TEST_CASE(every_name_issue_maps_to_its_own_key) {
    auto seen = std::set<std::string_view>{};
    for (const auto issue : all_name_issues()) {
        const auto key = ui::key_name_issue_key(issue);
        if (issue == conn::NameIssue::None) {
            // None＝不留痕：非空就会让面板在合法输入上说一句话。
            AURORA_TEST_CHECK_MSG(key.empty(), "None asks for no wording");
            continue;
        }
        AURORA_TEST_CHECK_MSG(key.starts_with("keys.issue.name."), "one namespace for name issues");
        // 两档共用一枚词条＝面板自己再措辞一次，判据在这里挡死。
        AURORA_TEST_CHECK_MSG(seen.insert(key).second, "distinct key per issue");
    }
    AURORA_TEST_CHECK_EQ(seen.size(), all_name_issues().size() - 1U);
}

AURORA_TEST_CASE(every_comment_issue_maps_to_its_own_key) {
    auto seen = std::set<std::string_view>{};
    for (const auto issue : all_comment_issues()) {
        const auto key = ui::key_comment_issue_key(issue);
        if (issue == conn::CommentIssue::None) {
            AURORA_TEST_CHECK_MSG(key.empty(), "None asks for no wording");
            continue;
        }
        AURORA_TEST_CHECK_MSG(key.starts_with("keys.issue.comment."), "one namespace for comment issues");
        AURORA_TEST_CHECK_MSG(seen.insert(key).second, "distinct key per issue");
    }
    AURORA_TEST_CHECK_EQ(seen.size(), all_comment_issues().size() - 1U);
}

AURORA_TEST_CASE(an_unreachable_dir_is_the_only_note_a_directory_can_ask_for) {
    // D2③ 细则⑶ 的文案侧：条目留在表里，这里只负责让「扫不到」说得出口。
    const auto dead = conn::KeyDirEntry{"/srv/gone", false, false};
    const auto alive = conn::KeyDirEntry{"/srv/here", false, true};
    const auto home = conn::KeyDirEntry{"/home/dev/.ssh", true, true};
    AURORA_TEST_CHECK_MSG(ui::key_dir_note_key(dead) == "keys.dir.unreachable", "unreachable gets a note");
    AURORA_TEST_CHECK_TRUE(ui::key_dir_note_key(alive).empty());
    // 「恒在且可达」的缺省条目不该有任何角标——首屏零配置可用（PREF.06）的那一眼。
    AURORA_TEST_CHECK_TRUE(ui::key_dir_note_key(home).empty());
}

}  // namespace borealis::test_cases::utest_keys_format
