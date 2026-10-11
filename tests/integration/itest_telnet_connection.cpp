/// 测试类型: integration
/// 目标单元: src/conn/telnet_connection.{h,cpp}（Telnet 连接腿）+ src/session/session.cpp
/// 测试说明: `SPEC.FEAT.CONN.06` 批 2 的环回证人（裁决 7.112 D12② 的第二层）——假服务端跑在
///           `127.0.0.1` 的内核择定端口上（每条用例一台，端口随监听点一起消失），两端字节逐字节
///           可查，故整条 IAC 协商腿不需要任何协议近似即可证（稿 §0 的 F2；本机无 telnetd，
///           F1 挡住的是真服务器那一腿，登记为 `etest_telnet_server` 在册不写）。证的五件事：
///           上线发起序真的到了对端、对端的命令段与净数据在同一条流上分得开（判据 5 的现场版）、
///           尺寸槽的 NAWS 上线且同值不再发（判据 8）、掉线两态各归其档（判据 7：EOF⇒RemoteExit
///           不进环／队列里留有未读字节时的 RST⇒LinkLost 进环，且重拨连的是同一个监听点）、
///           写出侧的 0xFF 折成 `FF FF`（F7 的发送半）。最后一条把会话接上同一条腿，一次证两件
///           事：UTF-8 解码链（D5①）与「键表产 CR ∧ Telnet 落地 CRLF」（D8①）。
///
///           归因表 `telnet_close_reason()` 的四档由第一条用例按纯函数断言：RST 那一态在环回上
///           靠「只吞 4 字节就关」造得出，但把四档都压到时序上会让用例只证「恰好这么归因」，
///           而判据 7 要的是「表只有一份」。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
// 私有头（裁决 7.112 D11① 的先例）：连接腿与其归因表都在 src/ 树内。
#include "conn/telnet_connection.h"
#include "conn/telnet_negotiation.h"

namespace borealis::test_cases::itest_telnet_connection {

namespace {

using borealis::conn::RetryPolicy;
using borealis::conn::TelnetConnection;
using borealis::conn::TelnetLinkEnd;
using borealis::conn::TelnetProfile;
using borealis::grid::Storage;
using borealis::session::CloseReason;
using borealis::session::ConnectionEvents;
using borealis::session::ReconnectProgress;
using borealis::session::ReconnectStop;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::LineEnding;
using borealis::term::SessionEncoding;
using borealis::term::SingleWidthPolicy;
using borealis::term::TerminalDefaults;
using borealis::term::TermModes;

auto width_policy = std::make_shared<SingleWidthPolicy>();

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::uint8_t> {
    auto out = std::vector<std::uint8_t>{};
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] auto hex_of(const std::vector<std::uint8_t> &bytes) -> std::string {
    static constexpr std::string_view digits{"0123456789ABCDEF"};
    auto out = std::string{};
    for (const auto byte : bytes) {
        out.push_back(digits[(byte >> 4U) & 0x0FU]);
        out.push_back(digits[byte & 0x0FU]);
        out.push_back(' ');
    }
    return out;
}

/// @brief 收到的字节里出现过几段（口径 ⑵ 的「只答一次」要数次数，不是数有没有）。
[[nodiscard]] auto count_occurrences(const std::vector<std::uint8_t> &haystack,
                                     const std::vector<std::uint8_t> &needle) -> std::size_t {
    auto hits = std::size_t{0};
    for (auto it = haystack.begin();
         it = std::search(it, haystack.end(), needle.begin(), needle.end()), it != haystack.end();
         ++it) {
        ++hits;
    }
    return hits;
}

/// @brief 发起序（判据 3）：`IAC DO ECHO` + `IAC DO SGA`，各 3 字节。
const auto kOpening = std::vector<std::uint8_t>{0xFFU, 0xFDU, 0x01U, 0xFFU, 0xFDU, 0x03U};

/// @brief 假服务端一条连接的收摊方式——掉线两态的现场全在这里造（实测 F3 的两态）。
enum class PeerBehavior {
    StayOpen,       ///< 一直排空并记录，直到用例收摊（析构才关）：证协商、尺寸腿与转义。
    CleanClose,     ///< 先把发起序读干（队列里没有未读字节）再关 ⇒ 内核发有序 FIN。
    ResetThenRedial, ///< 只吞 4 字节就关 ⇒ 队列留着未读字节 ⇒ 内核发 RST，然后回 accept 等重拨。
};

/// @brief 环回假服务端：一个监听点 + 一个工作线程，把对端字节攒成可比对的快照。
///
/// 监听点在用例期间一直开着（`ResetThenRedial` 那一档要接第二回），故每条用例用各自的实例。
class LoopbackPeer {
  public:
    explicit LoopbackPeer(PeerBehavior behavior, std::vector<std::uint8_t> to_send = {})
        : behavior_{behavior}, to_send_{std::move(to_send)} {
        listener_ = platform::tcp_listen("127.0.0.1", 0);
        if (listener_ == nullptr) {
            return;
        }
        port_ = listener_->bound_port();
        worker_ = std::thread([this] { run(); });
    }

