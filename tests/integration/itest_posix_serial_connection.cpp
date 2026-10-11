/// 测试类型: integration
/// 目标单元: src/conn/serial_connection.{h,cpp}（串口连接腿，POSIX 腿）+ src/session/session.cpp
///           的行尾折算出口
/// 测试说明: `SPEC.FEAT.CONN.05` 串口连接腿的替身设备证人（裁决 7.110 D13② 的第二层）。
///           设备取 `openpty()` 的 slave **路径**（与 `utest_posix_serial_platform` 同一形态），
///           证的是**调用序与回读**：打开即按档案置参 → 双向字节 → 热改回读相符才更新「当前档」
///           → 对端消失按 D7① 落 `LinkLost` 并进既有退避环 → 本端 `close()` 落 `LocalClose`。
///           断言面按实测边界收窄：帧格式（F5 里替身设备连 CS7 都拒）与调制解调器线（F7 的
///           ENOTTY）**不判**，真设备那一腿登记为 `etest_serial_device`（F1 在本沙箱挡着）。
///           最后一条用例把会话接上同一台替身设备，一次证两件事：GB18030 的串口会话读方向
///           解码（D10）与「键表产 CR ∧ 串口落地 CRLF」的两段事实（D11①）。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按文件名把平台专属套件挡在
///           另一侧的构建面之外）；Win32 腿待真机（F10，AGENTS.md §4.2 第 11 条标推断）。

#include <pty.h>      // openpty（glibc，链接 util）
#include <unistd.h>   // ttyname, read, write, close

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
// 私有头（裁决 7.110 D1① 的先例）：连接腿与其归因表都在 src/ 树内。
#include "conn/serial_connection.h"

namespace borealis::test_cases::itest_posix_serial_connection {

namespace {

using borealis::conn::RetryPolicy;
using borealis::conn::SerialConnection;
using borealis::conn::SerialProfile;
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

/// @brief 一对 pty：master 是「对端」，slave 路径是替身设备（同 utest_posix_serial_platform）。
class SurrogateDevice {
  public:
    SurrogateDevice() {
        ok_ = ::openpty(&master_, &slave_, nullptr, nullptr, nullptr) == 0;
        if (ok_) {
            const auto *name = ::ttyname(slave_);
            if (name != nullptr) {
                path_ = name;
            }
        }
    }
    ~SurrogateDevice() {
        if (master_ >= 0) {
            ::close(master_);
        }
        if (slave_ >= 0) {
            ::close(slave_);
        }
    }

    SurrogateDevice(const SurrogateDevice &) = delete;
    auto operator=(const SurrogateDevice &) -> SurrogateDevice & = delete;

    [[nodiscard]] auto ok() const -> bool { return ok_ && !path_.empty(); }
    [[nodiscard]] auto path() const -> const std::string & { return path_; }

    /// @brief 关掉写端（拔线形态）：句柄号随即归还，析构不再重复关。
    auto close_peer() -> void {
        if (master_ >= 0) {
            ::close(master_);
            master_ = -1;
        }
    }

    /// @brief 从 master 侧读一段（取连接写出去的字节）。
    [[nodiscard]] auto read_from_peer(std::span<std::uint8_t> out) -> std::size_t {
        const auto got = ::read(master_, out.data(), out.size());
        return got > 0 ? static_cast<std::size_t>(got) : 0U;
    }

    /// @brief 往 master 侧写字节（对端发来的字节）。
    auto write_to_peer(std::span<const std::uint8_t> bytes) -> void {
        static_cast<void>(::write(master_, bytes.data(), bytes.size()));
    }

