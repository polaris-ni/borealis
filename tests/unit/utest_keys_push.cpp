/// 测试类型: unit
/// 目标单元: src/conn/key_push.{h,cpp}（SPEC.FEAT.CONN.10 批 2b，裁决 7.106 D7①/D8①）
/// 测试说明: 本件无头可证的那一半——**两段 exec 的命令串**。稿 §6 把「exec 命令常量里没有
///           base64 段」钉成判据，断言的对象得是可读的常量，故两条命令经
///           `authorized_keys_read_command()` / `authorized_keys_append_command()` 外露。
///           判据不只看「我这一枚样本串不在里面」，还量**最长的一段 base64 字母连续串**：
///           真 blob 是 68 字符起，而两条命令串里最长的合法段是路径词 `authorized`（10），
///           于是「命令串里没有长到像密钥材料的段」与具体样本无关——换一把钥匙、改一段注释
///           都不会让它假绿。读腿另外判它**不带重定向**（读腿把文件截空是最坏的失败形态）。
///           拨号之后的三段（认证/执行/写回执）要有真 sshd，本棒不在这里测（同 7.96① 的在册
///           欠账，稿 §7）；失败档位与「废行不拨号」归 itest_keys_push。

#include "conn/key_push.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "conn/key_model.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_keys_push {

namespace {

using namespace borealis;

/// @brief 短到这个长度就不可能是密钥材料：`authorized_keys` 里的 `authorized` 是 10，
///        而一把 ed25519 的 blob 实测 68 字符、RSA-4096 的更是上千。取 16 留足余量。
constexpr std::size_t kBlobLikeRun = 16U;

/// @brief 一把「看起来是真的」ed25519 公钥的 base64 段（判据只需要它的长度与字母表）。
constexpr std::string_view kSampleBlob =
    "AAAAC3NzaC1lZDI1NTE5AAAAIOW3xkQ8bVJ3qZ0tFm2pYdBsXwEe6hR7vqZ1kQ3oZ4dN";

/// @brief 量一段文本里最长的 base64 字母连续段（`[A-Za-z0-9+/]`；下划线与连字符按 OpenSSH
///        的标准字母表**不算**，故 `authorized_keys` 在这里断成两段）。
[[nodiscard]] auto longest_base64_run(std::string_view text) -> std::size_t {
    const auto is_base64 = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '+' || c == '/';
    };
    auto best = std::size_t{0U};
    auto current = std::size_t{0U};
    for (const char c : text) {
        current = is_base64(c) ? current + 1U : 0U;
        if (current > best) {
            best = current;
        }
    }
    return best;
}

[[nodiscard]] auto sample_line() -> std::string {
    return conn::public_line("ssh-ed25519", kSampleBlob, "desk@borealis");
}

}  // namespace

AURORA_TEST_CASE(neither_exec_command_carries_anything_like_key_material) {
    const auto read = conn::authorized_keys_read_command();
    const auto append = conn::authorized_keys_append_command();

    // 正控制：先证明这把尺子会量到东西——同一行 blob 进了 payload 就该被读出长段。
    // 少了这一句，「两条命令都短」可能只是尺子本身永远回 0（第 22 条的反空转）。
    const auto plan = conn::plan_append(std::string_view{}, sample_line());
    AURORA_TEST_REQUIRE_MSG(plan.payload.find(kSampleBlob) != std::string_view::npos,
                            "the public line goes to the payload");
    AURORA_TEST_REQUIRE_MSG(longest_base64_run(plan.payload) >= kBlobLikeRun,
                            "the blob is measurable");

    AURORA_TEST_CHECK_MSG(longest_base64_run(read) < kBlobLikeRun, "read command is bare");
    AURORA_TEST_CHECK_MSG(longest_base64_run(append) < kBlobLikeRun, "append command is bare");
    // 与样本无关的两条：线名与 blob 都不许出现在命令里（D7①：不进对端进程表、不进本仓日志）。
    AURORA_TEST_CHECK_TRUE(read.find(kSampleBlob) == std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find(kSampleBlob) == std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find("ssh-ed25519") == std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(read.find("ssh-ed25519") == std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find(sample_line()) == std::string_view::npos);
}

AURORA_TEST_CASE(the_two_commands_are_the_read_and_the_append_leg_of_one_file) {
    const auto read = conn::authorized_keys_read_command();
    const auto append = conn::authorized_keys_append_command();

    // 读腿**不带重定向**：exec 命令一旦写成 `cat >` 就是把用户的授权表截空，
    // 而这一条在真 sshd 到货前没有任何别的证人。
    AURORA_TEST_CHECK_MSG(read.find('>') == std::string_view::npos, "read leg cannot truncate");
    AURORA_TEST_CHECK_MSG(read.find('|') == std::string_view::npos, "read leg is a plain cat");
    AURORA_TEST_CHECK_TRUE(read.find("authorized_keys") != std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(read.find("cat") != std::string_view::npos);

    // 追加腿的四件事（D7① 原文）：umask 收到 077、目录建起来、权限钉 700、走 append。
    AURORA_TEST_CHECK_TRUE(append.find("umask 077") != std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find("mkdir -p ~/.ssh") != std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find("chmod 700 ~/.ssh") != std::string_view::npos);
    AURORA_TEST_CHECK_TRUE(append.find("cat >> ~/.ssh/authorized_keys") != std::string_view::npos);
    // 两条腿指的是同一个远端文件：路径串在这里必须是同一份写法（`~` 交远端 shell 展开）。
    AURORA_TEST_CHECK_MSG(append.find("~/.ssh/authorized_keys") != std::string_view::npos &&
                              read.find("~/.ssh/authorized_keys") != std::string_view::npos,
                          "both legs name the same remote path");
}

}  // namespace borealis::test_cases::utest_keys_push
