#pragma once

// ============================================================
// 串口监视卡的行折算纯逻辑件（src/ui/serial_monitor_format.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 那条延后子项「十六进制收发视图」的**显示期**折算：环里的原始 run
// （方向 + 一个时刻 + 字节）→ 一屏转储行。全部只依赖标准类型与 `session::RawTap` 的值形态，
// 不含 `aurora`/`au::` 类型（`serial_format` / `tunnel_format` / `keys_format` 同族先例，
// AGENTS §4.4 第 20 条要的「纯逻辑层可独立单测」）。
//
// 为什么折算件在 `src/ui/` 而不是 `src/session/`（稿 §4）：行形态是显示期决定——每行几个字节、
// 要不要时间戳前缀都在这件里生效；放进 session 层就是让公共头认识「一行几个字节」这种界面量。
// 采集腿（`session::RawTap`）因此可以只做一次拷贝，视图档改而采集腿不动（D3①）。
//
// 三条形态口径来自稿 §2 与 §3：
//   ⑴ 行**按设备行边界**成行（CR／LF／CRLF 各算一条断点），行内再按每行字节数分格；
//   ⑵ 挂起尾段不与后续 run 断开——一条被切成两个读块的设备行仍显示为同一段（F10 实测 15 + 3
//       字节那一例）。本件对「快照末尾那段还没有行尾跟随的字节」的落地口径是**上屏成不完整行**：
//       环里存着却不显示的字节等于说谎（二进制协议可以整段没有 CR／LF），而它一旦等到行尾，
//       下一次重建就自动把它并回同一段——`snapshot()` 是 latest-value，没有游标也就不会漏。
//   ⑶ 尾栏只承 ASCII 可打印区（0x20–0x7E），其余字节一律一个 `.`；十六进制正文一律大写字母。
//       可见文案里除了 `RX`／`TX` 两个字母不出中文（面板上的措辞一律走 `settings_label(key)`）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "borealis/session/raw_tap.h"

namespace borealis::ui {

/// @brief 缺省每行字节数（D5①）。
///
/// 8 字节折成 54 字符（`[HH:MM:SS.mmm] ` 15 + `RX` 2 + 两空格 2 + 十六进制段 23 + 两空格 2 +
/// `|ASCII|` 10），在 14 pt 等宽（F8：单格 11 dp）里是 594 dp，容得进 640 dp 的卡片；16 字节档
/// 要 946 dp，等于在 960 dp 的窗里盖住整个终端，故否掉（D5③）。
inline constexpr std::size_t kMonitorBytesPerRow = 8;

/// @brief 行折算的选项。
///
/// 两枚都是**面板局部量**（D9①：零新设置键），重启回到这里写的缺省档。
struct MonitorRowOptions {
    std::size_t bytes_per_row = kMonitorBytesPerRow;  ///< 0 视作缺省档，不另立「无字节」形态。
    bool timestamp_prefix = true;                     ///< 关掉即不折前缀（也不调 `localtime`）。
};

/// @brief 一条显示行。
///
/// 三段各自成套、拼起来就是那一行：面板把 `timestamp_prefix` 与 `dump` 画成默认色的 `Text`，
/// `direction_label` 画成一列强调色（稿 §2「RX／TX 只以这两个字母与一列强调色区分」）。
/// 三段之间**不带**隐式间距：间距是字符串自己的空格，等宽族下逐字符对齐这件事只由本件负责。
struct MonitorRow {
    session::RawDirection direction = session::RawDirection::Received;  ///< 强调色只吃这一枚字段。
    std::string timestamp_prefix;  ///< `[HH:MM:SS.mmm] `（含尾随空格）；开关关掉时为空串。
    std::string direction_label;   ///< `"RX"` ／ `"TX"`（恒 2 字符）。
    std::string dump;              ///< 两空格 + 十六进制段 + 两空格 + `|ASCII|`。

    /// @brief 逐字段相等（面板状态泵的 diff 基准：`tick()` 拿它判「这一帧要不要重建行区」）。
    ///
    /// 三格都是值语义，故 defaulted 就是要的判据——不比指针、不比绘制态。
    [[nodiscard]] auto operator==(const MonitorRow &) const -> bool = default;
};

/// @brief 一行的完整串形：三段顺序拼接（`visible_rows()` 与逐字符断言都读它）。
[[nodiscard]] auto row_text(const MonitorRow &row) -> std::string;

/// @brief run 表 → 显示行表（纯函数：不持状态、不取时钟、不做 IO）。
///
/// 一条行的方向与时刻取**该行首字节所在的那个 run**（D6①：run 级单点时间戳，续行不另取）；
/// 一条长行按每行字节数切成多行，最后一条可以不完整。
/// @param runs `RawTap::snapshot()` 的产物，须是到达序（最旧在前）。
/// @param options 每行字节数与时间戳档。
/// @return 显示行表，顺序与环内字节顺序一致；输入为空即返回空表（面板据此画「等待数据」）。
[[nodiscard]] auto monitor_rows(const std::vector<session::RawRun> &runs,
                                const MonitorRowOptions &options) -> std::vector<MonitorRow>;

/// @brief epoch 毫秒 → `[HH:MM:SS.mmm] `（本地历法，含尾随空格）。
///
/// 格式化只发生在面板重建那一次：F5 实测 `localtime` + `strftime` 是 0.204 µs／行、占一行折算的
/// 85%，塞进读线程就是每批一次（D6④ 否掉的形态）。毫秒三位**零填充**，且负时刻（时钟回拨）
/// 向下取整到秒而不是截断，否则 `HH:MM:SS` 与毫秒位会差一秒。
[[nodiscard]] auto monitor_timestamp_prefix(std::int64_t timestamp_ms) -> std::string;

}  // namespace borealis::ui
