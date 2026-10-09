// ============================================================
// SFTP 浏览器纯格式化函数实现（src/ui/sftp_format.cpp）
// ------------------------------------------------------------
// 三个渲染点各有口径锚：大小＝纯整数 1024 进（sftp_format.h 头注）；时间＝
// gmtime/localtime 的可重入分叉；八进制＝掩 07777 后 %04o。错误码→key 一表
// 穷举（新增枚举值漏配时编译期的 switch 警告先红，不靠测试兜底）。
// ============================================================

#include "sftp_format.h"

#include <cstdio>
#include <ctime>

namespace borealis::ui {

auto human_size(std::uint64_t bytes) -> std::string {
    static constexpr const char *kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    if (bytes < 1024ULL) {
        return std::to_string(bytes) + " " + kUnits[0];
    }
    // 落在最高一个「商 < 1024」的档；TB 封顶（更大的值不再换档，整段照样可读）。
    unsigned tier = 1;
    while (tier < 4 && (bytes >> (10U * (tier + 1U))) != 0) {
        ++tier;
    }
    const std::uint64_t unit = 1ULL << (10U * tier);
    const std::uint64_t whole = bytes / unit;
    // 先取余再乘 10：bytes 接近 uint64 上限时「乘后取模」会溢出，这个次序不会。
    const auto frac = static_cast<unsigned>((bytes % unit) * 10ULL / unit);
    if (frac == 0) {
        return std::to_string(whole) + " " + kUnits[tier];
    }
    return std::to_string(whole) + "." + std::to_string(frac) + " " + kUnits[tier];
}

namespace {

/// @brief 两个取值腿共用的渲染核心：tm → "YYYY-MM-DD HH:MM"（无时区知识）。
///
/// 缓冲取 64：每个 %d 的实参都来自 tm 字段，GCC 的 format-truncation 检查按
/// int 最坏 11 位 × 5 段算——小缓冲它始终不放行，与其造假上界不如给足。
[[nodiscard]] auto render_mtime(const std::tm &value) -> std::string {
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", value.tm_year + 1900,
                  value.tm_mon + 1, value.tm_mday, value.tm_hour, value.tm_min);
    return std::string{buf};
}

}  // namespace

auto format_mtime_utc(std::int64_t epoch) -> std::string {
    const std::time_t time = static_cast<std::time_t>(epoch);
    std::tm value{};
#if defined(_WIN32)
    // WHY 不用 std::gmtime：它返回进程级共享静态缓冲，SFTP worker 与 UI 线程可能同时
    // 格式化本地栏/远端栏的 mtime，非线程安全；gmtime_s 是 <ctime> 的既定可重入形态。
    // 这是显示层格式化而非平台子系统（AGENTS §4.5 第 23 条的平台边界针对 PTY/串口/
    // 传输等共享路径），故不走 src/platform/ 分流。
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    return render_mtime(value);
}

auto format_mtime_local(std::int64_t epoch) -> std::string {
    const std::time_t time = static_cast<std::time_t>(epoch);
    std::tm value{};
#if defined(_WIN32)
    // WHY 同 format_mtime_utc：localtime 的静态缓冲同理，localtime_s 换可重入形态。
    localtime_s(&value, &time);
#else
    localtime_r(&time, &value);
#endif
    return render_mtime(value);
}

auto format_octal(std::uint32_t mode) -> std::string {
    char buf[8]{};
    std::snprintf(buf, sizeof(buf), "%04o", static_cast<unsigned>(mode & 07777U));
    return std::string{buf};
}

auto sftp_error_key(conn::SftpError error) -> std::string_view {
    switch (error) {
    case conn::SftpError::None:
        return {};
    case conn::SftpError::NotConnected:
        return "sftp.error.not_connected";
    case conn::SftpError::Network:
        return "sftp.error.network";
    case conn::SftpError::PermissionDenied:
        return "sftp.error.permission_denied";
    case conn::SftpError::NotFound:
        return "sftp.error.not_found";
    case conn::SftpError::Cancelled:
        return "sftp.error.cancelled";
    case conn::SftpError::LocalIo:
        return "sftp.error.local_io";
    case conn::SftpError::Protocol:
        return "sftp.error.protocol";
    }
    return {};
}

}  // namespace borealis::ui
