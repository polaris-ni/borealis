#pragma once

// ============================================================
// SSH 密钥管理的纯逻辑模型（src/conn/key_model.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 的纯逻辑层（裁决 7.106 D2/D3/D12，稿 §4 的「key_model」件）：
// 行模型 `KeyCandidate`、目录表规范化、名称与注释校验、同名碰撞判定、
// `authorized_keys` 判重与追加计划、公钥单行拼装——全部只依赖标准类型，
// **不碰 libssh、不 stat、不碰 UI**（AGENTS.md §4.4 第 20 条，`utest_keys_model` 无头单测）。
//
// 与传输腿的分界（稿 §5）：本件回答「这一行算不算合法名、这两个目录是不是同一个、
// 这把公钥是否已在授权表里、追加该发哪些字节」；**文件系统里的真相**（谁在场、
// 有没有口令、指纹与 base64）由 `conn/key_store` 读进来再交回这里判定。可达性因此
// 是**注入的判定函数**而不是本件自己 stat——同步 IO 不进纯逻辑层（同第 25 条那条纪律，
// 装载接缝也不做）。
//
// 三条贯穿全件的口径：
// ⑴ **行唯一键＝私钥全路径**（D2③ 细则⑷）：多目录后 basename 必然撞，行表与快照 diff
//    都按路径；故本件里凡是「按名字找」的判定都带目录参数。
// ⑵ **路径串逐字节比较，不做 `~` 展开、不 realpath、不折大小写**：展开是 shell 的语义，
//    本仓的目录表由用户录入并经系统选择器给出绝对路径，把展开逻辑塞进来只会让
//    「表里写的」与「扫到的」对不上。同一目录经不同写法被判成两条是**如实登记的边界**
//    （面板把两条都显示，用户看得见），不是缺陷。
// ⑶ **本件不含显示文案**：`KeyCandidate` 里的类型串是**协议线名**（`ssh-ed25519`），
//    换英文即让判据消失，属裁决 14 条那条例外之外的常规 ASCII；而「有口令」「公钥缺失」
//    这类词条归 `ui/keys_format`（裁决 7.25⑬）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::conn {

/// @brief 密钥算法族（需求原文只列这两族：ed25519 优先、RSA 备选）。
enum class KeyType : std::uint8_t {
    Unknown,  ///< 认不出来（扫盘时不该进行表，留着是给装配层一个「不合预期」的落点）。
    Ed25519,  ///< `ssh-ed25519`。
    Rsa,      ///< `ssh-rsa`，位数另由 `KeyCandidate::bits` 给出。
};

/// @brief 协议线名（`.pub` 与 `authorized_keys` 第一个字段用的字面量）。
///
/// 这不是可译词条而是线上格式：OpenSSH 认这两个串，写错了公钥行直接不可用。
/// `Unknown` 回空串＝不可拼装，调用方据此拒绝而不是拼出一行废数据。
[[nodiscard]] auto key_wire_name(KeyType type) -> std::string_view;

/// @brief RSA 位数档位（稿 §2 判据 5：只有 RSA 卡出位数下拉）。
enum class RsaBits : std::uint16_t {
    Bits2048 = 2048,
    Bits3072 = 3072,
    Bits4096 = 4096,
};

/// @brief 位数是否本仓允许的档（面板下拉之外的手填路径也走这一道）。
[[nodiscard]] auto rsa_bits_is_supported(std::uint16_t bits) -> bool;

/// @brief 从公钥的 base64 段解出 RSA 位数——类型徽标「rsa 3072」里那个数的唯一来源。
///
/// **为什么本仓自己解**：libssh 0.12 的公共头没有任何位数访问器（`ssh_key_type()` 只到
/// `SSH_KEYTYPE_RSA` 为止，带位数的 `ssh_rsa_struct` 在库的私有头里），而稿 §2 判据 2 要
/// 徽标带位数（三档在同一列表里必须分得开）。于是这里按 SSH 线格式读公钥 blob 的
/// `mpint n`：OpenSSH 在最高位为 1 时前置一个 `0x00` 作符号位填充，那一字节不计入位数。
///
/// 回 0 的四种情形（调用方据此只显「rsa」不带数，而不是判成错误）：base64 字符非法、
/// blob 截断或长度字段越界、线名不是 `ssh-rsa`（ed25519 无位数概念）、模数长度不合法。
/// 本函数**只读不定信**：不判密钥强弱、不做任何 IO，故留在纯逻辑层可无头单测（第 20 条）。
[[nodiscard]] auto rsa_bits_from_public_base64(std::string_view base64) -> std::uint16_t;

