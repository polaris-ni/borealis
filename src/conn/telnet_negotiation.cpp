// ============================================================
// Telnet IAC 协商的纯逻辑状态机实现（src/conn/telnet_negotiation.cpp）
// ------------------------------------------------------------
// 接口与口径见 conn/telnet_negotiation.h。本 TU 只做标准类型的按字节查表与
// 拼串，没有任何 socket、没有线程、没有 UI（AGENTS.md §4.4 第 20 条）。
// ============================================================

#include "conn/telnet_negotiation.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace borealis::conn {
namespace {

/// @brief 「我方已就这对 (命令, 选项) 表过态」的记账键。
[[nodiscard]] auto stance_key(std::uint8_t command, std::uint8_t option) -> int {
    return (static_cast<int>(command) << 8) | static_cast<int>(option);
}

/// @brief 对端声明「它会做」时，我方会 affirm 的选项集合（其余走 D4① 的拒绝档）。
///
/// 只有 ECHO 与 SGA：这两项的结论收下来记进状态位就是全部用途（口径 ⑶）。
/// `TTYPE`/`NAWS` 是**我方**要报的东西，对端 `WILL` 它们本端无从消费。
[[nodiscard]] auto can_affirm(std::uint8_t option) -> bool {
    return option == kTelnetOptEcho || option == kTelnetOptSga;
}

/// @brief 我方会应「要我做」的选项集合（`DO x` 的接受档）。ECHO 刻意不在内——D7①。
[[nodiscard]] auto can_affirm_our_do(std::uint8_t option) -> bool {
    return option == kTelnetOptSga || option == kTelnetOptTtype || option == kTelnetOptNaws;
}

/// @brief 尺寸单边的可发区间判据（0 与负数＝尺寸未定，超 65535＝RFC 1073 上限外）。
[[nodiscard]] auto window_value_is_sendable(int value) -> bool {
    return (value >= 1) && (value <= kTelnetMaxWindowSize);
}

/// @brief 大端两字节追加（线上次序 `WIDTH[1] WIDTH[0]`，RFC 1073 §2）。
auto append_be16(std::vector<std::uint8_t> &out, int value) -> void {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

}  // namespace

auto TelnetNegotiation::take_stance(std::uint8_t command, std::uint8_t option) -> bool {
    const auto key = stance_key(command, option);
    if (std::ranges::find(answered_, key) != answered_.end()) {
        return false;
    }
    answered_.push_back(key);
    return true;
}

auto TelnetNegotiation::opening_commands() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    out.reserve(6);
    for (const std::uint8_t option : {kTelnetOptEcho, kTelnetOptSga}) {
        if (!take_stance(kTelnetDo, option)) {
            continue;
        }
        out.push_back(kTelnetIac);
        out.push_back(kTelnetDo);
        out.push_back(option);
    }
    return out;
}

auto TelnetNegotiation::escape_outbound(std::span<const std::uint8_t> bytes)
    -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    out.reserve(bytes.size());
    for (const auto byte : bytes) {
        if (byte == kTelnetIac) {
            out.push_back(kTelnetIac);
        }
        out.push_back(byte);
    }
    return out;
}

auto TelnetNegotiation::on_bytes(std::span<const std::uint8_t> bytes) -> TelnetParsed {
    TelnetParsed out;
    for (const auto byte : bytes) {
        step(byte, out);
    }
    return out;
}

auto TelnetNegotiation::step(std::uint8_t byte, TelnetParsed &out) -> void {
    switch (state_) {
    case ParserState::Data:
        if (byte == kTelnetIac) {
            state_ = ParserState::AfterIac;
            return;
        }
        out.data.push_back(byte);
        return;

    case ParserState::AfterIac:
        switch (byte) {
        case kTelnetIac:
            // `FF FF` 在数据段里就是一个字面 0xFF（F7）。
            out.data.push_back(kTelnetIac);
            state_ = ParserState::Data;
            return;
        case kTelnetWill:
        case kTelnetWont:
        case kTelnetDo:
        case kTelnetDont:
            pending_command_ = byte;
            state_ = ParserState::AfterCommand;
            return;
        case kTelnetSb:
            state_ = ParserState::AfterSbOption;
            return;
        default:
            // NOP/DM/BRK/IP/AO/AYT/EC/EL/GA 与表外命令都是**单字节**命令：吞掉它本身即
            // 结束，绝不让它把下一个字节也吃掉（判据 5 的另一半：`IAC GA` 后面跟着的
            // 照常是数据）。
            state_ = ParserState::Data;
            return;
        }

    case ParserState::AfterCommand:
        answer(byte, out);
        pending_command_ = 0;
        state_ = ParserState::Data;
        return;

    case ParserState::AfterSbOption:
        // 载荷内容本期一律不解析（D4①：`SB x … SE` 照吞到 `SE` 不处理）。
        state_ = ParserState::InSb;
        return;

    case ParserState::InSb:
        state_ = (byte == kTelnetIac) ? ParserState::InSbAfterIac : ParserState::InSb;
        return;

    case ParserState::InSbAfterIac:
        // `SE` 终止子协商；第二个 `IAC` 是载荷里转义过的字面 0xFF（F7 的那一半在
        // 载荷侧）。其余字节仍当载荷继续吞——在等到 `IAC SE` 之前不回到数据段，
        // 否则一个畸形载荷会把后面的裸字节当数据送进网格（判据 5）。
        state_ = (byte == kTelnetSe) ? ParserState::Data : ParserState::InSb;
        return;
    }
}

