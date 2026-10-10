// ============================================================
// 密钥面板纯格式化实现（src/ui/keys_format.cpp）——口径见 keys_format.h 头注
// 与 codespec/UI_KEYS.draft.md §2/§3/§4。
// ============================================================

#include "keys_format.h"

#include <string>

namespace borealis::ui {

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

}  // namespace borealis::ui