/// @brief 一行＝一把私钥在列表里的全部事实（D2①：行源＝配对私钥 ∪ 无 `.pub` 的私钥）。
///
/// **只有私钥成行**：孤零零的 `.pub` 不是「一把钥匙」，列进去反而给不出任何动作。
/// 字段由 `key_store` 的扫盘腿填；本件只消费与排序，不伪造任何一项。
struct KeyCandidate {
    std::string path;             ///< 私钥全路径＝行唯一键（细则⑷）。
    std::string basename;         ///< 文件名，行首显示用。
    KeyType type{KeyType::Unknown};
    std::uint16_t bits{};         ///< RSA 位数；ed25519 恒 0（该族无位数概念）。
    bool has_public{};            ///< 同 basename 的 `.pub` 是否在场（假＝「公钥缺失」态）。
    std::string public_path;      ///< 配对 `.pub` 全路径；`has_public` 为假时为空。
    bool encrypted{};             ///< 有口令（D3①：一次探测的结论，不是解密）。
    std::string fingerprint;      ///< `SHA256:…`；探测失败时为空。
    std::string comment;          ///< `.pub` 第三字段；无 `.pub` 时为空。
};

/// @brief 行序：先按目录表次序（`~/.ssh` 恒在表首），同目录内按 basename 逐字节序。
///
/// 目录表里没有的行排在**最后**（按路径序）——它们来自上一次快照或并发落盘，
/// 丢行比乱序更糟。就地排序，返回引用便于链式使用。
[[nodiscard]] auto sort_rows(std::vector<KeyCandidate> &rows, const std::vector<std::string> &dirs)
    -> std::vector<KeyCandidate> &;

/// @brief 一个扫描目录在表里的形态。
struct KeyDirEntry {
    std::string path;         ///< 绝对路径串，逐字节即表内标识。
    bool is_home_dir{};       ///< 是否 `~/.ssh` 那一条（恒在不可移的凭据）。
    bool reachable{};         ///< 由注入的判定给出；未注入时恒为真。
};

/// @brief 目录表规范化（D2③ 细则⑶）：去重、`~/.ssh` 恒在、丢空串、保留用户录入次序。
/// @param raw     `Settings.key_dirs` 原样内容（**装载侧不做规范化**，见 `config/settings.h`）。
/// @param user_dir 平台用户目录（`platform::ssh_user_dir()`，D10①）；空串时不追加 `~/.ssh`
///                 ——拿不到 HOME 的极端现场不该凭空造一条扫面。
/// @param probe   可达性判定（调用方 stat；纯逻辑层不做 IO）。留空则全部算可达。
/// @return 规范化后的表：`~/.ssh` 恒在首位，其余按录入次序，重复项只留第一次出现的那条。
[[nodiscard]] auto normalize_key_dirs(std::span<const std::string> raw, std::string_view user_dir,
                                       const std::function<bool(std::string_view)> &probe = {})
    -> std::vector<KeyDirEntry>;

/// @brief 试图从表里移除某目录后的结果（判据 §6「`~/.ssh` 不可移有独立判据」）。
/// @return 移除后的表；请求移除的是 `~/.ssh` 时**原表照回**，调用方据此留痕。
[[nodiscard]] auto without_dir(const std::vector<KeyDirEntry> &dirs, std::string_view path)
    -> std::vector<KeyDirEntry>;

/// @brief 名称校验结论（`None`＝合法）。
enum class NameIssue : std::uint8_t {
    None,
    Empty,            ///< 空名。
    TooLong,          ///< 超 `kMaxKeyNameLength`。
    StartsDot,        ///< 以 `.` 开头：生成的私钥会成隐藏文件，扫盘与 `ls` 都看不见它。
    HasSeparator,     ///< 含 `/` 或 `\`——名字里带分隔符就是越出所选目录。
    HasControlChar,   ///< 含 ASCII 控制字符（含换行与制表）。
    Reserved,         ///< `.`、`..`，或与既有 `<name>.pub` 冲突的保留写法由碰撞判定另说。
};

/// @brief 注释校验结论。
enum class CommentIssue : std::uint8_t {
    None,
    TooLong,          ///< 超 `kMaxCommentLength`。
    HasControlChar,   ///< 含控制字符：注释是公钥行的第三个字段，换行会把一行劈成两行。
};