    LoopbackPeer(const LoopbackPeer &) = delete;
    auto operator=(const LoopbackPeer &) -> LoopbackPeer & = delete;

    ~LoopbackPeer() {
        stop_.store(true);
        if (worker_.joinable()) {
            worker_.join();  // 工作线程每轮读都是 20 ms 档，join 上界一个读分片。
        }
    }

    [[nodiscard]] auto ok() const -> bool { return port_ > 0; }
    [[nodiscard]] auto port() const -> int { return port_; }

    [[nodiscard]] auto accepted() -> bool { return connection_count() >= 1U; }

    /// @brief 已受理过几条连接：第二回 ≥2 就是「掉线后按同一份档案真拨回来了」的证人。
    [[nodiscard]] auto connection_count() -> std::size_t {
        const std::lock_guard lock{mutex_};
        return connections_;
    }

    [[nodiscard]] auto received() -> std::vector<std::uint8_t> {
        const std::lock_guard lock{mutex_};
        return received_;
    }

    /// @brief 收到的字节里是否出现过这一段（线上字节会被分片，故按子序列找而不是等长比对）。
    [[nodiscard]] auto received_contains(const std::vector<std::uint8_t> &needle) -> bool {
        const auto got = received();
        return std::search(got.begin(), got.end(), needle.begin(), needle.end()) != got.end();
    }

    /// @brief 收到的字节里出现过几段（「同一立场只答一次」要数次数，不是数有没有）。
    [[nodiscard]] auto received_count(const std::vector<std::uint8_t> &needle) -> std::size_t {
        return count_occurrences(received(), needle);
    }

    [[nodiscard]] auto received_text() -> std::string {
        auto out = std::string{};
        for (const auto byte : received()) {
            out.push_back(static_cast<char>(byte));
        }
        return out;
    }

    /// @brief 在同一条已受理的连接上追加一次写：让用例把一条 IAC 命令拆成两次送达（F6 的现场版）。
    ///
    /// 只在下一轮排空时写出，故调用方不碰 socket，也不与工作线程并发用同一条流。
    auto send_more(std::vector<std::uint8_t> bytes) -> void {
        const std::lock_guard lock{mutex_};
        pending_.push_back(std::move(bytes));
    }

  private:
    auto record(std::span<const std::uint8_t> bytes) -> void {
        const std::lock_guard lock{mutex_};
        received_.insert(received_.end(), bytes.begin(), bytes.end());
    }

    auto note_accepted() -> void {
        const std::lock_guard lock{mutex_};
        ++connections_;
    }

    /// @brief 取一份待写的追加字节（取走即从队列擦掉，写由工作线程做）。
    [[nodiscard]] auto take_pending() -> std::vector<std::uint8_t> {
        const std::lock_guard lock{mutex_};
        if (pending_.empty()) {
            return {};
        }
        auto out = std::move(pending_.front());
        pending_.erase(pending_.begin());
        return out;
    }

