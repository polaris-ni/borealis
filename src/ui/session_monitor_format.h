#pragma once

// ============================================================
// 字节监视卡的行折算纯逻辑件（src/ui/session_monitor_format.h）
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
//
// 四期（稿 §11）在同一台机器上加了第二种**行内容**形态：`MonitorRowOptions::view` 取 `Text` 时，
// `dump` 装的是按会话编码解码后的文本（`bytes_per_row` 让给 `kMonitorTextRowBytes` 那枚字节预算）。
// 断点、跨 run 拼接、不完整尾行这些行边界口径两档共用一份——变的只是「一段字节怎么变成一行字」。
//
// 六期（稿 §13）加的是第二种**行种类**：`MonitorRowOptions::boundary_rows` 为真时，一条 run 若在
// 自然行界上收尾（该 run 交出时挂起缓冲已空且当前显示行已收口），紧随其后多画一条分隔行
// （`-- RX 12 B (+812 ms) --`）。三个读数全取自环里已有的 run 级料（方向、条数、与上一 run 的时刻差），
// 故采集腿一行不改。两条口径要记住：① **行中到达不插**——一条设备行被读块切成两段时中间那条界不在屏上，
// 否则就推翻了口径 ⑵ 那条已裁的「挂起尾段不与后续 run 断开」（稿 F23／D44① 的代价）；② 边界行与内容行
// **同进计数侧与折行侧**，`monitor_row_count()` 与 `monitor_rows()` 的恒等式（稿 F24）在开档下仍成立。
// 边界行不吃时间戳前缀：它的时刻就是上面那条内容行的时刻，再折一次 `localtime` 是纯浪费。
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

/// @brief 文本档每行的**字节**预算（稿 §11 D27）。
///
/// 刻意按字节而不是按码点切：`monitor_row_count()` 因此不需要解码就能给出真实总行数，两档共用同一台
/// 扫描器这一条纪律才不破（按码点切就必须先解完整段才知道行落在哪）。
///
/// 这一格的算术与十六进制档同一条界：卡片 `.width(640.0F)` 减双侧 12 dp 内边距＝**616 dp** 净宽
/// （F8：14 pt 等宽单格 11 dp ⇒ 56 格；`Modifier::transform` 只按内边距收缩内容盒，1 dp 描边不吃布局宽，
/// 与 `mono_cell` 头注那句「卡内净宽 616 dp」同一口径）。减去前缀 15 与方向 2 与两空格 2＝19 格，
/// 上限本可取到 37，这里**留一格余量取 36**：整行 55 格＝605 dp，剩 11 dp。正好铺满 56 格那一档在
/// 十六进制档没有先例（它是 54 格），而文本档的每一行都会走到满格，量化的次像素推进一旦进位就在最右
/// 截掉一格——调试视图少看一个字符比少看一行糟。取字节数为界是安全的上界：这些档位里**每字节的显示宽度
/// 不超过一格**（ASCII／Latin-1 一字节一格；GBK／Big5 两字节一个宽字符＝两格；UTF-8 三字节一个宽字符＝两格；
/// GB18030 四字节一个宽字符＝两格），故 36 字节的文本段恒 ≤ 36 格。窄了不亏：CJK 料一行装得下的字数本就少。
///
/// 越界的代价不是「难看」而是**几何坏了**：行高是 `LazyList` 的固定 `item_extent`（D15①，20 dp），
/// 一行折成两截即截字并撑破行高——所以这一格是硬界，不是审美选择。
inline constexpr std::size_t kMonitorTextRowBytes = 36;

/// @brief 行形态档（稿 §11 D26）：同一份料源的两种显示形状，面板局部量，不是设置键。
enum class MonitorView : std::uint8_t {
    Hex,   ///< 缺省档：每行 `bytes_per_row` 字节的十六进制段 + ASCII 尾栏。
    Text,  ///< 文本档：按会话编码解码后的文本，CR/LF 只作断点，其余不可打印码点一个 `.`（D29）。
};

