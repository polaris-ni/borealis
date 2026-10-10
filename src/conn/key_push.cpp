// ============================================================
// SSH 公钥推送腿实现（src/conn/key_push.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 批 2b（裁决 7.106 的 D7①/D8①）。libssh 只在本 TU 出现；
// 交回给面板的 `PushReport` 全是本仓自有类型（D1① 纪律）。
//
// 本棒新增的一条实测读数（批 2 探针，与稿 §0 同法）：
//   F15 **取退出码不必引第二处弃用抑制**：0.12 的 `ssh_channel_get_exit_status` 确实带
//       `SSH_DEPRECATED`（稿 §0 F10 的读数没错），但它底下就是非弃用的
//       `ssh_channel_get_exit_state(channel, &code, &signal, &core)`（读源：`channels.c`
//       里前者只是一位转发）。本腿走后者，于是 7.96④ 那处定点抑制仍是**全仓唯一一处**，
//       与稿 F1「不引第二处弃用抑制」的口径对齐——稿 §4 那句「沿用 tunnel_client 的
//       定点抑制写法」按代码改口，落地期订正随批 2 的裁决登记。
//   `ssh_channel_get_exit_state` 的 `pexit_signal` 交回的是 `strdup` 出来的串（读源同处），
//       传 nullptr 即不取、也就没有一份需要谁去 free 的孤儿——本腿只要退出码，信号名不要。
//
// 另两条口径不在实测里，写在明面：⑴ 判重与追加字节**全在** `conn/key_model`，本件一行
// 规则都不重拼（第 3 条）；⑵ 公钥行进的是 stdin，不进命令串、不进日志（D7① 的理由句）。
// ============================================================

#include "conn/key_push.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/key_model.h"
#include "conn/ssh_dial.h"

namespace borealis::conn {

namespace {

/// @brief D7① 第一段 exec：读回远端现内容。文件不存在时 `cat` 回非零并往 stderr 抱怨，
///        那正是「远端还没有授权表」的形态，故读腿的退出码不参与判据（见头注⑶）。
constexpr std::string_view kReadCommand = "cat ~/.ssh/authorized_keys";

/// @brief D7① 第二段 exec：`umask 077` 起头，目录与文件的权限位由远端 shell 自己收，
///        本仓不假设远端已有 `~/.ssh`；公钥行走 stdin，**命令串里一个密钥字节都没有**。
constexpr std::string_view kAppendCommand =
    "sh -c 'umask 077; mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys'";

/// @brief 单次 `ssh_channel_read_timeout` 的等待（毫秒）：通道已 EOF 时它回 0，不占满。
constexpr int kReadSliceMs = 2000;

/// @brief 连续空转轮数上限：远端 shell 卡住时本腿的等待上界（≈30 s），与 dial 腿的
///        10 s 建连超时同属「worker 线程上可接受的一次阻塞」。
constexpr int kIdleRoundCap = 15;

/// @brief 读缓冲：与 sftp 传输腿同量级，授权表是文本文件，一趟几十 KB 顶天。
constexpr std::size_t kReadSliceBytes = 4096;

using SessionHandle = std::unique_ptr<ssh_session_struct, decltype(&ssh_free)>;
using ChannelHandle = std::unique_ptr<ssh_channel_struct, decltype(&ssh_channel_free)>;

[[nodiscard]] auto adopt_session(ssh_session session) -> SessionHandle {
    return SessionHandle{session, ssh_free};
}

[[nodiscard]] auto adopt_channel(ssh_channel channel) -> ChannelHandle {
    return ChannelHandle{channel, ssh_channel_free};
}

/// @brief 一次 exec 的现场（内部件：不外露，退出码与两条流的去处由调用方定）。
struct ExecRun {
    bool started{false};       ///< 通道开成**且** `request_exec` 被接受。
    bool write_failed{false};  ///< stdin 写不进或 `send_eof` 失败：只有追加腿会用到。
    bool exit_known{false};    ///< 取到了退出码（`SSH_OK`）。
    std::uint32_t exit_code{0};
    std::string output;        ///< stdout 的全部字节。
    std::string error_text;    ///< stderr 的全部字节（只进日志，绝不进判重）。
};

/// @brief 开一条执行通道跑完 `command`，`payload` 走 stdin；返回时通道已关已释。
///
/// 会话不属于本函数，故本函数不 disconnect、不 free 会话（D8① 的自开关由调用方收口）。
/// 阻塞语义全程沿用 libssh 的默认阻塞模式，与 sftp/tunnel 两条既有腿同档。
auto run_exec(ssh_session_struct *session, std::string_view command, std::string_view payload)
    -> ExecRun {
    auto run = ExecRun{};
    // `request_exec` 要 C 串：命令是文件级常量，这里为形态而非内容付一次拷贝。
    const auto command_text = std::string{command};
    auto channel = adopt_channel(ssh_channel_new(session));
    if (!channel) {
        AURORA_LOG_ERROR("conn", "ssh: push: cannot allocate channel");
        return run;
    }
    if (ssh_channel_open_session(channel.get()) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: push: open session channel failed: ",
                         ssh_get_error(session));
        return run;
    }
    if (ssh_channel_request_exec(channel.get(), command_text.c_str()) != SSH_OK) {
        // restricted shell / 禁 exec 的服务器落在这里（D7 登记的那条代价）。
        AURORA_LOG_ERROR("conn", "ssh: push: exec refused: ", ssh_get_error(channel.get()));
        return run;
    }
    run.started = true;