    /// @brief 分片受理：每 50 ms 查一次停止位，故用例收摊时工作线程至多再等一个片。
    ///
    /// `TcpListener::accept` 只有整段超时，直接等 3 s 会让「对端再没拨过来」的用例把
    /// 析构变成可观测的慢用例（测试时长也是读数）。
    [[nodiscard]] auto accept_sliced() -> std::unique_ptr<platform::TcpStream> {
        for (int waited = 0; waited < 3000 && !stop_.load(); waited += 50) {
            auto stream = listener_->accept(50);
            if (stream != nullptr) {
                return stream;
            }
        }
        return nullptr;
    }

    auto run() -> void {
        auto stream = accept_sliced();
        if (stream == nullptr) {
            return;  // 用例没拨过来：由它自己的 REQUIRE 报出来，这里不静默。
        }
        note_accepted();
        serve(*stream);
        if (behavior_ != PeerBehavior::ResetThenRedial) {
            return;
        }
        // 第二回：等重拨上来的连接。端口没变，故这一条才是「按档案重开」的直接证人。
        auto again = accept_sliced();
        if (again == nullptr) {
            return;
        }
        note_accepted();
        pump(*again);
    }

    /// @brief 一条连接的处置：按收摊方式分流；末了关流的方式决定对端看到的是 FIN 还是 RST。
    auto serve(platform::TcpStream &stream) -> void {
        if (!to_send_.empty()) {
            static_cast<void>(stream.write(to_send_));
        }
        auto buffer = std::vector<std::uint8_t>(4096U, 0U);
        switch (behavior_) {
        case PeerBehavior::StayOpen:
            pump(stream);
            return;  // 出作用域即关：用例已经收摊，这一关不影响任何断言。
        case PeerBehavior::CleanClose:
            // 读到「发起序已到手」为止：接收队列清空后才关，内核才发 FIN 而不是 RST（实测 F3）。
            while (!stop_.load() && received().size() < kOpening.size()) {
                const auto read = stream.read(std::span<std::uint8_t>{buffer}, 20);
                if (read.outcome == platform::TcpReadOutcome::Data) {
                    record({buffer.data(), read.bytes});
                    continue;
                }
                if (read.outcome != platform::TcpReadOutcome::Timeout) {
                    break;  // 对端先收了：没有可等的了。
                }
            }
            break;
        case PeerBehavior::ResetThenRedial: {
            // 发起序共 6 字节，只吞 4 字节：剩下 2 字节留在接收队列里 ⇒ close() 即 RST。
            const auto read = stream.read(std::span<std::uint8_t>{buffer.data(), 4U}, 2000);
            if (read.outcome == platform::TcpReadOutcome::Data) {
                record({buffer.data(), read.bytes});
            }
            break;
        }
        }
        stream.close();
    }

    /// @brief 一直排空并记录，直到用例收摊（析构置 `stop_`）；顺带把 `send_more` 的追加写送出去。
    auto pump(platform::TcpStream &stream) -> void {
        auto buffer = std::vector<std::uint8_t>(4096U, 0U);
        while (!stop_.load()) {
            for (auto extra = take_pending(); !extra.empty(); extra = take_pending()) {
                static_cast<void>(stream.write(extra));
            }
            const auto read = stream.read(std::span<std::uint8_t>{buffer}, 20);
            if (read.outcome == platform::TcpReadOutcome::Data) {
                record({buffer.data(), read.bytes});
                continue;  // 还有货就立刻再读，别把追加写压在一个空闲片之后。
            }
            if (read.outcome != platform::TcpReadOutcome::Timeout) {
                return;  // 对端已收摊或出错：这一条流没有可排的了。
            }
        }
    }

