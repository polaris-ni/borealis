/// 测试类型: integration
/// 目标单元: src/conn/key_push.{h,cpp}（SPEC.FEAT.CONN.10 批 2b，裁决 7.106 D7①/D8①）
/// 测试说明: 沙箱无 sshd，推送的**成功腿**与 exec 两档失败不可测（稿 §7 与 7.96① 的在册欠账，
///           真 sshd 到货后归 etest_）。本用例走确定可判的那条路：对 127.0.0.1:1 拨号必被 TCP
///           拒（`itest_tunnel_failure` 同法），于是能证两件事——⑴ 失败落在步骤态的**哪一格**
///           （拨号格，归因 `DialNetwork`，面板据此把阶梯停在第一格而不是全染红）；
///           ⑵ **废行压根不碰网络**：同一枚必拒档案配一行不成串的公钥，回的必须是
///           `InvalidLine` 而不是 `DialNetwork`——档位一变就说明那道拨号之前的闸被挪走了。
///           这两条都是「装配层要照抄的语义」，不是 libssh 的行为复述。

#include "conn/key_push.h"

#include <optional>
#include <string>

#include "borealis/conn/profile.h"
#include "conn/key_model.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::itest_keys_push {

namespace {

using namespace borealis;

/// @brief 指向本机必拒端口（1）的 SSH 档案：TCP 层即刻 ECONNREFUSED，不依赖任何外部网络。
[[nodiscard]] auto refused_profile() -> conn::SshProfile {
    auto profile = conn::SshProfile{};
    profile.host = "127.0.0.1";
    profile.port = 1;
    profile.user = "nobody";
    profile.auth_method = "agent";
    return profile;
}

/// @brief 一行**成串**的公钥：判重闸放过它，本用例要的是它走到拨号那一步。
[[nodiscard]] auto well_formed_line() -> std::string {
    return conn::public_line("ssh-ed25519",
                             "AAAAC3NzaC1lZDI1NTE5AAAAIOW3xkQ8bVJ3qZ0tFm2pYdBsXwEe6hR7vqZ1kQ3oZ4dN",
                             "desk@borealis");
}

}  // namespace

AURORA_TEST_CASE(a_refused_host_lands_the_failure_on_the_dial_step) {
    const auto report = conn::push_public_key(refused_profile(), std::nullopt, well_formed_line());

    AURORA_TEST_CHECK_EQ(static_cast<int>(report.stage), static_cast<int>(conn::PushStage::Dial));
    AURORA_TEST_CHECK_TRUE(report.failure.has_value());
    AURORA_TEST_CHECK_EQ(static_cast<int>(report.failure.value_or(conn::PushFailure::DialAuth)),
                         static_cast<int>(conn::PushFailure::DialNetwork));
    // 「没写成功」不等于「已在授权表里」：这一档若为真，面板会把失败报成一句好消息。
    AURORA_TEST_CHECK_TRUE(!report.already_authorized);
}

AURORA_TEST_CASE(an_unformed_line_never_opens_a_socket) {
    // 三行废料：只有线名、只有 blob、整行空白。`plan_append` 认不成行的那一条都在这里露形。
    for (const auto &line : {std::string{"ssh-ed25519"},
                             std::string{"AAAAC3NzaC1lZDI1NTE5AAAAIOW3xkQ8bVJ3qZ0tFm2pYd"},
                             std::string{"   "}}) {
        const auto report = conn::push_public_key(refused_profile(), std::nullopt, line);
        AURORA_TEST_CHECK_MSG(static_cast<int>(report.stage) ==
                                  static_cast<int>(conn::PushStage::Dial) &&
                                  report.failure.value_or(conn::PushFailure::DialNetwork) ==
                                      conn::PushFailure::InvalidLine,
                              "the pre-dial gate refused it, not the host");
        AURORA_TEST_CHECK_TRUE(!report.already_authorized);
    }
}

}  // namespace borealis::test_cases::itest_keys_push
