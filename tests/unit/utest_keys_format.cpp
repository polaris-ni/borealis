/// 测试类型: unit
/// 目标单元: src/ui/keys_format.{h,cpp}
/// 测试说明: SPEC.FEAT.CONN.10 批 1 的映射层（稿 §4 给这一件的判据＝「枚举→词条 key 的唯一映射」，
///           无头、无 IO）：类型徽标三档、加密两态、公钥缺失那一档、次行数据段（路径恒在）、
///           名称与注释校验留痕的**逐值互异**（两个问题共用一枚词条就等于面板自己再措辞一次）、
///           目录不可达留痕。批 2 的三档结果（生成／导出／删除）与推送的步骤态＋失败归因随
///           `conn/key_store`、`conn/key_push` 的枚举到货一并映射——判据仍是同一句「逐值互异」：
///           两档共用一枚词条就等于面板自己再措辞一次。稿 §4 说的「生成三态」不在本件：那是
///           装配层快照的属性（排队中／在跑／已完），一次阻塞调用没有中间态可映射。

#include "ui/keys_format.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "conn/key_model.h"
#include "conn/key_push.h"
#include "conn/key_store.h"
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

// ---- 批 2 到货的三族结果档位与推送两族：全量列表 + 逐值互异的通用核对 ----

[[nodiscard]] auto all_generate_outcomes() -> std::vector<conn::GenerateOutcome> {
    return {conn::GenerateOutcome::Created, conn::GenerateOutcome::InvalidRequest,
            conn::GenerateOutcome::NameTaken, conn::GenerateOutcome::DirectoryUnusable,
            conn::GenerateOutcome::Failed};
}

[[nodiscard]] auto all_export_outcomes() -> std::vector<conn::ExportOutcome> {
    return {conn::ExportOutcome::Written, conn::ExportOutcome::AlreadyExists,
            conn::ExportOutcome::NeedsPassphrase, conn::ExportOutcome::Failed};
}

[[nodiscard]] auto all_delete_outcomes() -> std::vector<conn::DeleteOutcome> {
    return {conn::DeleteOutcome::Deleted, conn::DeleteOutcome::RefusedSymlink,
            conn::DeleteOutcome::NotFound, conn::DeleteOutcome::Failed};
}

[[nodiscard]] auto all_push_stages() -> std::vector<conn::PushStage> {
    return {conn::PushStage::Dial, conn::PushStage::Auth, conn::PushStage::Exec,
            conn::PushStage::Done};
}

[[nodiscard]] auto all_push_failures() -> std::vector<conn::PushFailure> {
    return {conn::PushFailure::DialUnallocated, conn::PushFailure::DialNetwork,
            conn::PushFailure::DialHostKey, conn::PushFailure::DialAuth,
            conn::PushFailure::ExecRefused, conn::PushFailure::WriteRejected,
            conn::PushFailure::InvalidLine};
}