    PeerBehavior behavior_;
    std::vector<std::uint8_t> to_send_{};
    std::unique_ptr<platform::TcpListener> listener_{};
    std::thread worker_{};
    std::atomic<bool> stop_{false};
    int port_{0};
    std::mutex mutex_;
    std::size_t connections_{0};
    std::vector<std::uint8_t> received_{};
    std::vector<std::vector<std::uint8_t>> pending_{};
};

/// @brief 一条指向假服务端的档案。
[[nodiscard]] auto profile_for(int port) -> TelnetProfile {
    auto profile = TelnetProfile{};
    profile.host = "127.0.0.1";
    profile.port = port;
    return profile;
}

/// @brief 短退避档：用例不该为了等退避而睡一秒。
constexpr RetryPolicy kFastPolicy{.base_ms = 50, .cap_ms = 100, .max_attempts = 3};

/// @brief 连接事件记录的替身：把读线程的投递攒成可比对的快照（口径同串口那棒的替身）。
///
/// **声明顺序是硬约束**：替身必须比连接活得久——读线程在 `close()` 的 join 之前还会投
/// `on_closed`／进度，而连接持的是这里的裸指针。用例若把 `Recorder` 写在连接之后（析构
/// 先走），断言失败时的清理就会撞上已销毁的替身（实测：`pure virtual method called` 直接
/// 掀掉整个 runner，红线变成没有读数的崩溃）。
class Recorder final : public ConnectionEvents {
  public:
    auto on_bytes(std::span<const std::byte> bytes) -> void override {
        const std::lock_guard lock{mutex_};
        for (const auto byte : bytes) {
            received_.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
    }

    /// @brief 无参版：基类默认实现会把带归因的那条转调到这里，本替身只覆写归因版。
    auto on_closed() -> void override { record_close(CloseReason::Unknown); }

    auto on_closed(CloseReason reason) -> void override { record_close(reason); }

    auto on_reconnect_progress(const ReconnectProgress &progress) -> void override {
        const std::lock_guard lock{mutex_};
        progresses_.push_back(progress);
    }

    [[nodiscard]] auto received() -> std::vector<std::uint8_t> {
        const std::lock_guard lock{mutex_};
        return received_;
    }

    [[nodiscard]] auto received_text() -> std::string {
        auto out = std::string{};
        for (const auto byte : received()) {
            out.push_back(static_cast<char>(byte));
        }
        return out;
    }

    [[nodiscard]] auto closes() -> std::size_t {
        const std::lock_guard lock{mutex_};
        return reasons_.size();
    }

    [[nodiscard]] auto last_close_reason() -> CloseReason {
        const std::lock_guard lock{mutex_};
        return reasons_.empty() ? CloseReason::Unknown : reasons_.back();
    }

    [[nodiscard]] auto progresses() -> std::vector<ReconnectProgress> {
        const std::lock_guard lock{mutex_};
        return progresses_;
    }

    /// @brief 是否出现过「环里的一档」（attempt≥1 且未停）。
    [[nodiscard]] auto saw_redial() -> bool {
        for (const auto &progress : progresses()) {
            if (progress.attempt >= 1 && progress.stop == ReconnectStop::None) {
                return true;
            }
        }
        return false;
    }

    /// @brief 终态那一份（最后一投）。
    [[nodiscard]] auto last_progress() -> ReconnectProgress {
        const auto snapshots = progresses();
        return snapshots.empty() ? ReconnectProgress{} : snapshots.back();
    }

  private:
    auto record_close(CloseReason reason) -> void {
        const std::lock_guard lock{mutex_};
        reasons_.push_back(reason);
    }

    std::mutex mutex_;
    std::vector<std::uint8_t> received_{};
    std::vector<CloseReason> reasons_{};
    std::vector<ReconnectProgress> progresses_{};
};

/// @brief 轮询等待：读线程的动作是异步的，用例按 5 ms 步长查判定，到点即止。
template <typename Predicate>
[[nodiscard]] auto wait_for(Predicate &&predicate, int timeout_ms) -> bool {
    for (int waited = 0; waited < timeout_ms; waited += 5) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return predicate();
}

}  // namespace

AURORA_TEST_CASE(telnet_close_reason_table_is_the_single_mapping) {
    // 判据 7 的三档 + 本端收口：每档都对应一条重拨语义，表只有一份。
    AURORA_TEST_CHECK(conn::telnet_close_reason(TelnetLinkEnd::DialFailed) ==
                      CloseReason::DialNetwork);
    AURORA_TEST_CHECK(conn::telnet_close_reason(TelnetLinkEnd::RemoteEof) ==
                      CloseReason::RemoteExit);
    AURORA_TEST_CHECK(conn::telnet_close_reason(TelnetLinkEnd::ReadError) == CloseReason::LinkLost);
    AURORA_TEST_CHECK(conn::telnet_close_reason(TelnetLinkEnd::LocalClose) ==
                      CloseReason::LocalClose);

    // 裁决 7.112 D9① 的那条分野在分类表上的落点：Telnet 的 EOF 不重拨（串口那棒重拨）。
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(CloseReason::RemoteExit));
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(CloseReason::LinkLost));
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(CloseReason::DialNetwork));
}

