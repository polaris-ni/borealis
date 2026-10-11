#pragma once

// ============================================================
// 串口参数纯逻辑模型（src/conn/serial_model.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.05（串口终端）的纯逻辑层：取值表与档案校验、档案→生效参数的
// 折算、发送行尾的字节拼接、设备枚举行的排序与可达性投影——只依赖标准类型与
// borealis/term/paste.h 的 LineEnding，不碰 termios、不碰 Win32 COMM、不碰 UI
// （AGENTS.md §4.4 第 20 条，utest_serial_model 无头单测）。
//
// 档位表在此处**只有一份**（裁决 7.110 D4① 的代价条款）：向导下拉的选项、
// 热改波特率的校验源（D6②）、平台腿的折算源三处共用，UI 与平台侧一律不现编。
// 平台位组合（`SerialFrameBits`）刻意不带任何 termios/DCB 数值——本件只回答
// 「这组参数合不合法、折成哪一种」，把它翻成 `CS8|PARENB` 或 `ByteSize=8`
// 是 src/platform/{posix,win}/serial.cpp 的事（AGENTS.md §4.5 第 23 条）。
// ============================================================

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/term/paste.h"

namespace borealis::conn {

/// @brief 标准波特率档位（F4 实测：pty 替身路径上十四档 setattr 后 getattr 逐档一致）。
///
/// 次序即下拉次序，升序。50 与 2000000 两端是 Linux 通用 UART 的实际可用区间；
/// 需求里「常见波特率全覆盖」以此表为准，非标速率见 `validate_serial_profile` 的注。
inline constexpr std::array<int, 14> kSerialBaudRates{
    {50, 300, 1200, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 1500000, 2000000}};

/// @brief 该速率是否在档位表内（下拉与热改入口的可选判据；不等于「档案非法」，见下）。
[[nodiscard]] auto is_supported_baud(int baud) -> bool;

/// @brief 校验位（`parity` 键的代码侧形态）。
enum class SerialParity : std::uint8_t {
    None,  ///< 无校验。
    Even,  ///< 偶校验。
    Odd,   ///< 奇校验。
};

/// @brief 流控（`flow` 键的代码侧形态，裁决 7.110 D3① 新增键）。
enum class SerialFlow : std::uint8_t {
    None,      ///< 不用流控（缺省：与设置域缺省同值）。
    Hardware,  ///< RTS/CTS 硬流控。
    Software,  ///< XON/XOFF 软流控。
};

/// @brief 取值表（JSON 与 UI 共用的唯一一份名表；`config/store.cpp` 直接引用它们）。
inline constexpr std::array<std::string_view, 3> kSerialParityNames{{"none", "even", "odd"}};
inline constexpr std::array<std::string_view, 3> kSerialFlowNames{{"none", "hardware", "software"}};
inline constexpr std::array<std::string_view, 3> kSerialLineEndingNames{{"LF", "CR", "CRLF"}};

/// @brief 名字 → 校验位；表外名回 nullopt。
[[nodiscard]] auto serial_parity(std::string_view name) -> std::optional<SerialParity>;
/// @brief 名字 → 流控；表外名回 nullopt。
[[nodiscard]] auto serial_flow(std::string_view name) -> std::optional<SerialFlow>;
/// @brief 名字 → 行尾（复用 `term::LineEnding`，裁决 7.33⑥ 的同一套取值）。
[[nodiscard]] auto serial_line_ending(std::string_view name) -> std::optional<term::LineEnding>;

/// @brief 校验位 → 名字（与 `serial_parity` 互逆；表外枚举值按 `None` 处理）。
[[nodiscard]] auto serial_parity_name(SerialParity parity) -> std::string_view;
/// @brief 流控 → 名字（与 `serial_flow` 互逆）。
[[nodiscard]] auto serial_flow_name(SerialFlow flow) -> std::string_view;
/// @brief 行尾 → 名字（与 `serial_line_ending` 互逆）。
[[nodiscard]] auto line_ending_name(term::LineEnding ending) -> std::string_view;

/// @brief 行尾 → 落地字节（D11① 的写出侧唯一折算点取用；长度 1 或 2）。
[[nodiscard]] auto line_ending_bytes(term::LineEnding ending) -> std::string_view;

/// @brief 校验结论（None＝合法）。
enum class SerialProfileIssue : std::uint8_t {
    None,                  ///< 合法。
    DevicePathEmpty,       ///< 缺设备节点名。
    BaudOutOfRange,        ///< 速率不在 [1,4000000]（与设置域 `baud` 同区间）。
    DataBitsOutOfRange,    ///< 数据位不在 [5,8]。
    StopBitsOutOfRange,    ///< 停止位不在 [1,2]。
    UnknownParity,         ///< 校验位名不在表内。
    UnknownFlow,           ///< 流控名不在表内。
    UnknownLineEnding,     ///< 行尾名不在表内。
    EncodingEmpty,         ///< 缺会话编码名。
};

/// @brief 档案整体校验：逐字段查取值域，第一个越界项即结论（顺序见枚举声明次序）。
///
/// 速率只判区间、不判档位表：手工写进 `borealis.json` 的非标速率（如 4800）在设备上
/// 照样能设，判它非法只会让档案连不上而换不来任何安全；档位表约束的是**可选项**
/// （下拉与热改入口），不是**合法值**。
[[nodiscard]] auto validate_serial_profile(const SerialProfile &profile) -> SerialProfileIssue;

/// @brief 平台无关的生效参数组合（档案折出来的、给平台腿直接翻译的形态）。
struct SerialFrameBits {
    int baud{115200};                                ///< 已确认在区间内的速率。
    int data_bits{8};                                ///< 5..8。
    int stop_bits{1};                                ///< 1..2。
    SerialParity parity{SerialParity::None};         ///< 校验位。
    SerialFlow flow{SerialFlow::None};               ///< 流控。

