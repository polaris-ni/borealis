#pragma once

// ============================================================
// OSC 52 读方向的授权闸门（include/borealis/term/clipboard_read.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.12`：读方向**默认禁止**，可在设置里放开到「允许」或「每次询问」；
// 被拒时须向会话回写空响应而非静默无响应。本文件只给「闸门 + 待答请求 + 编码」三件纯逻辑，
// 读剪贴板、弹窗询问、回写应答都不在这里（分别在主线程的装配层与 `ClipboardOutbox`）。
//
// 三条决定形态的 WHY：
//
//   1. **闸门是 app 级一份、状态机现读**，不是构造期快照：这一档关掉的是「远端能不能读你的
//      剪贴板」，延后生效等于留一个跨标签的洞；而装配层的设置广播本来就只覆盖当前标签，
//      走快照还得先扩广播。读一枚 atomic 不是 IO，故现读不违反架构 §3.4「临界区内不做阻塞 IO」。
//      **未接线即 `Deny`**（fail-closed）：漏接线的后果是「读不到剪贴板」，而不是「谁都读得到」。
//   2. **状态机只登记、不答话**：一次剪贴板读在 POSIX 腿是一次子进程往返（框架经 `xsel`/`xclip`
//      外壳桥），把它算进网格锁等于让一次子进程阻塞该会话的解码与另一线程的取网格。
//   3. **base64 只有解码件是历史欠账**：`osc::decode_base64` 服务写方向，读方向要往外编码，
//      故本件补上互逆的那一半（标准字母表 + `=` 填充），供 `ClipboardOutbox` 拼应答。
// ============================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace borealis::term {

/// @brief OSC 52 读方向的三态授权档（落盘名 `deny` / `allow` / `ask` 的映射归 `config` 侧）。
enum class ClipboardReadPolicy : std::uint8_t {
    Deny,  ///< 禁止：立刻回写空响应（缺省档，判据 ①）。
    Allow, ///< 允许：登记待答请求，由主线程把本地内容编码后回写。
    Ask,   ///< 每次询问：同样登记，由主线程逐条问用户（判据 ②）。
};

/// @brief 一个会话同时挂起的读请求上限。
///
/// 会话字节流是不可信输入：远端可以连发 `OSC 52 ; c ; ?` 把我们的待答队列与询问框撑爆。
/// 满队列时**当场回写空响应**并计数，而不是把某一条悄悄丢掉——「被拒 ⇒ 有回写」是判据 ③，
/// 静默丢弃会让先问的那条远端一直等下去。
inline constexpr std::size_t kMaxClipboardReadRequests = 8U;

/// @brief 一条待答的读请求。
struct ClipboardReadRequest {
    /// @brief 回显用的选择位（`OSC 52 ; <Pc> ; ?` 的首段）。
    ///
    /// 恒为**单个 ASCII 字符**：状态机侧已把非法或缺段的取值归一到 `c`。回显只服务「答给问的那一位」，
    /// 而内容恒取系统剪贴板——框架只有一格 CLIPBOARD，`s`/`p`/`0`–`7` 那套选择位无处可取。
    std::u32string pc{U'c'};
};

/// @brief 跨会话共享的读授权闸门：一份原子值，状态机与装配层各读各写。
///
/// 生命周期归装配层（app 级单例），状态机只持观察指针——与 `ResponseSink` 同一套接线形态。
/// 可复制与可移动一律禁掉：拷贝一份出去就等于把「改档即刻生效」变成「改了个副本」。
class ClipboardGate final {
  public:
    ClipboardGate() noexcept = default;

    /// @brief 以 @p initial 开闸（装配层从设置里读到哪档就建哪档）。
    /// @param initial 初始授权档。
    explicit constexpr ClipboardGate(ClipboardReadPolicy initial) noexcept
        : policy_{static_cast<std::uint8_t>(initial)} {}

    ClipboardGate(const ClipboardGate &other) = delete;
    auto operator=(const ClipboardGate &other) -> ClipboardGate & = delete;
    ClipboardGate(ClipboardGate &&other) = delete;
    auto operator=(ClipboardGate &&other) -> ClipboardGate & = delete;
    ~ClipboardGate() noexcept = default;

    /// @brief 当前授权档（未设置即 `Deny`）。
    [[nodiscard]] auto policy() const noexcept -> ClipboardReadPolicy;

    /// @brief 改档：对所有已接线与后续新建的会话即刻生效。
    /// @param policy 新档。
    auto set_policy(ClipboardReadPolicy policy) noexcept -> void;

  private:
    std::atomic<std::uint8_t> policy_{static_cast<std::uint8_t>(ClipboardReadPolicy::Deny)};
};

/// @brief 按标准 base64 字母表编码，含 `=` 填充。
///
/// 与 `osc::decode_base64` 互逆：那边是「载荷 → UTF-8 字节 → 码点」，这边是「UTF-8 字节 → 载荷」。
/// @param bytes 待编码字节（通常是 `encode_utf8` 的产物）。
/// @return 纯 ASCII 的编码串；空输入返回空串。
[[nodiscard]] auto encode_base64(std::span<const std::byte> bytes) -> std::string;

}  // namespace borealis::term