AURORA_TEST_CASE(opening_sequence_goes_out_and_net_text_comes_in_without_command_bytes) {
    LoopbackPeer peer{PeerBehavior::StayOpen,
                      bytes_of("Welcome:")};  // 冒号后面故意不跟 IAC，只证净文本方向
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};

    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.received_text() == "Welcome:"; }, 3000),
                            recorder.received_text());
    // 判据 3：发起序逐字节到达对端（同一条 socket 的反方向）。
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(kOpening); }, 3000),
                            hex_of(peer.received()));
    // 判据 5 的现场版：净数据里一个命令字节都不该出现。
    for (const auto byte : recorder.received()) {
        AURORA_TEST_CHECK_NE(byte, 0xFBU);
        AURORA_TEST_CHECK_NE(byte, 0xFDU);
        AURORA_TEST_CHECK_NE(byte, 0xFAU);
    }
    connection.close();
}

AURORA_TEST_CASE(a_split_command_answers_once_and_the_bytes_after_it_stay_data) {
    // F6 的现场版：`IAC DO ECHO` 的前两字节与第三字节分两次写到达，中间夹着一次完整的重问。
    LoopbackPeer peer{PeerBehavior::StayOpen, {0xFFU, 0xFDU}};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.accepted(); }, 3000), "peer never accepted");
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 3000),
                            "leg never came up");

    // 半条命令停在解析器里：那两字节既不进网格，也换不来应答（判据 5）。
    AURORA_TEST_CHECK_TRUE(recorder.received().empty());
    // 补齐第三个字节（同一条流上的下一次写）⇒ 命令这才成立。
    peer.send_more({0x01U});
    // 应答表的拒绝档：对端 `DO ECHO` ⇒ 我方 `WONT ECHO`（D7①「不做本地回显」在线上唯一可见的那一格）。
    const auto wont_echo = std::vector<std::uint8_t>{0xFFU, 0xFCU, 0x01U};
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(wont_echo); }, 3000),
                            hex_of(peer.received()));

    // 对端带重试逻辑地再问一遍，且命令后面就跟着数据字节（判据 5 的另一半：单命令不吞下一字节）。
    peer.send_more({0xFFU, 0xFDU, 0x01U, 'A'});
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.received_text() == "A"; }, 3000),
                            recorder.received_text());
    // 口径 ⑵／D4①：同一立场只答一次——重复问不得再答，否则对端重试会形成应答风暴。
    AURORA_TEST_CHECK_EQ(peer.received_count(wont_echo), 1U);
    // 命令段与载荷字节绝不进网格：净数据里只剩那个 'A'。
    for (const auto byte : recorder.received()) {
        AURORA_TEST_CHECK_NE(byte, 0xFFU);
        AURORA_TEST_CHECK_NE(byte, 0xFDU);
        AURORA_TEST_CHECK_NE(byte, 0xFCU);
    }
    connection.close();
}

