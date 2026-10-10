#pragma once

// ============================================================
// SSH 密钥盘的传输腿（src/conn/key_store.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 批 2（裁决 7.106 的 D2/D3/D5/D12，稿 §4 的「key_store」件）：
// 扫盘成行表、生成密钥对、从私钥导出公钥、删除一对。四件事都在**文件系统**上，
// 一件都不碰网络。
//
// 本头是**私有头**（裁决 D1① 同款纪律）：libssh 类型只出现在实现文件里，
// 交回的字段全是标准类型（`KeyCandidate` 等归 conn/key_model）。
//
// 线程口径：**全部是阻塞 IO 与长计算**（稿 §0 F3：RSA-4096 生成本机 369 ms，
// `-O0` 读数），调用方必须在 worker 线程上跑，结果按 SPEC.NF.PERF.06 经有界
// 队列交回 UI（AGENTS.md §4.5 第 25 条）。本件自己不起线程。
//
// 与纯逻辑层的分界（稿 §5）：本件只回答「盘上有什么」——谁在场、有没有口令、
// 指纹与 base64 是什么；「这一行算不算合法名、这两个目录是不是同一个、该不该
// 追加、行怎么排」一律交回 `conn/key_model` 判。因此本件的入参已经是
// `normalize_key_dirs()` 与 `validate_key_name()` 过完闸的形态，而它自己**不再
// 造第二套裁决**（第 3 条）。
//
// 一次阻塞调用没有中间态：稿 §4 说的「生成三态」是**装配层快照**的属性
// （排队中／在跑／已完），不在本件里——本件只出终局分档。
//
// 随批 3 面板到货的一处补字段（裁决 7.109）：`KeyCandidate::public_base64` 由本腿填，
// 它是判据 4「一键复制」那一行的第二字段。理由是那枚「复制」发生在按钮的点击回调里，
// 现场再去读 `.pub` 就是回调中的同步 IO（AGENTS §4.5 第 25 条），而本腿算指纹时本来就
// 握着公钥——留下这串字节不多一次打开。取不出的那两档（带口令且无 `.pub`、两族之外的
// 算法族）留空串，面板据此把「复制／推送」整枚不画。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "conn/key_model.h"

namespace borealis::conn {

/// @brief 扫盘：目录表 → 行表（稿 §5 的 `scan(dirs)`）。
///
/// 纪律四条（D2③ 细则⑶）：**单层、不递归、不跟符号链接、不可达目录跳过**。
/// 符号链接不成行因此是**扫面侧**的口径（与删除腿的 D12⑶ 同一条理由：链接与
/// 目标的语义歧义），而不可达条目由 `KeyDirEntry::reachable` 带着，本件不去
/// 判定也不去删表——那是 `normalize_key_dirs()` 注入的判定与装配层的持久化。
/// 另外两条不成行的口径在这里，不在纯逻辑侧：`.pub` 不成行（D2① 的行源），
/// **点号开头的隐藏件不成行**——本仓的名称闸禁 `.` 起头（`validate_key_name` 的 StartsDot），
/// 扫进来也对应不到一行可生成的密钥，而生成中途崩溃残留的 `.<name>.borealis-tmp`
/// 正是这一档（它躺着一整把私钥，列出来反而像个能用的东西）。
///
/// 成行判据（D2①）：**只有私钥成行**，孤零零的 `.pub` 不是「一把钥匙」。
/// 一把钥匙的事实按下面的次序拼，每一步都可能缺，缺了留空而不伪造：
/// ⑴ 配对 `.pub` 在场 → 线名定类型、base64 定 RSA 位数（`rsa_bits_from_public_base64`，
///    libssh 公共头没有位数访问器）、第三字段是注释、指纹由该 blob 现算（F9）；
/// ⑵ `.pub` 不在场而私钥可无口令导入 → 类型/位数/指纹都从私钥的公共部分取；
/// ⑶ 私钥带口令（F8 的一次探测，实测 0 ms，**不解密**）且无 `.pub` → 类型与指纹
///    都拿不到，行仍出（它是用户的一把钥匙），徽标侧退化成空串（`keys_format` 已备此档）。
/// `.pub` 是符号链接时算「配对在场」但**不读它**：那一行不显「公钥缺失」，也就不会让
/// 「导出公钥」去覆写链接指向的别人的文件，字段退回 ⑵ 那条路。
///
/// **两族之外的算法族（ecdsa/dsa）也成行**，`type` 落 `Unknown`、指纹照常、徽标为空：
/// 需求原文的「ed25519 优先/RSA 备选」管的是**生成**那枚下拉，不是列表的行源（第 1 条
/// 不许把列表读成「只列这两族」）。这种行不能拼出公钥单行（`key_wire_name(Unknown)` 是
/// 空串 ⇒ `public_line()` 回空串），故「复制／推送」在装配侧按空行禁掉，本件不另造档位。
///
/// @param dirs `normalize_key_dirs()` 的产物（`~/.ssh` 恒在表首）。
/// @return 未排序的行表；行序由调用方交 `sort_rows()` 定（纯逻辑侧有证人）。
[[nodiscard]] auto scan_keys(const std::vector<KeyDirEntry> &dirs) -> std::vector<KeyCandidate>;

/// @brief 生成请求（装配层把面板的表单值原样搬来，本件不再读任何配置）。
struct GenerateRequest {
    KeyType type{KeyType::Ed25519};            ///< 需求原文两族：ed25519 优先、RSA 备选。
    std::uint16_t rsa_bits{0};                 ///< 仅 `Rsa` 用；须过 `rsa_bits_is_supported()`。
    std::string directory;                     ///< 目标目录（缺省 `~/.ssh`，由装配层给）。
    std::string basename;                      ///< 私钥文件名，不含 `.pub` 后缀。
    std::string comment;                       ///< 写进 `.pub` 第三字段；可空。
    std::optional<std::string> passphrase;     ///< 空/nullopt＝无口令。

