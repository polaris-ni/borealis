#pragma once

// ============================================================
// SSH 密钥面板纯格式化函数（src/ui/keys_format.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.10` 面板层的「最后一步」：行模型的徽标与数据段、状态→词条 key 的映射
// （批 1 的四族：加密两态 / 公钥缺失 / 名称与注释校验留痕 / 目录不可达留痕；批 2 到货的
// 生成、导出、删除三档结果与推送的步骤态＋失败归因）。全部只依赖标准类型与 `conn/key_model.h`
// 的枚举，不含 aurora/au:: 类型、不碰文件系统与 UI
// （`sftp_format`/`tunnel_format` 同族先例，AGENTS.md §4.4 第 20 条 ⇒ `utest_keys_format` 无头单测）。
//
// 词条分工照 `tunnel_state_key`：本件是**唯一**把 `KeyCandidate`/`NameIssue`/批 2 那几族枚举
// 翻成词条 key 的地方，面板不 switch 出第二套措辞（裁决 7.25⑬）；上屏文案一律经
// `ui::settings_label(key)` 现取，表里那些 key 的中文词条随批 3 的面板一并登记（批 1/2 只交付映射本身）。
//
// **本件不含任何阻塞 IO 与 libssh 类型**：批 2 的三条传输腿头文件**没有被包含进来**——
// 这里只前置声明用到的枚举与 `PushReport`（固定底层类型的枚举可以作 opaque 声明，
// `ssh_dial.h` 前向声明 `ssh_session_struct` 同法）。理由是上面那句「只依赖标准类型与 key_model」
// 必须是物理事实而非口径：面板经本件够得着传输腿的函数签名，就等于给「动作一律经 Hooks 交回
// 装配层」（裁决 7.97 D5①）开了一道门。
//
// **两条与稿不同的口径按代码记在这里**（落地期订正，随批 2 的裁决登记）：
// ⑴ 稿 §2 判据 6 写失败归因＝`conn::DialOutcome` 的四档失败再加推送腿自证的两档（六档措辞）。
//    实际到货的是 `conn::PushFailure` **七档**：`key_push` 把 `DialOutcome` 一比一翻成自己的档位
//    （面板因此只认一个枚举、不必两处判同一条失败），另多出的 `InvalidLine` 是**拨号之前**的闸
//    （废行不拨号），它既不是 DialOutcome 也不是 exec 腿的失败，塞进别档会说谎。
// ⑵ 稿 §4 的「生成三态」（排队中／在跑／已完）是**装配层 latest-value 快照**的属性：一次阻塞
//    调用没有中间态可映射（`key_store.h` 头注同口径），故本件不造那三档，只映射 `GenerateOutcome`。
// ============================================================

#include <cstdint>
#include <string>
#include <string_view>

#include "conn/key_model.h"  // KeyCandidate/KeyDirEntry/NameIssue/CommentIssue（非框架类型）

namespace borealis::conn {
// 批 2 传输腿交回的档位（在此只作前置声明，理由见头注）：定义在 `conn/key_store.h` 与
// `conn/key_push.h`，两边都是固定底层类型，opaque 声明与定义可并存。
enum class GenerateOutcome : std::uint8_t;
enum class ExportOutcome : std::uint8_t;
enum class DeleteOutcome : std::uint8_t;
enum class PushStage : std::uint8_t;
enum class PushFailure : std::uint8_t;
struct PushReport;
}  // namespace borealis::conn

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

/// @brief 生成结果 → 词条 key（`keys.generate.*`）；`Created` 回空串＝成功不留痕。
///
/// 四档失败都是「一物未动 / 半成品已回滚」之后的可复述事实，面板据此把红字说清楚，
/// 而不是笼统一句「生成失败」（稿 §2 判据 5 的那枚置灰与提示）。
[[nodiscard]] auto key_generate_outcome_key(conn::GenerateOutcome outcome) -> std::string_view;

/// @brief 导出公钥结果 → 词条 key（`keys.export.*`）；`Written` 回空串。
///
/// `NeedsPassphrase` 是 D12⑵ 那句反话的正 half：删除不问口令，导出**必须**问——
/// 这一档就是那枚询问的入口，措辞不能糊成「失败」。
[[nodiscard]] auto key_export_outcome_key(conn::ExportOutcome outcome) -> std::string_view;

/// @brief 删除结果 → 词条 key（`keys.delete.*`）；`Deleted` 回空串（D12⑷ 的「不静默」）。
///
/// `RefusedSymlink` 那句要说的是「链接与目标都按原样留着」——只报「删不了」会让人以为
/// 是自己的权限问题，而这里拦住的是本仓自己的闸。
[[nodiscard]] auto key_delete_outcome_key(conn::DeleteOutcome outcome) -> std::string_view;

/// @brief 推送步骤态四格 → 词条 key（`keys.push.stage.*`）：**恒有值**，回空串的情形不存在。
///
/// 阶梯的四格 label 是常驻文案（不是留痕），面板按 `PushReport::stage` 决定哪格是当前格。
/// 「执行」只有一格：读回与追加是同一次远端命令，两段 exec 之间没有用户能分辨的状态差。
[[nodiscard]] auto key_push_stage_key(conn::PushStage stage) -> std::string_view;

/// @brief 推送终值的留痕 → 词条 key：失败档（`keys.push.fail.*`）/「已在授权表里」/「已追加」三族之一。
///
/// 三族的分工是判据 §6 那句「本地判重命中即不发写并告知」：`already_authorized` **不是**失败，
/// 也不该显示成「刚写了什么」，所以它在前面判、失败其次、剩下才是真追加成功。本函数是这三者
/// 的唯一裁决处（面板不再自己排这个优先级）。
[[nodiscard]] auto key_push_notice_key(const conn::PushReport &report) -> std::string_view;

}  // namespace borealis::ui