AURORA_TEST_CASE(asks_for_ttype_gets_will_ttype_then_the_terminal_name) {
    LoopbackPeer peer{PeerBehavior::StayOpen,
                      {0xFFU, 0xFDU, 0x18U}};  // IAC DO TTYPE
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);

    // 判据 4：`WILL TTYPE` 紧跟 `SB TTYPE IS "XTERM" SE`（14 字节，一次写出）。
    const auto expected = std::vector<std::uint8_t>{0xFFU, 0xFBU, 0x18U, 0xFFU, 0xFAU, 0x18U, 0x00U,
                                                    'X',   'T',    'E',  'R',   'M',  0xFFU, 0xF0U};
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(expected); }, 3000),
                            hex_of(peer.received()));
    connection.close();
}

AURORA_TEST_CASE(the_size_slot_carries_one_naws_per_change_and_width_comes_first) {
    LoopbackPeer peer{PeerBehavior::StayOpen};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 3000), "leg never came up");

    // 判据 8：100 列 × 40 行 ⇒ `FF FA 1F 00 64 00 28 FF F0`（宽在前，F14 的实测次序）。
    const auto first = std::vector<std::uint8_t>{0xFFU, 0xFAU, 0x1FU, 0x00U, 0x64U, 0x00U, 0x28U,
                                                 0xFFU, 0xF0U};
    connection.resize(Size{.columns = 100, .rows = 40});
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(first); }, 3000),
                            hex_of(peer.received()));

    // 同值去重（裁决 7.112 D10② 的代价 ⓑ：拖分屏不得形成协商风暴）：再给一次同样的尺寸不得再上线。
    const auto bytes_after_first = peer.received().size();
    connection.resize(Size{.columns = 100, .rows = 40});
    // 给足两轮读分片（20 ms 片 + 200 ms 判据），去重若失效这里就会多出 9 字节。
    AURORA_TEST_CHECK_FALSE(wait_for([&] { return peer.received().size() > bytes_after_first; }, 200));
    AURORA_TEST_CHECK_EQ(peer.received().size(), bytes_after_first);

    // 变了才发（比对源是「上一次真的发出去的」，裁决 7.113④）：换个尺寸就得再上一回线。
    const auto second = std::vector<std::uint8_t>{0xFFU, 0xFAU, 0x1FU, 0x00U, 0x50U, 0x00U, 0x18U,
                                                  0xFFU, 0xF0U};  // 80 列 × 24 行
    connection.resize(Size{.columns = 80, .rows = 24});
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(second); }, 3000),
                            hex_of(peer.received()));
    connection.close();
}

AURORA_TEST_CASE(resize_before_the_link_is_up_costs_no_io_and_goes_out_after) {
    // 判据 8 的「建立后首发一次当前尺寸」：尺寸在连上之前就有了，也得在连上那一轮出去。
    auto listener = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_REQUIRE_TRUE(listener != nullptr);
    if (listener == nullptr) {
        return;
    }
    Recorder recorder;
    TelnetConnection connection{profile_for(listener->bound_port()), kFastPolicy};
    connection.resize(Size{.columns = 120, .rows = 30});  // UI 线程在拨号前就给了尺寸
    connection.start(recorder);

    auto stream = listener->accept(3000);
    AURORA_TEST_REQUIRE_TRUE(stream != nullptr);
    if (stream == nullptr) {
        return;
    }
    const auto expected = std::vector<std::uint8_t>{0xFFU, 0xFAU, 0x1FU, 0x00U, 0x78U, 0x00U, 0x1EU,
                                                    0xFFU, 0xF0U};
    auto seen = std::vector<std::uint8_t>{};
    auto buffer = std::vector<std::uint8_t>(64U, 0U);
    const auto found = wait_for(
        [&] {
            const auto read = stream->read(std::span<std::uint8_t>{buffer}, 20);
            if (read.outcome == platform::TcpReadOutcome::Data) {
                seen.insert(seen.end(), buffer.begin(),
                            buffer.begin() + static_cast<std::ptrdiff_t>(read.bytes));
            }
            return std::search(seen.begin(), seen.end(), expected.begin(), expected.end()) !=
                   seen.end();
        },
        3000);
    AURORA_TEST_CHECK_MSG(found, hex_of(seen));
    stream->close();
    connection.close();
}