  private:
    int master_{-1};
    int slave_{-1};
    bool ok_{false};
    std::string path_{};
};

/// @brief 一条指向替身设备的缺省档案（8N1、无流控、LF；速率由用例改写）。
[[nodiscard]] auto profile_for(std::string_view path, int baud = 115200) -> SerialProfile {
    auto profile = SerialProfile{};
    profile.device_path = std::string{path};
    profile.baud = baud;
    return profile;
}

/// @brief 短退避档：用例不该为了等退避而睡一秒。
constexpr RetryPolicy kFastPolicy{.base_ms = 50, .cap_ms = 100, .max_attempts = 3};

/// @brief 连接事件记录的替身：把读线程的投递攒成可比对的快照。
///
/// 读线程与用例线程并发，故接收侧全部经 `mutex_`；断言一律取副本而非引用。
/// **声明顺序是硬约束**：替身要排在连接之前，替身才比连接活得久——读线程在 `close()` 的
/// join 之前还会投 `on_closed`／进度，而连接持的是这里的裸指针。顺序倒了不会让通过的用例
/// 出问题，只会让**失败**的用例在清理时撞上已销毁的替身（实测 `pure virtual method called`
/// 掀掉整个 runner，红线变成没有读数的崩溃）。
class Recorder final : public ConnectionEvents {
  public:
    auto on_bytes(std::span<const std::byte> bytes) -> void override {
        const std::lock_guard lock{mutex_};
        for (const auto byte : bytes) {
            received_.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
        }
    }

    /// @brief 无参版：基类默认实现会把带归因的那条转调到这里，本替身只覆写归因版。
    auto on_closed() -> void override { record_close(CloseReason::Unknown); }

    auto on_closed(CloseReason reason) -> void override { record_close(reason); }

    auto on_reconnect_progress(const ReconnectProgress &progress) -> void override {
        const std::lock_guard lock{mutex_};
        progresses_.push_back(progress);
    }

    [[nodiscard]] auto received() -> std::string {
        const std::lock_guard lock{mutex_};
        return received_;
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
    std::string received_;
    std::vector<CloseReason> reasons_;
    std::vector<ReconnectProgress> progresses_;
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

[[nodiscard]] auto to_bytes(std::string_view text) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(serial_close_reason_table_is_the_single_mapping) {
    using borealis::platform::SerialFailure;

    // 事实类失败（重拨救不回来）一律落 Unknown，按裁决 7.99 D2① 的保守档不重拨。
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::Denied) == CloseReason::Unknown);
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::NoNode) == CloseReason::Unknown);
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::Io) == CloseReason::Unknown);
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::ConfigRejected) == CloseReason::Unknown);
    // 状态类失败（下一回可能就没了）落可重拨的 DialNetwork。
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::Hangup) == CloseReason::DialNetwork);
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::Busy) == CloseReason::DialNetwork);
    // 成功档不该出现在归因现场：保守落 Unknown。
    AURORA_TEST_CHECK(conn::serial_close_reason(SerialFailure::None) == CloseReason::Unknown);

    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(CloseReason::Unknown));
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(CloseReason::DialNetwork));
    // D7① 第 1 条代价口径：串口的 EOF 归 LinkLost（可重拨），而 RemoteExit 在本腿永不出现。
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(CloseReason::LinkLost));
}

