#pragma once

// ============================================================
// 连接档案与凭据模型（include/borealis/conn/profile.h）
// ------------------------------------------------------------
// M3 的 `SPEC.FEAT.CONN.03`（SSH 档案管理：分组/收藏/搜索/quick connect/
// `~/.ssh/config` 只读导入）与 `SPEC.FEAT.CONN.09`（凭据安全存储）的纯逻辑数据模型。
//
// 本头只含标准类型，不含 Aurora 类型与任何平台 API（与 `local_terminal.h` 同口径：
// 纯逻辑、可独立单测，AGENTS.md §4.4 第 20 条）。序列化进出 `au::json::Value` 落在
// `config/store.cpp`（裁决 7.26③：公共头不碰框架类型）。
//
// 凭据安全（CONN.09）的核心不变量：`SecretHandle` 永远不持有明文口令或私钥 passphrase。
// 它只持两种形态之一——
//   * Reference：OS 凭据库返回的**不透明引用句柄**（vault_id），真实秘密从不在本仓落盘；
//   * AskEveryTime：OS 凭据库不可用时（libsecret/Keychain/CredMan 全部缺失）的降级哨兵，
//     意为「每次连接都询问用户」，同样不保存任何秘密。
// 配置文件里出现的 `secret` 段因此永远只是上面的引用或哨兵，明文凭据扫描
// （`kBannedKeyNames` + `find_credential_key`）对本仓自有文档是永不触发分支。
//
// OS 凭据库的真实后端（libsecret / Keychain / CredMan）属独立任务，本切片只给出
// 接口 `CredentialStore` 与两个纯逻辑替身：`InMemoryCredentialStore`（测试）、
// `UnavailableCredentialStore`（运行期降级）。真实后端落地后接 `CredentialStore` 即可，
// 档案模型与落盘形态不变。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::conn {

/// @brief 连接类型（档案本体按它分流到 local / ssh 子结构）。
enum class ConnectionType : std::uint8_t {
    Local,  ///< 本地终端（走 `LocalProfile`）。
    Ssh,    ///< SSH（走 `SshProfile`）。
};

/// @brief 凭据句柄的两种形态（CONN.09 的核心枚举）。
enum class SecretKind : std::uint8_t {
    /// @brief 指向 OS 凭据库的不透明引用；`vault_id` 即库返回的句柄，真实秘密不在本仓。
    Reference,
    /// @brief 降级哨兵：OS 凭据库不可用，连接时每次都询问用户；不保存任何秘密。
    AskEveryTime,
};

/// @brief 一条凭据的引用/哨兵（CONN.09）：永远不含明文。
///
/// 序列化只写 `kind` 与上下文字段；`is_plaintext()` 恒为 false 是对外承诺，
/// 也是本类型不可被误用的静态护栏。
struct SecretHandle {
    SecretKind kind{SecretKind::Reference};
    std::string vault_id;     ///< kind==Reference：OS 凭据库返回的句柄。
    std::string profile_id;   ///< kind==AskEveryTime：询问时回指档案，便于 UI 给出上下文。
    std::string label;        ///< 人类可读标签（如「gw.example.com 的密码」），用于询问提示。

    /// @brief 构造一条 Reference 句柄。
    [[nodiscard]] static auto reference(std::string vault_id) -> SecretHandle {
        SecretHandle handle;
        handle.kind = SecretKind::Reference;
        handle.vault_id = std::move(vault_id);
        return handle;
    }

    /// @brief 构造一条 AskEveryTime 哨兵（降级路径）。
    [[nodiscard]] static auto ask_every_time(std::string profile_id, std::string label) -> SecretHandle {
        SecretHandle handle;
        handle.kind = SecretKind::AskEveryTime;
        handle.profile_id = std::move(profile_id);
        handle.label = std::move(label);
        return handle;
    }

    /// @brief 本句柄是否可能泄露明文：恒 false（CONN.09 不变量）。
    [[nodiscard]] auto is_plaintext() const noexcept -> bool { return false; }

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const SecretHandle &) const noexcept -> bool = default;
};

/// @brief 本地终端档案（对应 `LocalTerminalSpec` 的持久化形态，CONN.03）。
struct LocalProfile {
    std::string command_line;          ///< 自定义命令行；空＝平台默认 shell 探测。
    std::string working_directory;     ///< 启动目录；空＝继承进程当前目录。
    std::map<std::string, std::string> environment;  ///< 追加环境变量（同名覆盖继承来的）。

    [[nodiscard]] auto operator==(const LocalProfile &) const noexcept -> bool = default;
};

/// @brief 已知主机策略（SSH `StrictHostKeyChecking` 的口径，CONN.03）。
enum class KnownHostsPolicy : std::uint8_t {
    AcceptNew,  ///< 首次接受、之后核对（OpenSSH `accept-new`，缺省）。
    Yes,        ///< 必须已知（`StrictHostKeyChecking yes`）。
    No,         ///< 不核对（`no`，不安全，仅旧设备兼容）。
    Ask,        ///< 未知时询问用户。
};

/// @brief SSH 档案（CONN.03）。
struct SshProfile {
    std::string host;                 ///< 主机名或 IP。
    int port{22};
    std::string user;                 ///< 登录用户名；空＝SSH 默认（当前用户）。
    std::string auth_method{"agent"}; ///< password / privatekey / agent / keyboard-interactive。
    std::string identity_file;        ///< 私钥路径；空＝SSH 默认（`~/.ssh/id_*`）。
    bool agent_forwarding{false};     ///< Agent 转发开关（按 profile，默认关，CONN.02）。
    KnownHostsPolicy known_hosts_policy{KnownHostsPolicy::AcceptNew};
    SecretHandle credential{};        ///< 该连接的口令/私钥 passphrase 引用或询问哨兵（CONN.09）。

