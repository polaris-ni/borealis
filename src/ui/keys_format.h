#pragma once

// ============================================================
// SSH 密钥面板纯格式化函数（src/ui/keys_format.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.10` 面板层的「最后一步」：行模型的徽标与数据段、状态→词条 key 的映射
// （批 1 的四族：加密两态 / 公钥缺失 / 名称与注释校验留痕 / 目录不可达留痕；批 2 到货的
// 生成、导出、删除三档结果与推送的步骤态＋失败归因；批 3 到货的 `KeysSnapshot`/`key_rows()`
// 行模型与在途任务那一族）。全部只依赖标准类型与 `conn/key_model.h`
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
#include <vector>

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

// ============================================================
// 批 3 到货：运行态快照与行模型（裁决 7.106 D4① 的 latest-value 那一面）
// ============================================================

/// @brief 装配层在跑哪一类任务（稿 §4 那句「生成三态」的落地形态：空闲／进行中／已回投）。
///
/// **这不是 `key_store` 的枚举**——那三家 outcome 说的是一次阻塞调用的**终局分档**，本枚举说的是
/// 调用**在途时**界面上该置灰哪枚钮、哪一行显示「生成中」。一次调用没有中间态可映射
/// （`key_store.h`/`key_push.h` 头注同口径），所以中间态只能由装配层的队列知道，由这里命名。
enum class KeysOpKind : std::uint8_t {
    Idle,      ///< 队列空：没有钮要置灰。
    Scan,      ///< 扫盘在途（开面板、每次动作之后的重扫）。
    Generate,  ///< 生成在途（判据 5 的「行内生成中」＋提交钮置灰，F3：RSA-4096 秒级）。
    Export,    ///< 导出公钥在途。
    Delete,    ///< 删除在途（D12⑷：删后要重扫，两件事共用一趟）。
    Push,      ///< 推送在途（阶梯在这一档才有当前格）。
};

/// @brief 装配层交回面板的**最新值**快照（7.97 D6① 同族：状态是可覆盖的量，不留事件队列）。
///
/// 面板每帧经 `snapshot` Hook 取一份副本，`key_rows()` 折成行表，行表与上一帧相等就不重建。
/// 本类型**不含任何 libssh 与框架类型**：`conn::KeyCandidate` 是纯逻辑层的标准类型行模型，
/// 推送那一格是 `conn::push_stage_index()` 算好的整数——面板因此不需要够得着 `key_push.h`
/// （那头注的理由：让面板摸到传输腿签名就等于给「动作一律经 Hooks 交回装配层」开门）。
struct KeysSnapshot {
    std::vector<conn::KeyCandidate> rows;  ///< 最近一次扫盘的行表（装配层已按 `sort_rows()` 排过）。
    std::vector<conn::KeyDirEntry> dirs;   ///< 同一次扫盘用的**规范化**目录表（含可达性判定结果）。
    KeysOpKind op{KeysOpKind::Idle};       ///< 在途任务；`Idle`＝队列空，没有留痕之外的话要说。
    std::string op_path;                   ///< 该任务作用的那一行路径（生成/扫盘为空＝无行归属）。
    int push_step{-1};                     ///< 推送阶梯当前格（0..3）；-1＝本帧不在推送、无当前格。
    std::string notice_key;                ///< 留痕词条 key（空＝无话可说；成功档多半不留痕）。
    std::string notice_arg;                ///< 留痕的 `{0}` 参数（要带上屏的文件名或路径）。

    [[nodiscard]] auto operator==(const KeysSnapshot &) const noexcept -> bool = default;
};

/// @brief 面板画出的一行＝扫盘事实 + 这一行摊上的在途任务。
struct KeyRow {
    conn::KeyCandidate key;
    KeysOpKind op{KeysOpKind::Idle};

    /// @brief 这一行能不能交出公钥单行（判据 4 的「复制」、判据 6 的「推送」共用的闸）。
    ///
    /// 判据就是 `public_line()` 回不回空串——两族之外的算法族与「带口令又无 `.pub`」那两档
    /// 都取不出 base64（`key_store.h` 的第 ⑶ 档），拼不出行就**没有可交出的动作**，面板据此
    /// 整枚不画而不是画一枚点了没反应的钮（侧栏 7.38⑥ F-b 同一条口径）。
    [[nodiscard]] auto public_text() const -> std::string;

    /// @brief 这一行的私钥是否带口令（导出腿要不要先问口令，D12⑵ 的反面）。
    [[nodiscard]] auto needs_passphrase_to_export() const noexcept -> bool {
        return key.encrypted;
    }

    [[nodiscard]] auto operator==(const KeyRow &) const noexcept -> bool = default;
};

/// @brief 行模型：扫盘行表 × 快照（`tunnel_rows` 同族，判据比这份而不读浮层树）。
///
/// 快照里那个 `op_path` 之外没有别的按路径的查找：任务串行（7.95 D7① 同族——同一时刻只跑
/// 一把钥匙的事），因此**至多一行**带着在途标记。行序照 `scanned` 原样（装配层已排过）。
[[nodiscard]] auto key_rows(const std::vector<conn::KeyCandidate> &scanned,
                            const KeysSnapshot &snapshot) -> std::vector<KeyRow>;

/// @brief 在途任务 → 词条 key（`keys.op.*`）；`Idle` 回空串＝空闲不占一行措辞。
///
/// 这一族是「进行中」那一态的唯一措辞处：面板拿到 key 非空就上屏，不自己写「生成中」。
[[nodiscard]] auto key_op_kind_key(KeysOpKind op) -> std::string_view;

/// @brief 卡片顶部那一行该说哪一句：无行归属的在途任务优先，其次是上一个动作留下的留痕。
///
/// 「优先」是本件裁决的（与 `key_push_notice_key()` 的三选一同一类活儿）：面板如果自己排这个
/// 先后，生成在途时就会既显「生成中」又把上一趟的失败红字留在同一行里，两句互相说谎。
/// 有行归属的任务（导出／删除／推送点在某一行上）不在这里说话——那一行自己带着措辞。
[[nodiscard]] auto key_headline_key(const KeysSnapshot &snapshot) -> std::string_view;

/// @brief 推送阶梯的四格词条 key（`keys.push.stage.*` 的次序展开）。
///
/// 次序由 `conn::push_stage_index()` 定（枚举的主人说哪一格在第几格），措辞由本件定，
/// 面板只按快照那个整数决定哪格是当前格——三处各留一份判定，谁也不多知道别人的活儿。
[[nodiscard]] auto key_push_ladder() -> std::vector<std::string_view>;

/// @brief 口令双栏不一致 → `keys.generate.issue_mismatch`（一致时回空串＝不留痕）。
///
/// 为什么在映射件里：这是全表**唯一**一条不对应任何 `conn` 枚举的判据，它纯属于「对话框
/// 上的两格输入」这件事。措辞仍然只在这里有一份，面板不写第二句（裁决 7.25⑬）。
[[nodiscard]] auto key_passphrase_mismatch_key(std::string_view first, std::string_view second)
    -> std::string_view;

}  // namespace borealis::ui