    if (!payload.empty()) {
        std::size_t offset = 0U;
        while (offset < payload.size()) {
            const auto remaining = static_cast<std::uint32_t>(payload.size() - offset);
            const int written = ssh_channel_write(channel.get(), payload.data() + offset, remaining);
            if (written <= 0) {
                AURORA_LOG_ERROR("conn", "ssh: push: stdin write failed: ",
                                 ssh_get_error(channel.get()));
                run.write_failed = true;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        // 无论写全与否都要收口 stdin：`cat` 不拿到 EOF 就永远不退出，下一条读会空转到底。
        static_cast<void>(ssh_channel_send_eof(channel.get()));
    }

    auto buffer = std::array<char, kReadSliceBytes>{};
    auto idle_rounds = int{0};
    while (true) {
        const int out = ssh_channel_read_timeout(channel.get(), buffer.data(),
                                                 static_cast<std::uint32_t>(buffer.size()), 0,
                                                 kReadSliceMs);
        const int err = ssh_channel_read_timeout(channel.get(), buffer.data(),
                                                 static_cast<std::uint32_t>(buffer.size()), 1,
                                                 kReadSliceMs);
        if (out < 0 || err < 0) {
            break;  // 通道级错误：已经读到的字节照交，判据由调用方的档位收。
        }
        if (out > 0) {
            run.output.append(buffer.data(), static_cast<std::size_t>(out));
        }
        if (err > 0) {
            run.error_text.append(buffer.data(), static_cast<std::size_t>(err));
        }
        if (out > 0 || err > 0) {
            idle_rounds = 0;
            continue;
        }
        if (ssh_channel_is_eof(channel.get()) || ssh_channel_is_closed(channel.get())) {
            break;
        }
        if (++idle_rounds >= kIdleRoundCap) {
            AURORA_LOG_WARN("conn", "ssh: push: exec still open after the read budget");
            break;
        }
    }

    // 退出码要在 close 之前取：通道一关，libssh 就不再替我们留着那条 exit-status 记录。
    auto exit_code = std::uint32_t{0};
    if (ssh_channel_get_exit_state(channel.get(), &exit_code, nullptr, nullptr) == SSH_OK) {
        run.exit_known = true;
        run.exit_code = exit_code;
    }
    static_cast<void>(ssh_channel_close(channel.get()));
    return run;
}

/// @brief `DialOutcome` 的四个失败值 → 本腿的失败档位（一比一，全仓唯一一处翻译）。
///        `Ok` 不会走到这里；真走到了也只能落到「网络」那一档，绝不留一档「无失败」。
[[nodiscard]] auto failure_of_dial(DialOutcome outcome) -> PushFailure {
    switch (outcome) {
    case DialOutcome::Unallocated:
        return PushFailure::DialUnallocated;
    case DialOutcome::Network:
        return PushFailure::DialNetwork;
    case DialOutcome::HostKey:
        return PushFailure::DialHostKey;
    case DialOutcome::Auth:
        return PushFailure::DialAuth;
    case DialOutcome::Ok:
        break;
    }
    return PushFailure::DialNetwork;
}

}  // namespace

auto authorized_keys_read_command() -> std::string_view {
    return kReadCommand;
}

auto authorized_keys_append_command() -> std::string_view {
    return kAppendCommand;
}

auto push_stage_index(PushStage stage) -> int {
    switch (stage) {
    case PushStage::Dial:
        return 0;
    case PushStage::Auth:
        return 1;
    case PushStage::Exec:
        return 2;
    case PushStage::Done:
        return 3;
    }
    return 0;  // 穷尽后不可达：阶梯不能有一格落到格子外面（与 `key_push_stage_key` 同尾）。
}

auto push_public_key(const SshProfile &profile, std::optional<std::string> secret,
                     std::string_view line) -> PushReport {
    auto report = PushReport{};

    // ⑴ 纯逻辑闸：拨号之前先问「这行成不成立」。payload 为空即废行，此时既不分配会话
    //     也不碰网络——拿废行去拨号是白跑一趟，还可能触发远端的失败计数。
    if (plan_append(std::string_view{}, line).payload.empty()) {
        report.stage = PushStage::Dial;
        report.failure = PushFailure::InvalidLine;
        AURORA_LOG_ERROR("conn", "ssh: push: refused a line that is not a public key");
        return report;
    }

    // ⑵ 自开会话（D8①）：口径照 sftp_client，建连/核对/认证全在 dial 腿，本件不重拼策略。
    auto session = adopt_session(ssh_new());
    if (!session) {
        report.stage = PushStage::Dial;
        report.failure = PushFailure::DialUnallocated;
        AURORA_LOG_ERROR("conn", "ssh: push: cannot allocate ssh session");
        return report;
    }
    const auto dial = ssh_dial_and_authenticate(profile, std::move(secret), session.get());
    if (dial != DialOutcome::Ok) {
        // 认证那一格只归认证；其余失败都发生在拨号格（主机键核对也算，dial 腿的口径）。
        report.stage = dial == DialOutcome::Auth ? PushStage::Auth : PushStage::Dial;
        report.failure = failure_of_dial(dial);
        return report;
    }

    // ⑶ 读回远端现内容 → 交 `plan_append()` 判重（本地判重是判据 §6 的第一句）。
    const auto read = run_exec(session.get(), kReadCommand, {});
    if (!read.started) {
        report.stage = PushStage::Exec;
        report.failure = PushFailure::ExecRefused;
        return report;
    }
    const auto plan = plan_append(read.output, line);
    if (plan.already_authorized) {
        // 成功且**不发写**：这一档既不是失败也不是「刚写了什么」，面板据此告知「已在授权表里」。
        report.stage = PushStage::Done;
        report.already_authorized = true;
        return report;
    }

    // ⑷ 追加：公钥行走 stdin（⑵ 的命令串里没有它），退出码是这一段的唯一回执。
    const auto write = run_exec(session.get(), kAppendCommand, plan.payload);
    if (!write.started) {
        report.stage = PushStage::Exec;
        report.failure = PushFailure::ExecRefused;
        return report;
    }
    if (write.write_failed || !write.exit_known || write.exit_code != 0U) {
        AURORA_LOG_ERROR("conn", "ssh: push: append exited ",
                         write.exit_known ? static_cast<int>(write.exit_code) : -1,
                         ": ", write.error_text);
        report.stage = PushStage::Exec;
        report.failure = PushFailure::WriteRejected;
        return report;
    }
    report.stage = PushStage::Done;
    return report;
}

}  // namespace borealis::conn