/// @brief 行种类（稿 §13 D44）：内容行是缺省档，边界行是那条 `-- RX 12 B (+812 ms) --`。
///
/// 缺省取 `Content` 是有意的：既有 26 案与装配层的构造都不写这一格，它们的语义本来就是内容行。
/// 面板据它决定整行色（边界行走次要色，不吃方向强调），折算件据它决定 `dump` 的形态。
enum class MonitorRowKind : std::uint8_t {
    Content,  ///< 一段字节的显示行（十六进制档或文本档）。
    Boundary, ///< 报文边界行：一条 run 在自然行界上收尾时紧随其后的那一条分隔行。
};

/// @brief 行折算的选项。
///
/// 每一枚都是**面板局部量**（D9①：零新设置键），重启回到这里写的缺省档。
struct MonitorRowOptions {
    std::size_t bytes_per_row = kMonitorBytesPerRow;  ///< 0 视作缺省档，不另立「无字节」形态；文本档不吃这一格。
    bool timestamp_prefix = true;                     ///< 关掉即不折前缀（也不调 `localtime`）。
    MonitorView view = MonitorView::Hex;              ///< 文本档时 `dump` 装的是解码文本（稿 D26／D27）。
    std::string encoding{"UTF-8"};                    ///< 文本档的解码档名；取**实际生效腿**名（稿 D28）。
    bool boundary_rows = false;                       ///< 报文边界行上屏（稿 §13 D46①：缺省关，翻档不改既有行）。
};

/// @brief 一条显示行。
///
/// 三段各自成套、拼起来就是那一行：面板把 `timestamp_prefix` 与 `dump` 画成默认色的 `Text`，
/// `direction_label` 画成一列强调色（稿 §2「RX／TX 只以这两个字母与一列强调色区分」）。
/// 三段之间**不带**隐式间距：间距是字符串自己的空格，等宽族下逐字符对齐这件事只由本件负责。
/// 边界行是这一形的例外：`timestamp_prefix` 与 `direction_label` 恒空，整句（含 `RX`／`TX`）在 `dump` 里。
struct MonitorRow {
    MonitorRowKind kind = MonitorRowKind::Content;  ///< 行种类；缺省＝内容行，既有构造不必改口。
    session::RawDirection direction = session::RawDirection::Received;  ///< 强调色只吃这一枚字段。
    std::string timestamp_prefix;  ///< `[HH:MM:SS.mmm] `（含尾随空格）；开关关掉时为空串。
    std::string direction_label;   ///< `"RX"` ／ `"TX"`（恒 2 字符）；边界行为空串。
    std::string dump;              ///< 十六进制档＝两空格 + 十六进制段 + 两空格 + `|ASCII|`；文本档＝两空格 + 解码文本；边界行＝整句分隔行。

    /// @brief 逐字段相等（面板状态泵的 diff 基准：`tick()` 拿它判「这一帧要不要重建行区」）。
    ///
    /// 四格都是值语义，故 defaulted 就是要的判据——不比指针、不比绘制态。`kind` 进这一格是刻意的：
    /// 「只有边界行插进来了」那种帧必须让面板重画行区（稿 §13 F26）。
    [[nodiscard]] auto operator==(const MonitorRow &) const -> bool = default;
};

/// @brief 一行的完整串形：三段顺序拼接（`visible_rows()` 与逐字符断言都读它）。
[[nodiscard]] auto row_text(const MonitorRow &row) -> std::string;

/// @brief 折行后的总行数（只计数、不建字符串）。
///
/// 与 `monitor_row_window()` 共用同一台扫描器，故「数出来的行数」与「折出来的行数」由结构保证相等
/// （稿 §9.3：一期那条 `monitor_rows` 也改走同一台扫描器，四条行边界口径只写一遍）。
/// 面板用它给 `LazyList` 一个真实的总行数，而不必把 256 KiB 环折成三万多条字符串。
/// @param runs `RawTap::snapshot()` 的产物，须是到达序（最旧在前）。
/// @param options 视图档与每行字节数（0 视作缺省档）；时间戳档与编码档不影响计数——文本档的预算按字节，
///        故这一条不需要解码就能给出真实总行数（稿 D27 换来的那条对称性）。
///        边界档（`boundary_rows`）**影响**计数，且与 `monitor_row_window()` 同进同一台扫描器（稿 §13 F24）。
/// @return 显示行总数；空环即 0。
[[nodiscard]] auto monitor_row_count(const std::vector<session::RawRun> &runs,
                                     const MonitorRowOptions &options) -> std::size_t;

