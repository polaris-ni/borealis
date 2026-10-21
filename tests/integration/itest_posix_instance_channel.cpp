/// 测试类型: integration
/// 目标单元: src/platform/instance_channel.h + src/platform/posix/instance_channel.cpp
/// 测试说明: `SPEC.FEAT.INTEG.04` 单实例通道的 POSIX 腿。判据落在三条真行为上：
///           ① 认领权威是锁文件——第二个进程拿 `Existing`，首实例析构后名字可立刻复用；
///           ② 转交帧往返与串行处理——handler 在 transport 线程上收帧，回执原样送回；
///           ③ 失败面不猜——空键、缺实例、超限帧与 0755 目录都在本地拒绝。
///           另证崩溃残留：socket 路径上留一个普通文件时，首实例仍能认领并覆盖。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按文件名把平台专属套件
///           挡在另一侧）；Win32 腿是命名管道，待真机（AGENTS.md §4.2 第 11 条标推断）。

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "framework/aurora_test.h"
// 私有头：平台通道接口在 src/ 树内（测试目标已带 src/ 的 include 路径）。
#include "platform/instance_channel.h"

namespace borealis::test_cases::itest_posix_instance_channel {

namespace {

using borealis::platform::instance_forward;
using borealis::platform::instance_listen;
using borealis::platform::InstanceRequest;
using borealis::platform::InstanceRole;

/// @brief 一次用例独占的私有目录；析构把 socket、锁与目录一并清掉。
///
/// 目录本身交给被测代码创建（它会 chmod 0700）；只有用例需要预置残骸时才自己建。
class TempDir {
  public:
    TempDir() {
        static std::atomic<unsigned> sequence{0};
        // 不用 `temp_directory_path()`：测试隔离层给的路径很长，加上文件名会越过
        // `sun_path` 的 108 字节上限；生产键另有长度回落（`instance_default_key`）。
        const std::string name = "borealis-itest-" + std::to_string(::getpid()) + "-" +
                                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        path_ = std::filesystem::path{"/tmp"} / name;
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir &) = delete;
    auto operator=(const TempDir &) -> TempDir & = delete;
    TempDir(TempDir &&) = delete;
    auto operator=(TempDir &&) -> TempDir & = delete;

    [[nodiscard]] auto path() const -> const std::filesystem::path & { return path_; }
    [[nodiscard]] auto key() const -> std::string { return (path_ / "instance.sock").string(); }

    /// @brief 预建目录并设成指定权限（残骸/权限用例用）。
    auto make_with_mode(std::filesystem::perms permissions) -> void {
        std::error_code ec;
        std::filesystem::create_directories(path_, ec);
        std::filesystem::permissions(path_, permissions, std::filesystem::perm_options::replace, ec);
    }

