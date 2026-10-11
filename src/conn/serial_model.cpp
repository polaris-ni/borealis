// ============================================================
// 串口参数纯逻辑模型实现（src/conn/serial_model.cpp）
// ------------------------------------------------------------
// 接口与口径见 conn/serial_model.h。本 TU 只做标准类型的查表、区间判断与
// 排序，没有任何系统调用（平台腿见 src/platform/{posix,win}/serial.cpp）。
// ============================================================

#include "conn/serial_model.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace borealis::conn {
namespace {

/// @brief 闭区间判据（与 `config/store.cpp` 的 integer 读取区间同口径）。
[[nodiscard]] auto in_range(int value, int low, int high) -> bool {
    return (value >= low) && (value <= high);
}

/// @brief 名表查下标：大小写敏感，与设置域的 `one_of` 判据一致（不做宽容）。
template <std::size_t Size>
[[nodiscard]] auto name_index(const std::array<std::string_view, Size> &names, std::string_view name)
    -> std::optional<std::size_t> {
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::ranges::distance(names.cbegin(), found));
}

/// @brief 枚举值 → 名表下标；越界值夹回 0（缺省档），不让一个坏枚举变成空串文案。
[[nodiscard]] auto clamped_index(std::size_t value, std::size_t size) -> std::size_t {
    return value < size ? value : 0U;
}

/// @brief 自然序排序键：字母干（小写）+ 尾数字。
struct PortKey {
    std::string stem{};
    long long number{-1};  ///< 无尾数字时的哨兵：排在同干的字面项最前。
};

/// @brief 取路径末段并拆出尾数字（`/dev/ttyS12` → 干 `ttys` + 数 12）。
[[nodiscard]] auto port_sort_key(const std::string &path) -> PortKey {
    const auto slash = path.find_last_of('/');
    const auto leaf = (slash == std::string::npos) ? std::string_view{path}
                                                   : std::string_view{path}.substr(slash + 1);
    auto head = leaf;
    long long number = -1;
    std::size_t digits = 0;
    while (digits < head.size() && head[head.size() - 1 - digits] >= '0' &&
           head[head.size() - 1 - digits] <= '9') {
        ++digits;
    }
    if (digits > 0) {
        const auto tail = head.substr(head.size() - digits);
        // 位数钳制：超长的数字尾不撑爆 long long，按饱和值参与比较即可。
        number = (digits > 18) ? 9'223'372'036'854'775'807LL : std::stoll(std::string{tail});
        head = head.substr(0, head.size() - digits);
    }
    auto stem = std::string{head};
    std::ranges::transform(stem, stem.begin(), [](const unsigned char ch) -> char {
        return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 32) : static_cast<char>(ch);
    });
    return PortKey{std::move(stem), number};
}

}  // namespace

auto is_supported_baud(int baud) -> bool {
    return std::ranges::find(kSerialBaudRates, baud) != kSerialBaudRates.end();
}

auto serial_parity(std::string_view name) -> std::optional<SerialParity> {
    const auto index = name_index(kSerialParityNames, name);
    if (!index) {
        return std::nullopt;
    }
    return static_cast<SerialParity>(*index);
}

auto serial_flow(std::string_view name) -> std::optional<SerialFlow> {
    const auto index = name_index(kSerialFlowNames, name);
    if (!index) {
        return std::nullopt;
    }
    return static_cast<SerialFlow>(*index);
}

auto serial_line_ending(std::string_view name) -> std::optional<term::LineEnding> {
    switch (name_index(kSerialLineEndingNames, name).value_or(kSerialLineEndingNames.size())) {
        case 0:
            return term::LineEnding::Lf;
        case 1:
            return term::LineEnding::Cr;
        case 2:
            return term::LineEnding::Crlf;
        default:
            return std::nullopt;
    }
}

auto serial_parity_name(SerialParity parity) -> std::string_view {
    return kSerialParityNames[clamped_index(static_cast<std::size_t>(parity), kSerialParityNames.size())];
}