auto TelnetNegotiation::answer(std::uint8_t option, TelnetParsed &out) -> void {
    switch (pending_command_) {
    case kTelnetWill: {
        if (option == kTelnetOptEcho) {
            stance_.remote_echo = TelnetAnswer::Yes;
        } else if (option == kTelnetOptSga) {
            stance_.remote_suppress_ga = TelnetAnswer::Yes;
        }
        // 我方立场：支持则 `DO`，否则 `DONT`（判据 4）。已表过态即一个字节都不回。
        const auto affirm = can_affirm(option) ? kTelnetDo : kTelnetDont;
        if (take_stance(affirm, option)) {
            out.replies.insert(out.replies.end(), {kTelnetIac, affirm, option});
        }
        return;
    }

    case kTelnetWont:
        if (option == kTelnetOptEcho) {
            stance_.remote_echo = TelnetAnswer::No;
        } else if (option == kTelnetOptSga) {
            stance_.remote_suppress_ga = TelnetAnswer::No;
        }
        return;  // 对端说「它不会做」无需我方答话（口径 ⑵：没有新立场要表）

    case kTelnetDo: {
        if (option == kTelnetOptTtype) {
            stance_.reports_terminal_type = TelnetAnswer::Yes;
        } else if (option == kTelnetOptNaws) {
            stance_.reports_window_size = TelnetAnswer::Yes;
        }
        // ECHO 落进拒绝档 ⇒ `WONT ECHO`，即 D7① 的「不做本地回显」。
        const auto affirm = can_affirm_our_do(option) ? kTelnetWill : kTelnetWont;
        if (!take_stance(affirm, option)) {
            return;
        }
        out.replies.insert(out.replies.end(), {kTelnetIac, affirm, option});
        if (option != kTelnetOptTtype) {
            return;
        }
        // `WILL TTYPE` 紧跟 `SB TTYPE IS "<type>" SE`（判据 4）；名字里若出现 0xFF
        // 走同一份转义（F7）。
        std::vector<std::uint8_t> payload{kTelnetTtypeIs};
        payload.insert(payload.end(), kTelnetTerminalType.begin(), kTelnetTerminalType.end());
        const auto escaped = escape_outbound(payload);
        out.replies.insert(out.replies.end(), {kTelnetIac, kTelnetSb, kTelnetOptTtype});
        out.replies.insert(out.replies.end(), escaped.begin(), escaped.end());
        out.replies.insert(out.replies.end(), {kTelnetIac, kTelnetSe});
        return;
    }

    case kTelnetDont:
        if (option == kTelnetOptTtype) {
            stance_.reports_terminal_type = TelnetAnswer::No;
        } else if (option == kTelnetOptNaws) {
            // 状态位如实记 No，但**不**据此停发 `SB NAWS`：稿 §2 判据 8 定的是无条件
            // 下发（D10② 人改判），RFC 1073 §2 那句「服务器可发 DON'T NAWS 禁止后续
            // 子协商」属对端裁量，本仓尺寸信息是 advisory 的，多发不改变线上语义。
            // 真服务器的反应以稿 §6 走查第 ⑷ 条取数，不在本件预建停发分支。
            stance_.reports_window_size = TelnetAnswer::No;
        }
        return;

    default:
        return;  // `pending_command_` 只能是四档之一；走到这里说明状态被外部破坏，静默丢弃
    }
}

auto TelnetNegotiation::window_size_command(int rows, int cols) -> std::vector<std::uint8_t> {
    if (!window_value_is_sendable(rows) || !window_value_is_sendable(cols)) {
        return {};
    }
    if (has_last_window_ && (last_window_.rows == rows) && (last_window_.cols == cols)) {
        return {};  // 同值去重：拖动分屏不得形成协商风暴（D10② 代价 ⓑ）
    }
    last_window_ = LastWindow{rows, cols};
    has_last_window_ = true;

    std::vector<std::uint8_t> raw;
    append_be16(raw, cols);  // WIDTH 在前
    append_be16(raw, rows);  // HEIGHT 在后（RFC 1073 §2 的语法行与 §6 的例子）
    const auto payload = escape_outbound(raw);

    std::vector<std::uint8_t> out{kTelnetIac, kTelnetSb, kTelnetOptNaws};
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), {kTelnetIac, kTelnetSe});
    return out;
}

auto TelnetNegotiation::stance() const -> TelnetOptionStance {
    return stance_;
}

auto TelnetNegotiation::reset_for_redial() -> void {
    // 整件复位：半个命令的残留、已答记账、状态位、已发尺寸四样都不能跨连接带（D4①
    // 与 D10② 代价 ⓒ——重连后同一台服务器可能改口）。
    *this = TelnetNegotiation{};
}

}  // namespace borealis::conn