AURORA_TEST_CASE(outbound_0xff_is_doubled_on_the_wire_and_nothing_else_changes) {
    LoopbackPeer peer{PeerBehavior::StayOpen};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 3000), "leg never came up");

    // F7 的发送半：`41 FF 42` 上线成 `41 FF FF 42`（转义在连接腿，不在会话层）。
    const auto payload = std::vector<std::uint8_t>{0x41U, 0xFFU, 0x42U};
    connection.write({reinterpret_cast<const std::byte *>(payload.data()), payload.size()});
    const auto expected = std::vector<std::uint8_t>{0x41U, 0xFFU, 0xFFU, 0x42U};
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.received_contains(expected); }, 3000),
                            hex_of(peer.received()));
    // 转义只动 0xFF 这一格：发起序那 6 字节仍是原样，一处不多一处不少。
    AURORA_TEST_CHECK_EQ(peer.received_count(kOpening), 1U);
    connection.close();
}

AURORA_TEST_CASE(orderly_eof_is_remote_exit_and_never_enters_the_backoff) {
    LoopbackPeer peer{PeerBehavior::CleanClose};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 3000), "no on_closed");

    // 判据 7 第一档：EOF ⇒ RemoteExit，且**不**进重拨环（D9① 与串口那棒的分野）。
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::RemoteExit);
    AURORA_TEST_CHECK_FALSE(recorder.saw_redial());
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::RemoteExit);
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);  // 只投一次，不是一边重拨一边投
    AURORA_TEST_CHECK_FALSE(connection.alive());
    connection.close();
}

AURORA_TEST_CASE(a_reset_is_link_lost_and_the_backoff_redials_the_same_profile) {
    LoopbackPeer peer{PeerBehavior::ResetThenRedial};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);

    // 判据 7 第二档的现场：对端只吞 4 字节就关 ⇒ 接收队列里留着未读字节 ⇒ 内核发 RST（实测 F3 的
    // 另一态），本腿一律从读侧把它判成 LinkLost（可重拨）。监听点没换，故第二回受理就是
    // 「按同一份档案再拨一次、而且真连上了」的直接证人。
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return peer.connection_count() >= 2U; }, 3000),
                            "the leg never redialed the surrogate");
    AURORA_TEST_CHECK_MSG(recorder.saw_redial(), "the backoff loop never posted a ring snapshot");
    AURORA_TEST_CHECK_MSG(wait_for([&] { return connection.alive(); }, 1000),
                          "still down after the redial");
    // 中途掉线不投 on_closed：会话不闪断（口径同 SSH 与串口两腿）。
    AURORA_TEST_CHECK_EQ(recorder.closes(), 0U);

    connection.close();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 1000), "no on_closed");
    // 裁决 7.112 D9① 的分野：RST 不是 EOF——终态由本端收口给出，绝不是「远端退出」。
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
    AURORA_TEST_CHECK_NE(recorder.last_progress().stop, ReconnectStop::RemoteExit);
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);  // 环走完才投这一份，不是一边重拨一边投
}

AURORA_TEST_CASE(stop_reconnect_ends_a_waiting_backoff_on_the_spot) {
    LoopbackPeer peer{PeerBehavior::ResetThenRedial};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    // 首回落回 1 s 的退避档：正因为它长，「停止重连」把环截断才是可观测的。
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()),
                                RetryPolicy{.base_ms = 1000, .cap_ms = 2000, .max_attempts = 3}};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.saw_redial(); }, 3000),
                            "the backoff loop never started");

    const auto start = std::chrono::steady_clock::now();
    connection.stop_reconnect();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 3000), "no on_closed");
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    // 终态是「用户停止」而非「次数用尽」，归因仍是掉线那回的 LinkLost（没被重开的读数覆盖）。
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LinkLost);
    // 截断生效：远早于那 1 s 的退避（分片 200 ms，故判据给 900 ms）。
    AURORA_TEST_CHECK_LT(elapsed, 900);
    connection.close();
}

