#pragma once

// ============================================================
// 私有文件的建与写腿接口（src/platform/file_secure.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.10`（SSH 密钥管理器）落盘私钥时需要的「原子建 + 只我可读」能力，和
// `SPEC.FEAT.CONN.05` 三期监视转储需要的「整份就位、不落半成品」写盘，共用这一族闸。
// 两条读数（裁决 7.106 稿 §0 的 F5/F6）决定了本腿必须存在：
//
//   F5  libssh 的 `ssh_pki_export_privkey_file_format` 对**已存在路径静默改写**
//       （无 O_EXCL 语义）⇒「绝不覆盖既有私钥」这道闸库侧给不了。
//   F6  它写出的文件模式**受 umask 支配**（本机 umask 002 实测落成 0664），
//       而 OpenSSH 会拒用同组可写的私钥 ⇒ 权限也必须本仓收。
//
// 于是三道闸收在同一次 `O_CREAT|O_EXCL` 里：不存在才建得成（不覆盖）、建出来
// 即 0600（不受 umask 影响，建完再 `fchmod` 钉死）、传输腿随后**截断写入这个
// 已存在的 fd 对应路径**（`open` 对已存在文件忽略 mode 实参，模式因此保住），
// 最后由调用方 `rename` 就位（裁决 7.106 D5①）。`write_private_file` 就是把这
// 一套按同一顺序收成一次调用（稿 UI_SERIAL_MONITOR §10.2 D23①），给的是「用户
// 点名的路径要么还是老文件、要么是完整新文件」这条保证。
//
// 平台知识不外溢（AGENTS.md §4.5 第 23 条）：裸 open/fchmod 与 Win32 的 ACL
// 都只进 src/platform/{posix,win}/file_secure.cpp，路径串的码页口径只进
// src/platform/{posix,win}/native_path.cpp，共享侧只见下面这两个枚举加两个函数。
//
// 线程口径：**阻塞文件系统 IO**，持续或秒级量级的写调用方须在 worker 线程（第 25 条），
// 不得从绘制路径进来。一次性、用户显式发起、量级毫秒的导出（配置导出、监视转储）在点击
// 派发里跑，判据见稿 UI_SERIAL_MONITOR §10.5 与裁决 7.162——那条口径 `SettingsPanel::run_export()`
// 今天已是先例，本条把它写明而不是新立。
// ============================================================

#include <cstdint>
#include <string_view>

namespace borealis::platform {

/// @brief 建私有文件的结果分档。
enum class PrivateFileOutcome : std::uint8_t {
    Created,       ///< 本次新建成功：路径此前不存在，文件现为 0600 空文件。
    AlreadyExists, ///< 路径已在（私钥、公钥、符号链接或目录都算）：调用方据此**拒绝覆盖**（D5①）。
    Error,         ///< 建不了：父目录不存在、权限不足、名字非法（细节已记日志）。
};

/// @brief 以「只我可读」的权限原子新建一份文件；**已存在一律不碰**。
///
/// 语义边界三条：⑴ 只创建不写入——交回的是一份零长度文件，内容由调用方
/// （libssh 的导出函数）截断写入；⑵ 目标是符号链接时回 `AlreadyExists` 而
/// **不跟随**（`O_EXCL` 的自然结果），故本函数不会经链接改写链接指向的文件；
/// ⑶ 路径按 UTF-8 编码，父目录必须已存在——本函数不代建目录（密钥目录的
/// 创建与 `~/.ssh` 的 0700 归调用方，见 conn/key_store）。
/// @param path 目标文件路径（UTF-8）。
/// @return 见 `PrivateFileOutcome`。
[[nodiscard]] auto create_private_file(std::string_view path) -> PrivateFileOutcome;

/// @brief 一次私有写盘的结果分档。
enum class PrivateWriteOutcome : std::uint8_t {
    Written,       ///< 文本已整体落到点名路径（目标此前存在即被完整地换掉）。
    InvalidPath,   ///< 空串或内嵌 NUL：这种名字进系统调用只会落到另一份路径上，判非法不判失败。
    NoScratchName, ///< 同目录连档临时件名都取不到（前八档全被占）；没碰过任何既有文件。
    Failed,        ///< 建/写/就位任一步失败：细节已记日志，本次建的临时件已删。
};

/// @brief 把一段文本按「同目录 0600 临时件 → 截断写 → `rename` 就位」落到用户点名的路径。
///
/// 三条口径：⑴ 目标路径上**不会出现写了一半的文件**——要么老文件逐字还在，要么新文本整体就位；
/// ⑵ 目标此前存在即被换掉，这是「用户点了保存」的应有之义，本函数不替用户拒绝覆盖；
/// ⑶ 临时件名撞上已有文件时逐档改名再试（最多八档），既不覆写也不删别人的盘——那份东西可能
/// 是上一次崩在这里留下的半成品，也可能是用户自己的文件，本函数没有职权分辨。
/// 权限沿 `create_private_file`（POSIX 钉 0600；Windows 由目录继承，见两腿头注）。
/// 路径串按 UTF-8 处理：Windows 上先转宽串再进标准流（`native_path_of`），故点名一个中文名
/// 得到的就是那个中文名，不是按本地码页解出来的花码。
/// @param path 目标文件路径（UTF-8），父目录必须已存在。
/// @param text 要落的字节，原样写入、不做任何行分隔符翻译。
/// @return 见 `PrivateWriteOutcome`；除 `Written` 外都已在平台腿里记过诊断。
[[nodiscard]] auto write_private_file(std::string_view path, std::string_view text)
    -> PrivateWriteOutcome;

}  // namespace borealis::platform
