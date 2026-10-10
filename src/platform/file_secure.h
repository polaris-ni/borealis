#pragma once

// ============================================================
// 私钥文件创建腿接口（src/platform/file_secure.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10（SSH 密钥管理器）落盘私钥时需要的「原子建 + 只我可读」
// 能力。两条读数（裁决 7.106 稿 §0 的 F5/F6）决定了本腿必须存在：
//
//   F5  libssh 的 `ssh_pki_export_privkey_file_format` 对**已存在路径静默改写**
//       （无 O_EXCL 语义）⇒「绝不覆盖既有私钥」这道闸库侧给不了。
//   F6  它写出的文件模式**受 umask 支配**（本机 umask 002 实测落成 0664），
//       而 OpenSSH 会拒用同组可写的私钥 ⇒ 权限也必须本仓收。
//
// 于是三道闸收在同一次 `O_CREAT|O_EXCL` 里：不存在才建得成（不覆盖）、建出来
// 即 0600（不受 umask 影响，建完再 `fchmod` 钉死）、传输腿随后**截断写入这个
// 已存在的 fd 对应路径**（`open` 对已存在文件忽略 mode 实参，模式因此保住），
// 最后由调用方 `rename` 就位（裁决 7.106 D5①）。
//
// 平台知识不外溢（AGENTS.md §4.5 第 23 条）：裸 open/fchmod 与 Win32 的 ACL
// 都只进 src/platform/{posix,win}/file_secure.cpp，conn 层只消费下面这一个
// 枚举加一个函数。
//
// 线程口径：**阻塞文件系统 IO**，调用方须在 worker 线程（第 25 条），不得从
// UI 事件回调或绘制路径进来。
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

}  // namespace borealis::platform
