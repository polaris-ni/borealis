#pragma once

// ============================================================
// POSIX forkpty connection (src/platform/posix/forkpty_connection.h)
// ------------------------------------------------------------
// Platform-private header: POSIX types stay within this directory only, shared paths
// go through session::Connection abstraction (ruling 7.11, architecture §2.3 rule 1).
//
// Thread model per architecture §3.1: one read thread per session, raw bytes delivered
// via ConnectionEvents::on_bytes to the session; decoding, parsing and grid writes all
// happen on the session side, this class only manages the PTY and child process.
// ============================================================

#include <atomic>
#include <cstddef>
#include <mutex>
#include <span>
#include <thread>

#include "borealis/conn/local_terminal.h"
#include "borealis/session/connection.h"

namespace borealis::platform {

/// @brief Local terminal POSIX implementation: forkpty + child process + read thread.
class ForkptyConnection final : public session::Connection {
  public:
    /// @param spec Launch specification (command line, working directory, extra environment).
    /// @param initial_size Pseudo-terminal initial size, applied at creation time
    ///                     (SPEC.FEAT.XFER.01 session-start leg).
    ForkptyConnection(conn::LocalTerminalSpec spec, session::Size initial_size);

    ForkptyConnection(const ForkptyConnection &) = delete;
    ForkptyConnection(ForkptyConnection &&) = delete;
    auto operator=(const ForkptyConnection &) -> ForkptyConnection & = delete;
    auto operator=(ForkptyConnection &&) -> ForkptyConnection & = delete;

    ~ForkptyConnection() override;

    auto start(session::ConnectionEvents &events) -> void override;

    auto write(std::span<const std::byte> bytes) -> void override;

    auto resize(session::Size size) -> void override;

    auto close() -> void override;

    [[nodiscard]] auto alive() const noexcept -> bool override;

  private:
    /// @brief Read loop body: blocks reading from the PTY master until EOF, then notifies
    ///        the session that the stream has ended.
    auto read_loop() -> void;

    /// @brief Create the pseudo-terminal and attach the child process; any step failure
    ///        recovers created handles and returns false.
    /// @param events Event sink, used by the read thread after spawn succeeds.
    /// @return Whether the child process was started.
    auto spawn(session::ConnectionEvents &events) -> bool;

    conn::LocalTerminalSpec spec_;
    session::Size size_;

    /// Guards console_ handle usage and shutdown so resize does not race with close.
    std::mutex state_mutex_;
    /// Serialises pipe writes: user keystrokes come from the main thread, query responses
    /// come from the read thread — two interleaving paths that could split an escape sequence.
    std::mutex write_mutex_;

    /// PTY master file descriptor.
    int pty_master_ = -1;
    /// Child process PID.
    pid_t child_pid_ = -1;

    session::ConnectionEvents *events_ = nullptr;
    std::thread reader_;

    /// Shutdown flag: close()'s pre-emption and write()/resize()'s early exit key off this.
    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
};

}  // namespace borealis::platform