    [[nodiscard]] auto operator==(const SshProfile &) const noexcept -> bool = default;
};

/// @brief 一条连接档案（CONN.03 的统一外壳）。
struct Profile {
    std::string id;                  ///< 稳定唯一 id；quick connect 与 ssh config 导入用确定性生成。
    std::string name;                ///< 展示名。
    ConnectionType type{ConnectionType::Local};
    std::vector<std::string> tags{};   ///< 自由标签（搜索用）。
    std::vector<std::string> groups{}; ///< 分组（侧栏分组视图用）。
    bool favorite{false};              ///< 收藏（置顶/快捷入口用）。
    LocalProfile local{};              ///< type==Local 时生效。
    SshProfile ssh{};                  ///< type==Ssh 时生效。

    [[nodiscard]] auto operator==(const Profile &) const noexcept -> bool = default;
};

/// @brief 凭据库接口（CONN.09）：档案只持 `SecretHandle`，真实秘密由实现保管。
///
/// 真实后端（libsecret / Keychain / CredMan）属独立任务；本切片提供两个纯逻辑替身
/// （`InMemoryCredentialStore` / `UnavailableCredentialStore`）供运行期与测试使用。
class CredentialStore {
  public:
    virtual ~CredentialStore() = default;

    /// @brief 凭据库是否可用（OS 后端存在）。不可用时 `store` 退化为询问哨兵。
    [[nodiscard]] virtual auto available() const -> bool = 0;

    /// @brief 存入一条秘密，回其引用句柄。不可用时回 AskEveryTime 哨兵且**不保存**秘密。
    [[nodiscard]] virtual auto store(std::string_view profile_id, std::string_view label,
                                     std::string secret) -> SecretHandle = 0;

    /// @brief 凭句柄取回秘密；句柄无效或不可用时回空。
    [[nodiscard]] virtual auto retrieve(const SecretHandle &handle) -> std::optional<std::string> = 0;
};

/// @brief 测试/内存替身：秘密留在内存映射，便于单测；`available()` 恒 true。
class InMemoryCredentialStore : public CredentialStore {
  public:
    [[nodiscard]] auto available() const -> bool override { return true; }
    [[nodiscard]] auto store(std::string_view profile_id, std::string_view label,
                             std::string secret) -> SecretHandle override;
    [[nodiscard]] auto retrieve(const SecretHandle &handle) -> std::optional<std::string> override;

  private:
    std::map<std::string, std::string> vault_;
    std::uint64_t seq_{0};
};

/// @brief 运行期降级替身：OS 凭据库全缺时，任何 `store` 都只回询问哨兵、绝不保存明文。
class UnavailableCredentialStore : public CredentialStore {
  public:
    [[nodiscard]] auto available() const -> bool override { return false; }
    [[nodiscard]] auto store(std::string_view profile_id, std::string_view label,
                             std::string /*secret*/) -> SecretHandle override {
        return SecretHandle::ask_every_time(std::string{profile_id}, std::string{label});
    }
    [[nodiscard]] auto retrieve(const SecretHandle &) -> std::optional<std::string> override {
        return std::nullopt;
    }
};

/// @brief 档案集合：增删改查与分组/收藏/搜索（CONN.03 的查询侧，纯逻辑、可单测）。
///
/// 持有 `std::vector<Profile>`；持久化落盘的是这个 vector（经 `config::Store` 的
/// `profiles` 域），本类型只是内存态的查询视图，不入 `Settings`。
class ProfileStore {
  public:
    using Profiles = std::vector<Profile>;

    ProfileStore() = default;
    explicit ProfileStore(std::vector<Profile> profiles) : items_(std::move(profiles)) {}

    [[nodiscard]] auto empty() const noexcept -> bool { return items_.empty(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return items_.size(); }
    [[nodiscard]] auto profiles() const noexcept -> const Profiles & { return items_; }

    /// @brief 按 id 查档案；不存在回 nullptr。
    [[nodiscard]] auto find(std::string_view id) const -> const Profile *;
    /// @brief 某分组下的全部档案（按登记次序）。
    [[nodiscard]] auto by_group(std::string_view group) const -> std::vector<const Profile *>;
    /// @brief 全部收藏（按登记次序）。
    [[nodiscard]] auto favorites() const -> std::vector<const Profile *>;
    /// @brief 按名称/标签/主机模糊匹配（子串、大小写不敏感），用于搜索框与 quick connect。
    [[nodiscard]] auto search(std::string_view query) const -> std::vector<const Profile *>;

    /// @brief 新增一条；id 冲突回 false（不改集合）。
    auto add(Profile profile) -> bool;
    /// @brief 按 id 删除；不存在回 false。
    auto remove(std::string_view id) -> bool;
    /// @brief 整条替换（按 id 匹配）；不存在回 false。
    auto update(Profile profile) -> bool;

  private:
    Profiles items_;
};

/// @brief 只读导入 `~/.ssh/config`：把每个 `Host` 块折成一条 SSH 档案（CONN.03）。
///
/// 纯函数、不碰文件系统——调用方负责读盘后把文本喂进来，于是可无头单测。
/// 单向、带变更检测由调用方负责（仅「导入」不「回写」，避免与用户手改的 config 冲突）。
/// 生成的 id 确定性地由输入推出（`ssh-import:<pattern>`），重复导入同一文件得到同 id，
/// 便于调用方做「已存在则跳过/更新」的去重。
/// @param content 文件全文。
/// @return 按文件出现次序的 SSH 档案列表；解析不出任何 `Host` 时为空的合法结果。
[[nodiscard]] auto parse_ssh_config(std::string_view content) -> std::vector<Profile>;

}  // namespace borealis::conn
