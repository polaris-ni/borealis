#pragma once

// ============================================================
// SFTP 浏览器纯格式化函数（src/ui/sftp_format.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.04 面板层的「最后一步」格式化：大小 / 修改时间 / 八进制权限 /
// 错误码→词条 key。全部只依赖标准类型与 conn/sftp_model 的枚举，不含任何
// aurora/au:: 类型、不碰文件系统与 UI（同 conn/sftp_model.h 的口径，可无头单测）。
//
// 渲染核心（gmtime/localtime 的取值与拼串）在本头声明、sftp_format.cpp 实现；
// 面板侧只消费算好的串（模型头既定口径「UI 只消费算好的行」的格式化版）。
// ============================================================

#include <cstdint>
#include <string>
#include <string_view>

#include "conn/sftp_model.h"  // conn::SftpError（枚举，非框架类型）

namespace borealis::ui {

/// @brief 字节数渲染成人类可读大小（1024 进 B/KB/MB/GB/TB，至多一位小数）。
///
/// 整值不带小数（1048576→"1 MB"），非整值一位小数、截断不四舍五入
/// （1536→"1.5 KB"）。纯整数运算：不引入浮点舍入口径，同输入恒同输出。
/// @param bytes 字节数。
[[nodiscard]] auto human_size(std::uint64_t bytes) -> std::string;

/// @brief Unix 秒渲染成 UTC "YYYY-MM-DD HH:MM"（确定性，供无头单测定值断言）。
/// @param epoch 秒级 Unix 时间。
[[nodiscard]] auto format_mtime_utc(std::int64_t epoch) -> std::string;

/// @brief Unix 秒渲染成本地时区 "YYYY-MM-DD HH:MM"（产品用；时区依运行环境）。
/// @param epoch 秒级 Unix 时间。
[[nodiscard]] auto format_mtime_local(std::int64_t epoch) -> std::string;

/// @brief st_mode 渲染成 4 位八进制权限串（mode & 07777，含特殊位，D5① tooltip 用）。
/// @param mode 全量 st_mode（类型位与特殊位先被掩掉）。
[[nodiscard]] auto format_octal(std::uint32_t mode) -> std::string;

/// @brief SFTP 错误分类 → 词条 key（中转文案经 settings_label(key) 取）。
///
/// 本件是唯一把 `conn::SftpError` 翻成词条 key 的地方（同 settings_i18n 里
/// `issue_key` 的分工形态）：面板不 switch 出第二套措辞。@c SftpError::None
/// 回空串——无错时调用方不该取文案。
/// @param error 传输腿报出的错误分类。
[[nodiscard]] auto sftp_error_key(conn::SftpError error) -> std::string_view;

}  // namespace borealis::ui