    [[nodiscard]] auto operator==(const SerialFrameBits &) const noexcept -> bool = default;
};

/// @brief 档案 → 生效参数（校验通过才出值；不合法回 nullopt）。
///
/// 折算只有一处，故「档案里的字符串」与「会话跑的参数」之间不存在第二套解释。
[[nodiscard]] auto fold_serial_frame(const SerialProfile &profile) -> std::optional<SerialFrameBits>;

/// @brief 设备可达性（D9①：不可达项照列 + 置灰 + 归因，不从列表里消失）。
enum class SerialPortAccess : std::uint8_t {
    Openable,    ///< 试开得动——唯一可发起连接的档。
    Denied,      ///< 权限不足（EACCES/EPERM：节点在但当前用户不在其属组）。
    Busy,        ///< 已被占用（EBUSY/TIOCEXCL 一类）。
    Unavailable, ///< 暂时打不开（ENXIO/ENODEV；拔掉但未消失的 USB 口常见）。
};

/// @brief 一行设备枚举结果（平台腿填、UI 只读投影）。
struct SerialPortInfo {
    std::string path;                                        ///< 交给平台腿打开的原样名字。
    std::string friendly;                                    ///< 友好名；取不到回空串（F3：本环境无 USB 口可测）。
    SerialPortAccess access{SerialPortAccess::Unavailable};  ///< 可达性。

    [[nodiscard]] auto operator==(const SerialPortInfo &) const noexcept -> bool = default;
};

/// @brief 该行是否可发起连接（置灰判据；只有 Openable 能连）。
[[nodiscard]] auto serial_port_is_selectable(const SerialPortInfo &port) -> bool;

/// @brief 就地整理枚举结果：自然序（字母干大小写不敏感 → 尾数字按数值 → 全名）+ 按 path 去重。
///
/// 排序**不看可达性**：刷新一次就换行位会让用户正在点的下拉跳行（D8① 的取舍延续）；
/// 不可达项靠置灰与归因文案分辨（D9①）。
auto sort_serial_ports(std::vector<SerialPortInfo> &ports) -> void;

}  // namespace borealis::conn
