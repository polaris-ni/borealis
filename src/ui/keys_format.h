#pragma once

// ============================================================
// SSH 密钥面板纯格式化函数（src/ui/keys_format.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.10` 面板层的「最后一步」：行模型的徽标与数据段、四族状态→词条 key
// 的映射（加密两态 / 公钥缺失 / 名称与注释校验留痕 / 目录不可达留痕）。全部只依赖标准类型
// 与 `conn/key_model.h` 的枚举，不含 aurora/au:: 类型、不碰文件系统与 UI
// （`sftp_format`/`tunnel_format` 同族先例，AGENTS.md §4.4 第 20 条 ⇒ `utest_keys_format` 无头单测）。
//
// 词条分工照 `tunnel_state_key`：本件是**唯一**把 `KeyCandidate`/`NameIssue` 翻成词条 key 的地方，
// 面板不 switch 出第二套措辞（裁决 7.25⑬）；上屏文案一律经 `ui::settings_label(key)` 现取，
// 表里那些 key 的中文词条随批 3 的面板一并登记（批 1 只交付映射本身）。
//
// **本件不含任何阻塞 IO 与 libssh 类型**，也**不含尚未到货的那两族枚举**：
// 「生成三态」与「推送四态」的映射要等 `conn/key_store`、`conn/key_push`（稿 §8 的批 2）把
// 它们的阶段枚举立起来——在这里先写一份占位枚举就是造第二个真值源（AGENTS 第 3 条）。
// 推送的失败归因同理随批 2 到货（稿 §2 判据 6）：`conn::DialOutcome` 实测**五值**
// （`Ok` 加四档失败：会话未分配 / 网络 / 主机密钥 / 认证），另两档「执行被拒」「写回执
// 异常」不属该枚举，要等 `key_push` 自己的阶段枚举立起来——本件一处都不预先映射。
// ============================================================

#include <string>
#include <string_view>

#include "conn/key_model.h"  // KeyCandidate/KeyDirEntry/NameIssue/CommentIssue（非框架类型）

namespace borealis::ui {

/// @brief 类型徽标（稿 §2 判据 2 的首行短标）：`ed25519`、`rsa 3072`。
///
/// RSA 带位数是因为三档在同一列表里必须一眼分得开；ed25519 无位数概念，故**不带**那个空字段。
/// `Unknown` 回空串＝这行没有类型徽标。批 2 落地时按代码改口（本件原注记写的是「认不出算法族的
/// 行本就不该进行表，扫盘腿负责过滤」）：扫盘腿按 D2① 的行源**照收**两类——
/// ecdsa/dsa 等两族之外的钥匙（需求那句「ed25519 优先/RSA 备选」管的是生成下拉，不是列表），
/// 以及带口令又无 `.pub` 的钥匙（F8 探得出不解密，类型无从取得）。丢行比丢徽标糟：行还在列表里，
/// 用户看得见自己的东西，也能删得掉；而这里不留一套自造措辞。
[[nodiscard]] auto key_type_badge(const conn::KeyCandidate &row) -> std::string;

/// @brief 加密两态 → 词条 key：`keys.row.has_passphrase` / `keys.row.no_passphrase`。
///
/// D3① 的结论只有这两档，且它是**探测**结果不是解密结果；两档都得有文案，
/// 因为「看不出哪把有口令」正是这条裁决要消灭的那件事。
[[nodiscard]] auto key_passphrase_key(const conn::KeyCandidate &row) -> std::string_view;

/// @brief 公钥缺失 → `keys.row.public_missing`；配对 `.pub` 在场时回空串（无留痕就不取文案）。
///
/// 这一档把行内动作从「复制 / 推送」换成「导出公钥」（稿 §2 判据 3），动作归属由面板按本 key
/// 是否非空决定，措辞不在面板里。
[[nodiscard]] auto key_public_state_key(const conn::KeyCandidate &row) -> std::string_view;

/// @brief 次行数据段：`<指纹> <全路径>`，单空格连接、无尾随空白。
///
/// 指纹为空时只交路径（探测失败不留一枚空徽标）；多目录后 basename 会撞，
/// 故路径**恒在**且是唯一键那一半的可见兑现（D2③ 细则⑷）。
[[nodiscard]] auto key_secondary_line(const conn::KeyCandidate &row) -> std::string;

/// @brief 名称校验 → 词条 key（`keys.issue.name.*`）；`None` 回空串＝不留痕。
[[nodiscard]] auto key_name_issue_key(conn::NameIssue issue) -> std::string_view;

/// @brief 注释校验 → 词条 key（`keys.issue.comment.*`）；`None` 回空串。
[[nodiscard]] auto key_comment_issue_key(conn::CommentIssue issue) -> std::string_view;

/// @brief 目录不可达 → `keys.dir.unreachable`；可达时回空串。
///
/// D2③ 细则⑶ 的「保留条目并行内留痕」在文案侧的那一半：条目还在表里，这里只负责让它说得出口。
[[nodiscard]] auto key_dir_note_key(const conn::KeyDirEntry &dir) -> std::string_view;

}  // namespace borealis::ui