AURORA_TEST_CASE(surrogate_link_pumps_bytes_both_ways_and_local_close_is_attributed) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    Recorder recorder;
    SerialConnection connection{profile_for(device.path()), kFastPolicy};

    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 2000),
                            "serial leg never came up on the surrogate");
    // 打开即按档案置参：当前档＝档案档（D6② 的回读判据在平台腿，本处只证传下来的那一份）。
    // 这一问要取 port_mutex_，读线程空闲时只可能握着一个非阻塞探测，故它不该有可观测的等待。
    AURORA_TEST_CHECK_EQ(connection.current_baud(), 115200);

    // 读方向：对端写的字节经读线程投进会话侧。
    device.write_to_peer(to_bytes("ping"));
    AURORA_TEST_CHECK_MSG(wait_for([&] { return recorder.received() == "ping"; }, 2000),
                          recorder.received());

    // 写方向：连接写出的字节原样到对端（F8：设备规程不改写，行尾折算不属本腿）。
    const auto outgoing = to_bytes("pong");
    connection.write({reinterpret_cast<const std::byte *>(outgoing.data()), outgoing.size()});
    std::vector<std::uint8_t> seen(8U, 0U);
    AURORA_TEST_REQUIRE_EQ(device.read_from_peer(std::span<std::uint8_t>{seen}), outgoing.size());
    AURORA_TEST_CHECK_TRUE(std::equal(outgoing.begin(), outgoing.end(), seen.begin()));

    connection.close();
    AURORA_TEST_CHECK_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 1000), "no on_closed");
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
    AURORA_TEST_CHECK_FALSE(connection.alive());
    // 本端主动关闭不该进重拨环：终态档位是「用户停止」，且没有过 attempt≥1 的那一档。
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK_FALSE(recorder.saw_redial());
}

AURORA_TEST_CASE(hot_baud_change_only_updates_when_the_readback_agrees) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    Recorder recorder;
    SerialConnection connection{profile_for(device.path(), 9600), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 2000),
                            "serial leg never came up on the surrogate");

    // F4 在连接腿的现场：相符才更新「会话当前波特率」。
    AURORA_TEST_CHECK_TRUE(connection.set_baud(115200));
    AURORA_TEST_CHECK_EQ(connection.current_baud(), 115200);

    // D6② 的拒绝档：无 termios 码的速率回读不符，界面保持原档、当前值不能被改脏。
    AURORA_TEST_CHECK_FALSE(connection.set_baud(12345));
    AURORA_TEST_CHECK_EQ(connection.current_baud(), 115200);

    connection.close();
    // 没连着就谈不上热改（退避期间与关闭后都回 false，调用侧据此保持原档）。
    AURORA_TEST_CHECK_FALSE(connection.set_baud(19200));
}

AURORA_TEST_CASE(peer_gone_is_link_lost_and_the_loop_redials_from_the_profile) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    Recorder recorder;
    SerialConnection connection{profile_for(device.path()), kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 2000),
                            "serial leg never came up on the surrogate");

    // F9：替身设备的掉线形态是 EOF。D7① 把它归到 LinkLost（而非 RemoteExit），于是环要重拨。
    const auto start = std::chrono::steady_clock::now();
    device.close_peer();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 3000), "no on_closed");
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    // 证「按档案重开」这一步真发生过：投过一份 attempt≥1 的环内快照（首拨成功后红ials 归零，
    // 故这一份只能是掉线之后那一回重拨投的）。
    AURORA_TEST_CHECK_MSG(recorder.saw_redial(), "the backoff loop never redialed");
    // 重开的是同一个已消失的节点，故终态由那次打开的失败归因决定：本沙箱实测（探针）pts 节点随
    // master 关闭而消失 ⇒ NoNode ⇒ Unknown ⇒ 不可重拨；换一台节点不消失的机器就按档重拨到用尽。
    // 两种都是可接受的终态，共同点是**只投一次 on_closed**，且都不是「用户停止」。
    const auto stop = recorder.last_progress().stop;
    AURORA_TEST_CHECK_MSG(stop == ReconnectStop::NotReconnectable ||
                          stop == ReconnectStop::AttemptsExhausted,
                          std::to_string(static_cast<int>(stop)));
    AURORA_TEST_CHECK_NE(stop, ReconnectStop::UserStopped);
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);
    AURORA_TEST_CHECK_FALSE(connection.alive());
    // 掉线归因不能等满退避预算：退避上限 100 ms、最多 3 回，收口应在秒级以内。
    AURORA_TEST_CHECK_LT(elapsed, 2000);
    connection.close();
}