/// @brief 一族结果档位的通用核对：成功档回空串（不留痕），其余档同前缀且互不相同。
template <typename Enum, typename Map>
auto check_outcome_family(std::string_view prefix, const std::vector<Enum> &values, Enum success_value,
                          Map map) -> void {
    auto seen = std::set<std::string_view>{};
    for (const auto value : values) {
        const auto key = map(value);
        if (value == success_value) {
            AURORA_TEST_CHECK_MSG(key.empty(), "success leaves no notice");
            continue;
        }
        AURORA_TEST_CHECK_MSG(key.starts_with(prefix), "one namespace per family");
        AURORA_TEST_CHECK_MSG(seen.insert(key).second, "distinct key per outcome");
    }
    AURORA_TEST_CHECK_EQ(seen.size(), values.size() - 1U);
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

AURORA_TEST_CASE(the_three_action_families_keep_success_silent_and_failures_distinct) {
    check_outcome_family("keys.generate.", all_generate_outcomes(), conn::GenerateOutcome::Created,
                         ui::key_generate_outcome_key);
    check_outcome_family("keys.export.", all_export_outcomes(), conn::ExportOutcome::Written,
                         ui::key_export_outcome_key);
    check_outcome_family("keys.delete.", all_delete_outcomes(), conn::DeleteOutcome::Deleted,
                         ui::key_delete_outcome_key);
    // 导出那一档里 `NeedsPassphrase` **不是**失败：它是 D12⑵ 反话的正 half（删除不问、导出必问），
    // 混进 `keys.export.failed` 就等于把「该弹询问」说成「办砸了」。
    AURORA_TEST_CHECK_MSG(
        ui::key_export_outcome_key(conn::ExportOutcome::NeedsPassphrase) == "keys.export.needs_passphrase",
        "the passphrase prompt has its own wording");
}

AURORA_TEST_CASE(the_push_ladder_has_a_label_per_step_and_a_word_per_finding) {
    auto labels = std::set<std::string_view>{};
    for (const auto stage : all_push_stages()) {
        const auto key = ui::key_push_stage_key(stage);
        // 阶梯四格是常驻文案：**没有**回空串的一档（空 label 的格子在面板上是个洞）。
        AURORA_TEST_CHECK_MSG(!key.empty(), "every ladder step is labelled");
        AURORA_TEST_CHECK_MSG(key.starts_with("keys.push.stage."), "one namespace for the ladder");
        AURORA_TEST_CHECK_MSG(labels.insert(key).second, "distinct label per step");
    }
    AURORA_TEST_CHECK_EQ(labels.size(), all_push_stages().size());

    // 失败归因走 `key_push_notice_key()`：面板手上只有终值报告，没有裸档位可问（唯一裁决处）。
    auto findings = std::set<std::string_view>{};
    for (const auto failure : all_push_failures()) {
        auto report = conn::PushReport{};
        report.stage = conn::PushStage::Exec;
        report.failure = failure;
        const auto key = ui::key_push_notice_key(report);
        AURORA_TEST_CHECK_MSG(key.starts_with("keys.push.fail."), "one namespace for findings");
        AURORA_TEST_CHECK_MSG(findings.insert(key).second, "distinct key per finding");
    }
    // 七档：稿 §2 判据 6 的「四档 DialOutcome＋两档推送腿」按代码是七档（`InvalidLine` 判在拨号之前）。
    AURORA_TEST_CHECK_EQ(findings.size(), 7U);
}

AURORA_TEST_CASE(the_push_notice_chooses_one_of_three_wordings_by_priority) {
    auto authorized = conn::PushReport{};
    authorized.stage = conn::PushStage::Done;
    authorized.already_authorized = true;
    AURORA_TEST_CHECK_MSG(ui::key_push_notice_key(authorized) == "keys.push.already_authorized",
                          "no write happened, say that");

    auto appended = conn::PushReport{};
    appended.stage = conn::PushStage::Done;
    AURORA_TEST_CHECK_MSG(ui::key_push_notice_key(appended) == "keys.push.appended", "a write happened");

    // 快照的缺省值（任务还没回来）既不是「已追加」也不是「已在表里」：留痕位必须空着。
    AURORA_TEST_CHECK_TRUE(ui::key_push_notice_key(conn::PushReport{}).empty());

    // 失败优先，哪怕报告同时带着 `already_authorized`——那是一份自相矛盾的快照，
    // 报一句好消息是最坏的处理。
    auto contradictory = conn::PushReport{};
    contradictory.stage = conn::PushStage::Exec;
    contradictory.failure = conn::PushFailure::WriteRejected;
    contradictory.already_authorized = true;
    AURORA_TEST_CHECK_MSG(ui::key_push_notice_key(contradictory) == "keys.push.fail.write_rejected",
                          "a failure never reads as good news");
}

}  // namespace borealis::test_cases::utest_keys_format
