// ============================================================
// 字节监视卡身份串的折算实现（src/ui/monitor_binding.cpp）
// ------------------------------------------------------------
// 四档折法与三条口径见公共头横幅。本件无状态、不查表、不碰会话，故全部判据都能脱离 GUI 单测
// （AGENTS.md §4.4 第 20 条）；装配层那三张表的取料不在这里，也没有证人——那是裁决 7.83 的射程。
// ============================================================

#include "borealis/ui/monitor_binding.h"

#include <string>

namespace borealis::ui {
namespace {

/// @brief IPv6 字面量加方括号：主机串里出现 `:` 即按字面量处理（与 `conn::proxy_jump.cpp` 的
///        `bracket_host` 同规则，见公共头末段）。
[[nodiscard]] auto bracketed(const std::string &host) -> std::string {
    if (host.find(':') == std::string::npos) {
        return host;
    }
    return "[" + host + "]";
}

}  // namespace

auto monitor_binding_target(const MonitorBindingSource &source) -> std::string {
    switch (source.kind) {
        case conn::ConnectionType::Serial: {
            if (!source.serial.has_value()) {
                return {};
            }
            return source.serial->device_path;
        }
        case conn::ConnectionType::Ssh: {
            if (!source.ssh.has_value() || source.ssh->host.empty()) {
                return {};
            }
            std::string out;
            // 空 user 不写前导 `@`：SSH 的「以当前用户登录」在档案里就是留空，把 `@host` 上屏是
            // 报了一个并不存在的登录名（与跳链规范化那条已裁形态同值，稿 D38②）。
            if (!source.ssh->user.empty()) {
                out += source.ssh->user;
                out += '@';
            }
            out += bracketed(source.ssh->host);
            // 缺省端口取档案结构自己那枚成员初值，不在这里写死 22（公共头第二段）。
            if (source.ssh->port != conn::SshProfile{}.port) {
                out += ':';
                out += std::to_string(source.ssh->port);
            }
            return out;
        }
        case conn::ConnectionType::Telnet: {
            if (!source.telnet.has_value() || source.telnet->host.empty()) {
                return {};
            }
            std::string out = bracketed(source.telnet->host);
            if (source.telnet->port != conn::TelnetProfile{}.port) {
                out += ':';
                out += std::to_string(source.telnet->port);
            }
            return out;
        }
        case conn::ConnectionType::Local: {
            if (!source.local.has_value()) {
                return {};
            }
            // 空命令行在这里不是「平台默认 shell」而是「取料没取到」——装配层交进来的是**有效值**
            // （`local_specs_by_pane` 的字段口径，稿 F14①），探测早于开卡就已完成。真取不到时整格
            // 不画，绝不伪造「本地终端」这种第二来源的名字（稿 D38②末档）。
            return source.local->command_line;
        }
    }
    return {};
}

}  // namespace borealis::ui