AURORA_TEST_CASE(an_unopenable_device_never_enters_the_backoff) {
    Recorder recorder;
    SerialConnection connection{profile_for("/definitely/not/a/device"), RetryPolicy{.base_ms = 50,
                                                                                     .cap_ms = 100,
                                                                                     .max_attempts = 5}};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 2000), "no on_closed");

    // D7① 第 2 条代价口径：节点根本不存在属事实类失败，落 Unknown 即不进重拨环。
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::Unknown);
    AURORA_TEST_CHECK_EQ(recorder.progresses().size(), 1U);  // 只有终态那一份
    AURORA_TEST_CHECK_EQ(recorder.last_progress().attempt, 0);
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::NotReconnectable);
    AURORA_TEST_CHECK_EQ(recorder.closes(), 1U);
    connection.close();
}

AURORA_TEST_CASE(stop_reconnect_ends_a_waiting_backoff_on_the_spot) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    // 首回落回缺省档的 1 s 退避：正因为它长，「停止重连」把环截断才是可观测的。
    Recorder recorder;
    SerialConnection connection{profile_for(device.path()),
                                RetryPolicy{.base_ms = 1000, .cap_ms = 2000, .max_attempts = 3}};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection.alive(); }, 2000),
                            "serial leg never came up on the surrogate");

    device.close_peer();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.saw_redial(); }, 3000),
                            "the backoff loop never started");
    const auto start = std::chrono::steady_clock::now();
    connection.stop_reconnect();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return recorder.closes() >= 1U; }, 3000), "no on_closed");
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    // 终态是「用户停止」而非「次数用尽」，且这一归因就是掉线那回的 LinkLost（没被重开覆盖）。
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LinkLost);
    // 截断生效：远早于那 1 s 的退避（分片 200 ms，故给 900 ms 的判据）。
    AURORA_TEST_CHECK_LT(elapsed, 900);
    connection.close();
}

AURORA_TEST_CASE(a_serial_session_decodes_gb18030_and_lands_the_chosen_line_ending) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    auto owned = std::make_unique<SerialConnection>(profile_for(device.path()), kFastPolicy);
    auto *connection = owned.get();
    // 串口会话的两条构造入参（裁决 7.110 D10/D11①）：编码 GB18030、发送行尾 CRLF。
    auto session = std::make_unique<Session>(std::move(owned), Size{10, 3}, 5, width_policy,
                                             TerminalDefaults{},
                                             SessionEncoding{.name = "GB18030"},
                                             std::optional<LineEnding>{LineEnding::Crlf});
    session->start();
    AURORA_TEST_REQUIRE_MSG(wait_for([&] { return connection->alive(); }, 2000),
                            "serial leg never came up on the surrogate");

    // 读方向：对端发来 GB18030 的「中」（0xD6 0xD0），网格第 0 格应落成 U+4E2D。
    device.write_to_peer(std::vector<std::uint8_t>{0xD6U, 0xD0U});
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

    // 写方向的两段事实（D11①）：键表产的仍是一个 CR 字节，而串口会话落地的是 CRLF。
    // 这里直接喂 `send_bytes` 的那一个 CR，即 `term::encode_key` 对 Enter 的产出形态
    // （键表面由 `utest_keymap` 逐字节锁死，本处锁的是「会话写出侧折了一次」这半段）。
    const auto cr = std::string{"\r"};
    std::vector<std::uint8_t> seen(4U, 0U);
    session->send_bytes({reinterpret_cast<const std::byte *>(cr.data()), cr.size()});
    AURORA_TEST_REQUIRE_EQ(device.read_from_peer(std::span<std::uint8_t>{seen}), 2U);
    AURORA_TEST_CHECK_EQ(seen[0], static_cast<std::uint8_t>('\r'));
    AURORA_TEST_CHECK_EQ(seen[1], static_cast<std::uint8_t>('\n'));

    session.reset();  // 会话析构即断开连接（读线程回调持着会话指针，不得比它活得久）
}

}  // namespace borealis::test_cases::itest_posix_serial_connection