  private:
    std::filesystem::path path_;
};

/// @brief 有界等待：轮询到条件成立或 3 秒到期。
auto wait_until(const std::function<bool()> &predicate) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

}  // namespace

AURORA_TEST_CASE(second_claim_reports_existing_and_release_frees_the_name) {
    TempDir dir;
    auto first = instance_listen(dir.key(), {});
    AURORA_TEST_CHECK_MSG(first.role == InstanceRole::Primary, first.error);
    AURORA_TEST_CHECK(first.listener != nullptr);

    auto second = instance_listen(dir.key(), {});
    AURORA_TEST_CHECK(second.role == InstanceRole::Existing);
    AURORA_TEST_CHECK(second.listener == nullptr);

    first.listener.reset();

    auto third = instance_listen(dir.key(), {});
    AURORA_TEST_CHECK(third.role == InstanceRole::Primary);
    AURORA_TEST_CHECK(third.listener != nullptr);
}

AURORA_TEST_CASE(forward_round_trip_and_sequential_requests) {
    TempDir dir;
    std::mutex mutex;
    std::vector<std::string> received;

    auto listener = instance_listen(
        dir.key(), [&mutex, &received](std::shared_ptr<InstanceRequest> request) -> void {
            {
                const std::lock_guard lock{mutex};
                received.push_back(request->frame());
            }
            request->complete("ack:" + request->frame());
        });
    AURORA_TEST_CHECK(listener.role == InstanceRole::Primary);

    const auto first = instance_forward(dir.key(), "one", 2000);
    AURORA_TEST_CHECK(first.delivered);
    AURORA_TEST_CHECK(first.reply == "ack:one");

    const std::string large_payload(1024, 'x');
    const auto second = instance_forward(dir.key(), large_payload, 2000);
    AURORA_TEST_CHECK(second.delivered);
    AURORA_TEST_CHECK(second.reply == "ack:" + large_payload);

    AURORA_TEST_CHECK(wait_until([&received]() -> bool { return received.size() == 2; }));
    {
        const std::lock_guard lock{mutex};
        AURORA_TEST_CHECK(received.size() == 2);
        if (received.size() == 2) {
            AURORA_TEST_CHECK(received[0] == "one");
            AURORA_TEST_CHECK(received[1] == large_payload);
        }
    }
}

AURORA_TEST_CASE(oversized_frame_is_refused_before_the_wire) {
    TempDir dir;
    std::atomic<int> calls{0};
    auto listener = instance_listen(dir.key(), [&calls](std::shared_ptr<InstanceRequest> request) -> void {
        calls.fetch_add(1, std::memory_order_relaxed);
        request->complete("x");
    });
    AURORA_TEST_CHECK(listener.role == InstanceRole::Primary);

    const std::string huge(borealis::platform::kMaxInstanceFrameBytes + 1, 'x');
    const auto result = instance_forward(dir.key(), huge, 1000);
    AURORA_TEST_CHECK(!result.delivered);
    AURORA_TEST_CHECK(!result.error.empty());
    AURORA_TEST_CHECK(calls.load(std::memory_order_relaxed) == 0);
}

AURORA_TEST_CASE(stale_socket_file_is_replaced) {
    TempDir dir;
    dir.make_with_mode(std::filesystem::perms::owner_all);
    {
        std::ofstream stale(dir.key(), std::ios::binary);
        stale << "stale";
    }
    AURORA_TEST_CHECK(std::filesystem::exists(dir.key()));

    auto listener = instance_listen(dir.key(), {});
    AURORA_TEST_CHECK(listener.role == InstanceRole::Primary);
    AURORA_TEST_CHECK(listener.listener != nullptr);
}

AURORA_TEST_CASE(non_private_directory_is_refused) {
    TempDir dir;
    dir.make_with_mode(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                       std::filesystem::perms::others_read);

    auto listener = instance_listen(dir.key(), {});
    AURORA_TEST_CHECK(listener.role == InstanceRole::Failed);
    AURORA_TEST_CHECK(!listener.error.empty());
    AURORA_TEST_CHECK(listener.listener == nullptr);
}

AURORA_TEST_CASE(forward_to_missing_instance_reports_an_error) {
    TempDir dir;
    dir.make_with_mode(std::filesystem::perms::owner_all);

    const auto result = instance_forward(dir.key(), "ping", 200);
    AURORA_TEST_CHECK(!result.delivered);
    AURORA_TEST_CHECK(!result.error.empty());
}

AURORA_TEST_CASE(empty_key_is_refused) {
    const auto listen = instance_listen("", {});
    AURORA_TEST_CHECK(listen.role == InstanceRole::Failed);
    AURORA_TEST_CHECK(!listen.error.empty());

    const auto forward = instance_forward("", "ping", 100);
    AURORA_TEST_CHECK(!forward.delivered);
    AURORA_TEST_CHECK(!forward.error.empty());
}

AURORA_TEST_CASE(request_keeps_the_first_reply_and_times_out) {
    InstanceRequest request{"question"};
    request.complete("first");
    request.complete("second");
    const auto reply = request.wait(0);
    AURORA_TEST_CHECK(reply.has_value());
    if (reply.has_value()) {
        AURORA_TEST_CHECK(*reply == "first");
    }

    InstanceRequest unanswered{"question"};
    const auto timeout = unanswered.wait(20);
    AURORA_TEST_CHECK(!timeout.has_value());
}

}  // namespace borealis::test_cases::itest_posix_instance_channel