/// @brief 折出**一段**显示行：`[first_row, first_row + max_rows)`（纯函数）。
///
/// 冻结回看时面板只折当前窗口（稿 §9.2 D11①），比全折省下 `环形总行数 × 0.239 µs`（F5）。
/// 窗口之前与之后的行只推进计数、不建字符串；窗口内每行的形态与全折逐字符一致。
/// @param runs `RawTap::snapshot()` 的产物，须是到达序（最旧在前）。
/// @param options 视图档、每行字节数、时间戳档与编码档。
/// @param first_row 窗口首行的全局行号；越过总行数即返回空表。
/// @param max_rows 窗口行数上限；0 即返回空表。超尾自动截短。
/// @return 窗口内的行表，顺序与环内字节顺序一致。
[[nodiscard]] auto monitor_row_window(const std::vector<session::RawRun> &runs,
                                      const MonitorRowOptions &options, std::size_t first_row,
                                      std::size_t max_rows) -> std::vector<MonitorRow>;

/// @brief run 表 → 全部显示行（纯函数：不持状态、不取时钟、不做 IO）。
///
/// 一条行的方向与时刻取**该行首字节所在的那个 run**（D6①：run 级单点时间戳，续行不另取）；
/// 一条长行按每行字节数切成多行，最后一条可以不完整。
/// @param runs `RawTap::snapshot()` 的产物，须是到达序（最旧在前）。
/// @param options 视图档、每行字节数、时间戳档与编码档。
/// @return 显示行表，顺序与环内字节顺序一致；输入为空即返回空表（面板据此画「等待数据」）。
[[nodiscard]] auto monitor_rows(const std::vector<session::RawRun> &runs,
                                const MonitorRowOptions &options) -> std::vector<MonitorRow>;

/// @brief 全部显示行 → 一整份可交出去的文本（稿 §10.2 D19①：复制与导出共用的唯一串形）。
///
/// 逐行 `row_text()` 以 `\n` 相连、**结尾不带换行**：剪贴板要的就是这一串（尾随换行是噪声），写盘那一条腿
/// 自己在末尾补一个 `\n`（文本档惯例）。口径与屏面**同源**——同一台扫描器、同一份 `MonitorRowOptions`，
/// 故文件里的每一行都能在屏上逐字符找到；本件不另造第二台格式化机器（D19③ 的否决理由）。
/// @param runs `RawTap::snapshot()` 的产物，须是到达序（最旧在前）。
/// @param options 视图档、每行字节数、时间戳档与编码档——与画这一屏用的是同一份档（稿 D19①／D30①）。
/// @return 行文本；输入为空即返回空串（调用方据此整条不跑，见稿 D21①）。
[[nodiscard]] auto monitor_dump_text(const std::vector<session::RawRun> &runs,
                                     const MonitorRowOptions &options) -> std::string;

/// @brief epoch 毫秒 → `[HH:MM:SS.mmm] `（本地历法，含尾随空格）。
///
/// 格式化只发生在面板重建那一次：F5 实测 `localtime` + `strftime` 是 0.204 µs／行、占一行折算的
/// 85%，塞进读线程就是每批一次（D6④ 否掉的形态）。毫秒三位**零填充**，且负时刻（时钟回拨）
/// 向下取整到秒而不是截断，否则 `HH:MM:SS` 与毫秒位会差一秒。
[[nodiscard]] auto monitor_timestamp_prefix(std::int64_t timestamp_ms) -> std::string;

}  // namespace borealis::ui
