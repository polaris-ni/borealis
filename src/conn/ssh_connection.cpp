// ============================================================
// SSH 传输的薄适配器（src/conn/ssh_connection.cpp）
// ------------------------------------------------------------
// 见同头注释与设计稿 `codespec/CONN02_REUSE.draft.md`。本文件只剩三件事：
//   1. 把「档案 + 秘密 + 尺寸」交给共享会话并挂接一份租约；
//   2. 把 UI 线程的 write／resize／retry_now／stop_reconnect 转成共享会话的命令；
//   3. 维护两枚原子位（`channel_open_` 由 I/O 线程置位/清位），供 `alive()` 免锁读取。
//
// 所有 libssh 调用都在共享会话的 I/O 线程上，本文件一行 libssh 都没有。
// `close()` 走 `std::call_once`：两个调用点（会话层与析构）都等到 `detach` 的 ack，
// 于是「返回后不再回调」对两者同时成立。
// ============================================================

#include "ssh_connection.h"

#include <utility>

#include "aurora/core/log.h"

namespace borealis::conn {

SshConnection::SshConnection(const SshProfile &profile, SshSecretBundle secret,
                             session::Size initial_size, RetryPolicy reconnect_policy,
                             SshAlgorithmOverrides algorithms,
                             std::shared_ptr<SshSessionRegistry> registry)
    : profile_{profile},
      secret_{std::move(secret)},
      size_{initial_size},
      reconnect_policy_{reconnect_policy},
      algorithms_{std::move(algorithms)},
      registry_{std::move(registry)} {}

SshConnection::~SshConnection() {
    close();
}

auto SshConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.exchange(true) || closing_.load()) {
        return;
    }
    // 两回尝试：共享会话可能恰在上一次挂接之后落了终态（registry 的借出与终止
    // 之间有窗口），换一条重来；自建会话不存在这一档。
    for (auto attempt = 0; attempt < 2; ++attempt) {
        if (shared_ == nullptr) {
            shared_ = registry_ != nullptr
                          ? registry_->acquire(profile_, secret_, algorithms_, reconnect_policy_)
                          : std::make_shared<SshSharedSession>(profile_, secret_,
                                                               reconnect_policy_, algorithms_);
        }
        auto on_open = [this] { channel_open_.store(true); };
        auto on_finished = [this] { channel_open_.store(false); };
        if (shared_->attach(this, events, size_, std::move(on_open), std::move(on_finished))) {
            secret_ = SshSecretBundle{};  // 材料（含逐跳）已移交给共享会话；本对象不再留明文。
            return;
        }
        shared_.reset();
    }
    AURORA_LOG_WARN("conn", "ssh: shared session refused the lease");
}

auto SshConnection::write(std::span<const std::byte> bytes) -> void {
    if (closing_.load() || !channel_open_.load() || shared_ == nullptr) {
        return;  // 退避期或已收口：键入丢弃（与旧单标签腿同语义）。
    }
    shared_->write(this, bytes);
}

auto SshConnection::resize(session::Size size) -> void {
    // 退避期间也要记账：共享会话按这份最新值在重拨时开 PTY（设计稿 D9）。
    size_ = size;
    if (shared_ != nullptr) {
        shared_->resize(this, size);
    }
}

auto SshConnection::close() -> void {
    std::call_once(close_once_, [this] {
        closing_.store(true);
        channel_open_.store(false);
        if (shared_ != nullptr) {
            shared_->detach(this);  // 等 ack：返回后 I/O 线程不再碰本对象的事件接收端。
            shared_.reset();
        }
        secret_ = SshSecretBundle{};
    });
}

auto SshConnection::alive() const noexcept -> bool {
    return started_.load() && !closing_.load() && channel_open_.load();
}

auto SshConnection::retry_now() -> void {
    if (shared_ != nullptr) {
        shared_->retry_now();
    }
}

auto SshConnection::stop_reconnect() -> void {
    if (shared_ != nullptr) {
        shared_->stop_reconnect();
    }
}

}  // namespace borealis::conn
