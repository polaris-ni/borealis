#pragma once

// ============================================================
// 打开外部 URI 的平台腿（src/platform/open_url.h）
// ------------------------------------------------------------
// SPEC.FEAT.INTERACT.05（裁决 7.119 D10①）：终端里点一条链接，最终要由**桌面环境**
// 的默认程序接住。三条读数决定了本腿的形状，逐条写死在下面：
//
//   ⑴ **不等子进程、不等 IO 完成**（§0 的 F13：双 fork 一次 176.97 µs，`/bin/true` 档）。
//       因此本函数**不属** `file_secure.h` 那句「阻塞 IO，调用方须在 worker 线程」的范围
//       （F21）：它就是一次 fork/exec 系统调用，从 UI 事件回调直接调是裁决 D10① 选的档，
//       挪进 worker 反而丢掉「点击即打开」的手感与证人面。
//   ⑵ **双 fork 拿不到孙进程退出码** ⇒ `Opened` 的确切含义是「已发出」，**不承诺**
//       浏览器真的起了一扇窗口（D10① 的代价 ⑴）。于是本期没有「已在系统中打开」那类
//       回执：没有证人的陈述不进界面。
//   ⑶ **URI 是不可信输入**（它来自会话字节流），而 `exec` 族不经 shell ⇒ 注入面天然封住
//       （F14 的一手探针：`execvp("/bin/true", {"/bin/true", "; touch …/INJECTED"})`
//       之后标记文件未生成）。故本腿**绝不** `system()`、绝不拼命令行、绝不 `sh -c`，
//       URI 只以 argv 里的一枚参数交出去。
//
// 平台知识不外溢（AGENTS.md §4.5 第 23 条）：`fork`/`execvp`/`access` 与 Win32 的
// `ShellExecuteW` 各自只进 `src/platform/{posix,win}/open_url.cpp`，UI 与装配层只消费
// 下面这一个枚举加一个函数。
//
// **测试与自动化一律不得走真打开腿**：§0 的 F15 记的是一次一手事故——`timeout 8
// xdg-open "https://example.com"` 在本机 rc=0 并真的 exec 了 msedge，在用户的桌面上
// 开出了一扇窗口。故 UI 侧的 `Presentation::open_url` 接缝**没有**「留空即调本腿」的
// 回落，装配层才把它指过来；itest 断的是「该不该开、交出去的逐字节是哪条串」。
// ============================================================

#include <cstdint>
#include <string_view>

namespace borealis::platform {

/// @brief 把一条 URI 交给系统默认程序的结果分档。
enum class OpenUrlOutcome : std::uint8_t {
    Opened,    ///< 已发出：孙进程（POSIX）或子进程（Win32）已起来，**不**代表窗口真的出现了。
    NotFound,  ///< 系统里没有可用的桥：POSIX 腿＝`PATH` 上找不到可执行的 `xdg-open`。
    Error,     ///< 发不出去：入参非法（空串或含 C0 控制字符）、`fork` 失败、平台侧拒绝（细节已记日志）。
};

/// @brief 用系统默认程序打开一条 URI（本期只有 http/https 两类，白名单在识别处已把关）。
///
/// 语义边界三条：⑴ 入参按 **UTF-8** 交出，宽字符与码页 API 挡在实现侧（裁决 7.11）；
/// ⑵ 含 NUL 或任何 C0 控制字符 / DEL 的串直接 `Error` 而**不清洗**——链接的字符表本就不产
/// 这样的串，收到它们意味着调用方越过了识别件，那是要显式失败而不是猜一个安全形态；
/// ⑶ 本函数**同步返回但不等待打开结果**（见头注⑴），多次调用彼此独立，不留任何句柄给调用方收。
///
/// @param utf8_target 目标 URI 全文（提示条与确认框显示的那一枚，逐字节即打开的那一枚）。
/// @return 分档结果；除 `Opened` 外的每一档都已在实现侧记了日志，调用方**不需要**再报一遍。
[[nodiscard]] auto open_url(std::string_view utf8_target) -> OpenUrlOutcome;

}  // namespace borealis::platform