auto serial_flow_name(SerialFlow flow) -> std::string_view {
    return kSerialFlowNames[clamped_index(static_cast<std::size_t>(flow), kSerialFlowNames.size())];
}

auto line_ending_name(term::LineEnding ending) -> std::string_view {
    switch (ending) {
        case term::LineEnding::Cr:
            return kSerialLineEndingNames[1];
        case term::LineEnding::Crlf:
            return kSerialLineEndingNames[2];
        case term::LineEnding::Lf:
            return kSerialLineEndingNames[0];
    }
    return kSerialLineEndingNames[0];
}

auto line_ending_bytes(term::LineEnding ending) -> std::string_view {
    switch (ending) {
        case term::LineEnding::Cr:
            return "\r";
        case term::LineEnding::Crlf:
            return "\r\n";
        case term::LineEnding::Lf:
            return "\n";
    }
    return "\n";
}

auto validate_serial_profile(const SerialProfile &profile) -> SerialProfileIssue {
    if (profile.device_path.empty()) {
        return SerialProfileIssue::DevicePathEmpty;
    }
    if (!in_range(profile.baud, 1, 4000000)) {
        return SerialProfileIssue::BaudOutOfRange;
    }
    if (!in_range(profile.data_bits, 5, 8)) {
        return SerialProfileIssue::DataBitsOutOfRange;
    }
    if (!in_range(profile.stop_bits, 1, 2)) {
        return SerialProfileIssue::StopBitsOutOfRange;
    }
    if (!serial_parity(profile.parity)) {
        return SerialProfileIssue::UnknownParity;
    }
    if (!serial_flow(profile.flow)) {
        return SerialProfileIssue::UnknownFlow;
    }
    if (!serial_line_ending(profile.line_ending)) {
        return SerialProfileIssue::UnknownLineEnding;
    }
    if (profile.encoding.empty()) {
        return SerialProfileIssue::EncodingEmpty;
    }
    return SerialProfileIssue::None;
}

auto fold_serial_frame(const SerialProfile &profile) -> std::optional<SerialFrameBits> {
    if (validate_serial_profile(profile) != SerialProfileIssue::None) {
        return std::nullopt;
    }
    SerialFrameBits bits;
    bits.baud = profile.baud;
    bits.data_bits = profile.data_bits;
    bits.stop_bits = profile.stop_bits;
    bits.parity = serial_parity(profile.parity).value_or(SerialParity::None);
    bits.flow = serial_flow(profile.flow).value_or(SerialFlow::None);
    return bits;
}

auto serial_port_is_selectable(const SerialPortInfo &port) -> bool {
    return port.access == SerialPortAccess::Openable;
}

auto sort_serial_ports(std::vector<SerialPortInfo> &ports) -> void {
    std::vector<PortKey> keys{};
    keys.reserve(ports.size());
    for (const auto &port : ports) {
        keys.push_back(port_sort_key(port.path));
    }
    // 索引置换而不是直接排 vector：排序键算一次就锁一次，比较器里不再拆字符串。
    std::vector<std::size_t> order{};
    order.reserve(ports.size());
    for (std::size_t index = 0; index < ports.size(); ++index) {
        order.push_back(index);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) -> bool {
        const auto &a = keys[left];
        const auto &b = keys[right];
        if (a.stem != b.stem) {
            return a.stem < b.stem;
        }
        if (a.number != b.number) {
            return a.number < b.number;
        }
        return ports[left].path < ports[right].path;
    });

    std::vector<SerialPortInfo> sorted{};
    sorted.reserve(ports.size());
    for (const auto index : order) {
        // 同 path 只留第一条：平台腿在 `/dev` 与 `/sys` 两处都可能报出同一节点。
        const auto duplicate = std::ranges::find(sorted, ports[index].path, &SerialPortInfo::path);
        if (duplicate != sorted.end()) {
            continue;
        }
        sorted.push_back(std::move(ports[index]));
    }
    ports = std::move(sorted);
}

}  // namespace borealis::conn