/// @brief 私钥文件名的长度上限。稿 §3 只写「长度上限」没给数，此处取定并回写稿：
///        64 足够容纳 `id_ed25519_公司跳板` 一类人读名，又让行首名称加类型徽标不挤掉次行路径。
inline constexpr std::size_t kMaxKeyNameLength = 64U;

/// @brief 注释长度上限。同上，取定并回写稿：72 是 `.pub` 第三字段的人读上界，
///        再长复制进 `authorized_keys` 也只是给对端添乱（判据 §6 的留痕宽度同档）。
inline constexpr std::size_t kMaxCommentLength = 72U;

/// @brief 校验私钥文件名（不含 `.pub` 后缀；后缀由拼装侧加）。
///
/// 只判**字符与长度**，不判文件系统里是否已有同名——那是 `would_collide_with_rows` 与
/// 传输腿的存在性检查（D5①「同名即拒」的两半，分别在纯逻辑与 IO 两侧）。
[[nodiscard]] auto validate_key_name(std::string_view name) -> NameIssue;

/// @brief 校验注释（可空；空注释＝用 F7 的 `user@host` 或留空，两档都不算错）。
[[nodiscard]] auto validate_comment(std::string_view comment) -> CommentIssue;

/// @brief 目标目录里是否已有同 basename 的**私钥行**（生成对话框「同名即拒」的纯判定半边）。
/// @param dir 目录全路径，与 `KeyCandidate::path` 的父段逐字节比较（带不带尾分隔符都算同一目录）。
///
/// **只看得见行，看不见盘**：行源是私钥（D2①，孤 `.pub` 不成行），所以「磁盘上还有一份无主的
/// `<name>.pub` 也占名」这一半不在本函数能力内，归传输腿的存在性检查（`key_store`，D5① 第三闸）。
/// 空名回真＝压根不能生成，而不是「不碰撞」。
[[nodiscard]] auto would_collide_with_rows(const std::vector<KeyCandidate> &rows, std::string_view dir,
                                           std::string_view basename) -> bool;

/// @brief 多目录后 basename 撞车的那些名字（面板据此在次行强调全路径；判据 2）。
[[nodiscard]] auto duplicate_basenames(const std::vector<KeyCandidate> &rows) -> std::vector<std::string>;

/// @brief 拼一行公钥：`<线名> <base64> <comment>`，**单行、无尾随换行**（判据 §6 复制那条）。
///
/// 注释为空时只交两字段，不留尾随空格。线名或 base64 为空回空串＝不产出废行。
/// 拼装侧对线名/base64 里的空白做二次防御（含空白即废行，回空串）——注释里的空格是合法的，
/// OpenSSH 本就允许 `… comment with spaces`，故只裁首尾、不折叠内部。
[[nodiscard]] auto public_line(std::string_view wire_name, std::string_view base64, std::string_view comment)
    -> std::string;

/// @brief 公钥是否已在远端 `authorized_keys` 内容里（D7① 的本地判重）。
///
/// 比对**线名与 base64 两个字段**，忽略注释与首尾空白——同一把钥匙换个注释写就是两行
/// 不同的文本，但授权的是同一个主体；远端带选项前缀（`from="…"`）的行也算命中。
[[nodiscard]] auto is_authorized(std::string_view remote_content, std::string_view wire_name,
                                 std::string_view base64) -> bool;

/// @brief 追加计划：该不该发写、发哪些字节。
struct AppendPlan {
    bool already_authorized{};  ///< 真＝不发写，只告知「已在授权表里」。
    std::string line;           ///< 规范化后的公钥单行（留痕与「已授权」时展示用）。
    std::string payload;        ///< 经 stdin 交给 `cat >>` 的字节；`already_authorized` 时为空。
};

/// @brief 判该不该追加，并算出交付字节（D7①）。
///
/// payload 的两条硬规矩：⑴ 远端内容非空且**不以换行结尾**时前置一个 `\n`——否则新公钥会
/// 与末行粘在一起，两把钥匙同时报废，这是本函数存在的理由；⑵ payload **自带尾随换行**
/// （与剪贴板那条「无尾随换行」相反：追加写要让文件保持行终止，否则下一个客户端又粘一次）。
/// @param remote_content 远端 `authorized_keys` 现内容；读不到（不存在）时传空串＝新建。
/// @param line `public_line()` 的产物。
[[nodiscard]] auto plan_append(std::string_view remote_content, std::string_view line) -> AppendPlan;

}  // namespace borealis::conn
