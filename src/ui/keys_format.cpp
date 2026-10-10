// ============================================================
// 密钥面板纯格式化实现（src/ui/keys_format.cpp）——口径见 keys_format.h 头注
// 与 codespec/UI_KEYS.draft.md §2/§3/§4。
// ============================================================

#include "keys_format.h"

#include <string>

#include "conn/key_push.h"    // PushReport 的定义（头里只有前置声明）
#include "conn/key_store.h"   // GenerateOutcome/ExportOutcome/DeleteOutcome 的定义

namespace borealis::ui {

namespace {

/// @brief 推送失败归因 → 词条 key（`keys.push.fail.*`）；只有本文件之内用得到它——
///        面板要的是「这一趟说得出口的那句话」，那一句由 `key_push_notice_key()` 裁决。
[[nodiscard]] auto key_push_failure_key(conn::PushFailure failure) -> std::string_view {
    switch (failure) {
    case conn::PushFailure::DialUnallocated:
        return "keys.push.fail.session_unallocated";
    case conn::PushFailure::DialNetwork:
        return "keys.push.fail.network";
    case conn::PushFailure::DialHostKey:
        return "keys.push.fail.host_key";
    case conn::PushFailure::DialAuth:
        return "keys.push.fail.auth";
    case conn::PushFailure::ExecRefused:
        return "keys.push.fail.exec_refused";
    case conn::PushFailure::WriteRejected:
        return "keys.push.fail.write_rejected";
    case conn::PushFailure::InvalidLine:
        return "keys.push.fail.invalid_line";
    }
    // 穷尽后不可达：走到这里只可能是枚举加了新值而此处没跟上。按最接近的一档报，
    // 好过让一次失败顶着一句空措辞上屏——`key_name_issue_key()` 那种「None 回空」的
    // 收尾在这里不适用，因为本枚举压根没有「无失败」值。
    return "keys.push.fail.network";
}

}  // namespace

auto key_type_badge(const conn::KeyCandidate &row) -> std::string {
    switch (row.type) {
    case conn::KeyType::Ed25519:
        return "ed25519";
    case conn::KeyType::Rsa:
        // 位数是 RSA 三档唯一分得开的地方；扫盘没给出位数时不留一枚尾随空格。
        return row.bits == 0U ? std::string{"rsa"} : "rsa " + std::to_string(row.bits);
    case conn::KeyType::Unknown:
        break;
    }
    return {};
}

auto key_passphrase_key(const conn::KeyCandidate &row) -> std::string_view {
    return row.encrypted ? "keys.row.has_passphrase" : "keys.row.no_passphrase";
}

auto key_public_state_key(const conn::KeyCandidate &row) -> std::string_view {
    return row.has_public ? std::string_view{} : "keys.row.public_missing";
}

auto key_secondary_line(const conn::KeyCandidate &row) -> std::string {
    if (row.fingerprint.empty()) {
        return row.path;
    }
    return row.fingerprint + " " + row.path;
}

auto key_name_issue_key(conn::NameIssue issue) -> std::string_view {
    switch (issue) {
    case conn::NameIssue::None:
        return {};
    case conn::NameIssue::Empty:
        return "keys.issue.name.empty";
    case conn::NameIssue::TooLong:
        return "keys.issue.name.too_long";
    case conn::NameIssue::StartsDot:
        // 词条说的是「点开头就成了隐藏文件」这件事，不是说字符类——面板不该解释实现细节。
        return "keys.issue.name.hidden";
    case conn::NameIssue::HasSeparator:
        return "keys.issue.name.has_separator";
    case conn::NameIssue::HasControlChar:
        return "keys.issue.name.control_char";
    case conn::NameIssue::Reserved:
        return "keys.issue.name.reserved";
    }
    return {};
}

auto key_comment_issue_key(conn::CommentIssue issue) -> std::string_view {
    switch (issue) {
    case conn::CommentIssue::None:
        return {};
    case conn::CommentIssue::TooLong:
        return "keys.issue.comment.too_long";
    case conn::CommentIssue::HasControlChar:
        return "keys.issue.comment.control_char";
    }
    return {};
}

auto key_dir_note_key(const conn::KeyDirEntry &dir) -> std::string_view {
    return dir.reachable ? std::string_view{} : "keys.dir.unreachable";
}

auto key_generate_outcome_key(conn::GenerateOutcome outcome) -> std::string_view {
    switch (outcome) {
    case conn::GenerateOutcome::Created:
        return {};  // 成功不留痕：列表少一行才是它唯一的证据（行表由调用方重扫）。
    case conn::GenerateOutcome::InvalidRequest:
        return "keys.generate.invalid_request";
    case conn::GenerateOutcome::NameTaken:
        return "keys.generate.name_taken";
    case conn::GenerateOutcome::DirectoryUnusable:
        return "keys.generate.directory_unusable";
    case conn::GenerateOutcome::Failed:
        return "keys.generate.failed";
    }
    return {};
}

auto key_export_outcome_key(conn::ExportOutcome outcome) -> std::string_view {
    switch (outcome) {
    case conn::ExportOutcome::Written:
        return {};
    case conn::ExportOutcome::AlreadyExists:
        return "keys.export.already_exists";
    case conn::ExportOutcome::NeedsPassphrase:
        return "keys.export.needs_passphrase";
    case conn::ExportOutcome::Failed:
        return "keys.export.failed";
    }
    return {};
}

auto key_delete_outcome_key(conn::DeleteOutcome outcome) -> std::string_view {
    switch (outcome) {
    case conn::DeleteOutcome::Deleted:
        return {};
    case conn::DeleteOutcome::RefusedSymlink:
        return "keys.delete.refused_symlink";
    case conn::DeleteOutcome::NotFound:
        return "keys.delete.not_found";
    case conn::DeleteOutcome::Failed:
        return "keys.delete.failed";
    }
    return {};
}

auto key_push_stage_key(conn::PushStage stage) -> std::string_view {
    switch (stage) {
    case conn::PushStage::Dial:
        return "keys.push.stage.dial";
    case conn::PushStage::Auth:
        return "keys.push.stage.auth";
    case conn::PushStage::Exec:
        return "keys.push.stage.exec";
    case conn::PushStage::Done:
        return "keys.push.stage.done";
    }
    return "keys.push.stage.dial";  // 穷尽后不可达：阶梯不能有一格没有 label。
}

auto key_push_notice_key(const conn::PushReport &report) -> std::string_view {
    if (report.failure.has_value()) {
        return key_push_failure_key(*report.failure);
    }
    if (report.stage != conn::PushStage::Done) {
        // 装配层的快照在任务回来之前放的就是这样一个缺省值：既不报喜也不报错，留痕位空着。
        return {};
    }
    // 「已在授权表里」先判：它成功且**没发写**，与「刚追加了一行」是两句不同的话（判据 §6）。
    return report.already_authorized ? "keys.push.already_authorized" : "keys.push.appended";
}

}  // namespace borealis::ui
