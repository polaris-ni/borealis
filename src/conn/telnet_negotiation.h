#pragma once

// ============================================================
// Telnet IAC 协商的纯逻辑状态机（src/conn/telnet_negotiation.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.06（Telnet）的纯逻辑层（裁决 7.112 D11①）：命令段与数据段的
// 分界、`FF FF` 的双向转义、`SB … SE` 载荷的吞吃、ECHO/SGA/TTYPE 的应答表、
// 上线发起序、NAWS 的组包与同值去重——只依赖标准类型，不碰 socket、不碰
// `au::`、不碰 UI（AGENTS.md §4.4 第 20 条，`utest_telnet_negotiation` 无头单测）。
//
// 三条贯穿全件的口径：
// ⑴ **按字节走，绝不分块**（稿 §0 的 F5/F6 实测）：一条 `IAC WILL ECHO` 可以
//    三次读各 1 字节到达，也可以和三条命令 + `login: ` 混在同一次读里。故未
//    完成的命令段**绝不进**净数据——半个命令的 0xFF/0xFB 一旦被当数据解码就是
//    乱码格（判据 5）。等价性由此成为可断言的事实：同一段字节流按 1 字节／随机
//    分片／整段喂入，净数据与待回字节必须逐字节全等。
// ⑵ **表外一律拒绝，且同一立场只答一次**（D4①）：对端重复问不得再答，否则对端
//    带重试逻辑时形成应答风暴。发起序（D3①）呼用即记账，服务端回 `WILL ECHO`
//    不会再换来第二条 `DO ECHO`；「已答」位与解析状态、已发尺寸一并随重拨清零。
// ⑶ **状态位只记不改行为**（D7①）：`stance()` 给的是结论快照，本期没有 UI 消费方，
//    故连接腿之外无人读它——不因此预建通道，也不做本地回显。
//
// 写出侧（`escape_outbound`）与读入侧（净数据里的 `FF FF`）是同一份规程的两半
// （F7：数据段与 SB 载荷两处都可能出现转义）。socket 与线程归
// `src/conn/telnet_connection`（批 2）。
// ============================================================

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace borealis::conn {

/// @brief IAC 与其命令字节（线上常量，与 `uint8_t` 字节流同型）。
inline constexpr std::uint8_t kTelnetIac = 255;
inline constexpr std::uint8_t kTelnetSe = 240;
inline constexpr std::uint8_t kTelnetSb = 250;
inline constexpr std::uint8_t kTelnetWill = 251;
inline constexpr std::uint8_t kTelnetWont = 252;
inline constexpr std::uint8_t kTelnetDo = 253;
inline constexpr std::uint8_t kTelnetDont = 254;

/// @brief 本仓认识的四个选项码。
inline constexpr std::uint8_t kTelnetOptEcho = 1;
inline constexpr std::uint8_t kTelnetOptSga = 3;
inline constexpr std::uint8_t kTelnetOptTtype = 24;
inline constexpr std::uint8_t kTelnetOptNaws = 31;

/// @brief `SB TTYPE` 载荷的首字节（`IS`，RFC 1091 的线上取值 0）。
///
/// 少了这一字节，主流 telnetd 会把选项码本身当终端类型名的第一个字符读。
inline constexpr std::uint8_t kTelnetTtypeIs = 0;

/// @brief 终端类型名（稿 §7 在册：本期不可配，就收在这一枚常量里）。
inline constexpr std::string_view kTelnetTerminalType{"XTERM"};

/// @brief NAWS 单边的可发上界（RFC 1073：每维两个字节，故 65535）。
inline constexpr int kTelnetMaxWindowSize = 65535;

/// @brief 协商结论的三态档（缺省未定，定了对本件的行为也没有影响，见口径 ⑶）。
enum class TelnetAnswer : std::uint8_t {
    Pending,  ///< 未定：这一对方向上还没出现过命令。
    Yes,      ///< 已定为「会／要」。
    No,       ///< 已定为「不会／不要」。
};

/// @brief 选项状态位快照（判据 4 的「结论记在状态位里」）。
struct TelnetOptionStance {
    TelnetAnswer remote_echo{TelnetAnswer::Pending};                        ///< 对端是否替本端回显。
    TelnetAnswer remote_suppress_ga{TelnetAnswer::Pending};                 ///< 对端是否抑制 Go Ahead。
    TelnetAnswer reports_terminal_type{TelnetAnswer::Pending};              ///< 对端是否要求本端报 TTYPE。
    TelnetAnswer reports_window_size{TelnetAnswer::Pending};                ///< 对端是否要求本端报尺寸。

    [[nodiscard]] auto operator==(const TelnetOptionStance &) const noexcept -> bool = default;
};

/// @brief 一次喂入的结果：进网格的净数据 + 该写回 socket 的字节。
struct TelnetParsed {
    std::vector<std::uint8_t> data{};      ///< 净数据（`FF FF` 已还原成单个 0xFF；命令段与 SB 载荷绝不在这里）。
    std::vector<std::uint8_t> replies{};   ///< 待回字节，按应答表的次序排好，调用方原样写出即可。
};

/// @brief Telnet 协商状态机（可暂停、可续跑，实例由连接腿在读线程私有持有）。
///
/// 无锁、不可跨线程共享：解析状态、已答记账与已发尺寸三者都是**单线程**的
/// （AGENTS.md §4.5 第 25 条：跨线程只交 latest-value 合成后的最终值）。
class TelnetNegotiation {
public:
    /// @brief 上线发起序：`IAC DO ECHO` + `IAC DO SGA`（判据 3，D3①）。
    ///
    /// 呼用即把这两项立场记成「已表态」（口径 ⑵），故随后对端回 `WILL ECHO` 时
    /// 不再产生第二条 `DO ECHO`；因此重复呼本函数回空——建连成功后呼一次即可，
    /// 重拨走 `reset_for_redial()` 之后才再有字节。
    [[nodiscard]] auto opening_commands() -> std::vector<std::uint8_t>;

    /// @brief 喂入一段收到的字节，取出净数据与待回字节。
    [[nodiscard]] auto on_bytes(std::span<const std::uint8_t> bytes) -> TelnetParsed;

    /// @brief 写出侧转义：把数据里的每个 0xFF 写成 `FF FF`（F7，判据 5 的发送半）。
    ///
    /// 转义发生在连接腿而不是 `Session`：SSH／本地腿没有这层规程（稿 §5）。
    [[nodiscard]] static auto escape_outbound(std::span<const std::uint8_t> bytes)
        -> std::vector<std::uint8_t>;

    /// @brief 组一条 `SB NAWS`（判据 8，D10② 人改判新增）；该不发时回空。
    ///
    /// 载荷次序为 `WIDTH WIDTH HEIGHT HEIGHT`（各大端两字节，RFC 1073 §2 与 §6
    /// 的例子；本机 GNU inetutils 2.7 客户端在 20×60 的 pty 上实测发出
    /// `FF FA 1F 00 3C 00 14 FF F0`，见稿 §0 的 F14）。回空的三种情形：与上次
    /// 已发同值（同值去重）、任一维不在 [1, 65535]（尺寸未定或超 RFC 上限）。
    /// **回空即不记账**，故下一次合法值照样能发出去。
    [[nodiscard]] auto window_size_command(int rows, int cols) -> std::vector<std::uint8_t>;

    /// @brief 当前状态位快照（口径 ⑶：本期无 UI 消费方，供日志与测试断言）。
    [[nodiscard]] auto stance() const -> TelnetOptionStance;

    /// @brief 重拨前清零：解析状态、「已答」记账、状态位、已发尺寸四样一起复位（D4①/D10② 代价 ⓒ）。
    auto reset_for_redial() -> void;

private:
    /// @brief 解析状态（按字节推进，任何一档都可停在半条命令上）。
    enum class ParserState : std::uint8_t {
        Data,           ///< 数据段。
        AfterIac,       ///< 刚收 IAC，等命令字节。
        AfterCommand,   ///< 刚收 WILL/WONT/DO/DONT，等选项字节。
        AfterSbOption,  ///< 刚收 SB，等选项字节。
        InSb,           ///< 子协商载荷中（本件不解析内容，只找终止符）。
        InSbAfterIac,   ///< 载荷中刚收 IAC，等 SE 或转义的第二个 IAC。
    };

    /// @brief 记下「我方已就 (命令, 选项) 表过态」；已记过则回 false。
    [[nodiscard]] auto take_stance(std::uint8_t command, std::uint8_t option) -> bool;

    /// @brief 按字节推进状态机一档（`on_bytes` 对段里每个字节呼一次）。
    auto step(std::uint8_t byte, TelnetParsed &out) -> void;

    /// @brief 应答表（判据 4）：`pending_command_` + 刚收到的选项字节 → 状态位与待回字节。
    auto answer(std::uint8_t option, TelnetParsed &out) -> void;

    ParserState state_{ParserState::Data};
    std::uint8_t pending_command_{0};        ///< AfterCommand 档上暂存的 WILL/WONT/DO/DONT。
    std::vector<int> answered_{};            ///< 已表态的 (命令 << 8 | 选项) 键表，长度就是个位数。
    TelnetOptionStance stance_{};
    struct LastWindow {
        int rows{0};
        int cols{0};
    };
    bool has_last_window_{false};
    LastWindow last_window_{};               ///< 上一次真的发出去的尺寸（同值去重的比对源）。
};

}  // namespace borealis::conn