AURORA_TEST_CASE(an_unreachable_port_is_dial_network_and_the_session_never_looks_alive) {
    auto probe = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_REQUIRE_TRUE(probe != nullptr);
    if (probe == nullptr) {
        return;
    }
    const int port = probe->bound_port();
    probe->close();  // 端口刚释放 ⇒ connect 立刻被拒（实测 F9 的 errno 111 形态）

    Recorder recorder;
    TelnetConnection connection{profile_for(port), RetryPolicy{.base_ms = 50,
                                                              .cap_ms = 100,
                                                              .max_attempts = 2}};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 3000), "no on_closed");

    // 判据 7 的建连档：归 DialNetwork（可重拨），且本端从不假装会话已建立。
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::DialNetwork);
    AURORA_TEST_CHECK_FALSE(connection.alive());
    AURORA_TEST_CHECK_MSG(recorder.saw_redial(), "a refused dial should be redialed");
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);
    connection.close();
}

AURORA_TEST_CASE(local_close_ends_the_ring_and_reports_once) {
    LoopbackPeer peer{PeerBehavior::StayOpen};
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    Recorder recorder;
    TelnetConnection connection{profile_for(peer.port()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 3000), "leg never came up");

    connection.close();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 1000), "no on_closed");
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
    AURORA_TEST_CHECK_FALSE(connection.alive());
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK_FALSE(recorder.saw_redial());
    // 关掉之后再写：丢弃而不是崩（未连上含退避期间同语义）。
    const auto late = bytes_of("late");
    connection.write({reinterpret_cast<const std::byte *>(late.data()), late.size()});
    AURORA_TEST_CHECK_FALSE(peer.received_contains(bytes_of("late")));
}

AURORA_TEST_CASE(a_telnet_session_decodes_utf8_and_lands_the_chosen_line_ending) {
    LoopbackPeer peer{PeerBehavior::StayOpen, {0xE4U, 0xB8U, 0xADU}};  // GB 之外：UTF-8 的「中」
    AURORA_TEST_REQUIRE_MSG(peer.ok(), "loopback listener unavailable");
    auto owned = std::make_unique<TelnetConnection>(profile_for(peer.port()), kFastPolicy);
    auto *connection = owned.get();
    // Telnet 会话的两条构造入参（裁决 7.112 D5①／D8①）：编码吃全局档（缺省 UTF-8）、行尾 CRLF。
    auto session = std::make_unique<Session>(std::move(owned), Size{10, 3}, 5, width_policy,
                                             TerminalDefaults{}, SessionEncoding{.name = "UTF-8"},
                                             std::optional<LineEnding>{LineEnding::Crlf});
    session->start();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection->alive(); }, 3000),
                            "telnet leg never came up on the surrogate");
    char32_t landed = 0U;
    AURORA_TEST_REQUIRE_MSG(wait_for(
                                [&] {
                                    session->read([&landed](Storage &grid, Cursor, const TermModes &) {
                                        landed = grid.visible_line(0U).cell(0U).code_point;
                                    });
                                    return landed == 0x4E2DU;
                                },
                                3000),
                            std::to_string(static_cast<unsigned>(landed)));

    // 写方向的两段事实（D8①）：键表产的仍是一个 CR 字节，而 Telnet 会话落地的是 CRLF。
    const auto cr = std::string{"\r"};
    session->send_bytes({reinterpret_cast<const std::byte *>(cr.data()), cr.size()});
    AURORA_TEST_REQUIRE_MSG(
        wait_for([&] { return peer.received_contains(bytes_of("\r\n")); }, 3000),
        hex_of(peer.received()));

    session.reset();  // 会话析构即断开连接（读线程回调持着会话指针，不得比它活得久）
}

}  // namespace borealis::test_cases::itest_telnet_connection