    // 明文纪律（CONN.09）：passphrase 只活在本函数的调用栈里，随 F4 的实参进
    // libssh 后即销毁；本件不持副本、不落盘、不进日志。
    //
    // 「空串」与「没填」在这里是**同一件事**，而且必须是同一件事：F13 实测
    // `ssh_pki_export_privkey_file_format(key, "", …)` 回 -1 并只留下一份 0 字节文件——
    // 空串在库侧走的是「以空口令加密」那条分支，不是「不加密」。无口令要传 `nullptr`。
};

/// @brief 生成结果分档。
enum class GenerateOutcome : std::uint8_t {
    Created,            ///< 成功：私钥与同名 `.pub` 都在位（行表由调用方重扫）。
    InvalidRequest,     ///< 名称/注释/位数不合纯逻辑闸，或目录串为空——**未碰盘**。
    NameTaken,          ///< D5①：目标目录已有同名私钥**或**同名 `.pub`——一物未动。
    DirectoryUnusable,  ///< 目录建不起来、临时文件建不了（细节在日志）。
    Failed,             ///< libssh 侧失败（生成或落盘）；临时文件已回滚，不留半成品。
};

/// @brief 生成一对密钥（D4① + D5① 的三道闸收在一次 `O_EXCL` 里）。
///
/// 落盘形态：先在目标目录建隐藏的 0600 临时文件（`platform::create_private_file`），
/// 令 libssh **截断写入这份已存在的文件**（`open` 对已存在文件忽略 mode 实参，
/// 权限因此保住，而 F5 的静默改写拿不到目标路径），最后 `rename` 就位。
/// `.pub` 在私钥就位**之后**才写：万一写失败，得到的是一把「公钥缺失」的私钥，
/// 而那一态在列表里有出口（`export_public_key`），比反过来留一份无主公钥好。
///
/// 同名判定看两样：私钥本体（行源）与 `.pub`（**盘上存在性检查**——`key_model` 的
/// `would_collide_with_rows()` 只看得见行，孤 `.pub` 不成行，那一半本来就在本件，
/// 见裁决 7.107 ④）。
[[nodiscard]] auto generate_key(const GenerateRequest &request) -> GenerateOutcome;

/// @brief 导出公钥的结果分档。
enum class ExportOutcome : std::uint8_t {
    Written,          ///< `.pub` 已写出（内容＝`public_line()` 的单行三字段）。
    AlreadyExists,    ///< 目标 `.pub` 已在场：多半是列表过期，**不覆写**，让调用方重扫。
    NeedsPassphrase,  ///< 私钥带口令而手上没有口令：与删除腿相反的一档（D12⑵ 只管删除）。
    Failed,           ///< 读不到私钥、线格式不合 libssh、或写不出去（细节在日志）。
};

/// @brief 从私钥导出同名 `.pub`（稿 §2 判据 3 那枚「导出公钥」动作）。
///
/// @param private_path 私钥全路径（行唯一键）。
/// @param passphrase   私钥带口令时要交出口令；无口令的钥匙传 nullopt 即可。
///                     与 `generate_key` 同一条明文纪律：只活在调用栈里。
[[nodiscard]] auto export_public_key(std::string_view private_path,
                                     std::optional<std::string> passphrase) -> ExportOutcome;

/// @brief 删除结果分档（D12 的四条边界）。
enum class DeleteOutcome : std::uint8_t {
    Deleted,        ///< 私钥连同同名 `.pub`（若在场）一次清一对，两者都不在场了。
    RefusedSymlink, ///< 边界⑶：两条路径任一是符号链接——**链接与目标都按原样留着**。
    NotFound,       ///< 边界⑷：私钥路径不在场（并发删除或列表过期）。
    Failed,         ///< 在场但删不掉（权限/只读盘），或路径压根不是「一行密钥」那种东西。
                    ///< 动手之前两条路径都过完闸，故常规失败一物不动；唯一的例外是先摘 `.pub`
                    ///< 成功、再摘私钥失败——留下的行退化成「公钥缺失」态，那是列表里有出口的一档
                    ///< （「导出公钥」），比反过来留一份无主公钥好。
};

/// @brief 删除一把钥匙（D9② 改判追加的腿）。
///
/// 三条口径：⑴ **不读私钥内容**，故有口令的钥匙不需要口令就能删（D12⑵，写成反向
/// 判据以免实现顺手加询问）；⑵ 符号链接拒删（D12⑶）；⑶ 只删「同名一对」，
/// 目录表本身不因删文件而变动（`~/.ssh` 恒在不可移，其余目录去留归用户自己管）。
///
/// 「路径必须在扫描面内」这一条**不在本件**：本件不认识目录表，那是装配层的闸
/// （它手上才有 `normalize_key_dirs()` 的结果与当前行表）。
[[nodiscard]] auto delete_key_pair(std::string_view private_path) -> DeleteOutcome;

}  // namespace borealis::conn
