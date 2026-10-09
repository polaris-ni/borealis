#pragma once

// ============================================================
// 连接管理器纯逻辑视图模型（src/conn/profile_views.h）
// ------------------------------------------------------------
// CONN.07 侧栏与向导的查询/校验/裁决全部住在这一层，UI 只落笔（设计稿 D7 的
// 「UI 只消费」）。本头只含标准类型 + 本域模型（profile.h），可无头单测；
// 私有头：视图模型形态随 UI 迭代，不进 include/。
// ============================================================

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/conn/profile.h"

namespace borealis::conn {

/// @brief 侧栏三视图（设计稿 D7）。
enum class SidebarViewMode : std::uint8_t {
    All,       ///< 全部档案。
    Favorites, ///< 仅收藏。
    Groups,    ///< 按分组排布（无分组的排最后）。
};

/// @brief 侧栏一行的投影：UI 只画这些字段，查询与排序已在模型层算好。
struct SidebarRow {
    std::string id{};          ///< 档案 id（连接/编辑/删除的定位锚）。
    std::string name{};        ///< 展示名。
    ConnectionType type{ConnectionType::Local}; ///< 类型图标取这里。
    bool favorite{false};      ///< 星标态。
    std::string group_label{}; ///< 分组视图下的组名；无分组为空串。
};

/// @brief 按视图模式与搜索词算侧栏行（次序即 UI 排版次序）。
///
/// 排序规则（确定性，单测锁）：组视图按「组名 → 名字」字典序、无分组殿后；
/// 其余视图按名字字典序。搜索词非空时走 `ProfileStore::search`（模型层已有
/// 大小写不敏感匹配），三视图的过滤在此之上叠加。
[[nodiscard]] auto sidebar_rows(const ProfileStore &store, SidebarViewMode mode,
                                std::string_view query) -> std::vector<SidebarRow>;

/// @brief 新建/编辑向导的一份草稿（UI 表单的字段集，CONN.07 步骤二）。
struct ProfileDraft {
    std::string name{};             ///< 展示名。
    ConnectionType type{ConnectionType::Local};
    // Local 型字段。
    std::string command_line{};     ///< 空＝默认 shell。
    std::string working_directory{};
    // SSH 型字段。
    std::string host{};
    int port{22};
    std::string user{};
    std::string auth_method{"agent"}; ///< password / privatekey / agent / keyboard-interactive。
    std::string identity_file{};
    bool agent_forwarding{false};
    KnownHostsPolicy known_hosts_policy{KnownHostsPolicy::AcceptNew};
};

/// @brief 草稿校验的问题码（UI 把每一档映射成一句可读文案；本层不产文案）。
enum class DraftIssue : std::uint8_t {
    None,             ///< 通过。
    EmptyName,        ///< 展示名必填。
    EmptyHost,        ///< SSH 型主机必填。
    PortOutOfRange,   ///< 端口须在 1..65535。
    UnknownAuthMethod ///< 认证方式必须是四档之一。
};

/// @brief 校验草稿：返回**首个**问题（顺序即上表），通过为 None。
[[nodiscard]] auto validate_draft(const ProfileDraft &draft) -> DraftIssue;

/// @brief 草稿 → 档案（校验通过后调用；id 由调用方定——新建走 ProfileStore 的
///        确定性 id 生成，编辑传原 id 以保最近连接等引用不断）。
[[nodiscard]] auto draft_to_profile(const ProfileDraft &draft, std::string id) -> Profile;

/// @brief 档案 → 草稿（编辑向导的回填）。
[[nodiscard]] auto profile_to_draft(const Profile &profile) -> ProfileDraft;

/// @brief SSH 连接需要向用户要哪种秘密材料（CONN.09 询问件的计划，纯逻辑）。
enum class SecretAsk : std::uint8_t {
    None,        ///< 不需要（agent 认证）。
    Password,    ///< 口令。
    Passphrase,  ///< 私钥 passphrase（可空口令钥匙——询问件须允许空提交）。
    Interactive, ///< keyboard-interactive 的首提示答案。
};

/// @brief 按认证方式算询问计划（与 SecretHandle 的 kind 正交：句柄是 Reference 时
///        调用方先查凭据库，查不到才落到这里要的询问）。
[[nodiscard]] auto secret_ask_for(std::string_view auth_method) -> SecretAsk;

/// @brief quick connect 输入的解析（CONN.03：`host[:port]` 临时输入即连，不生成档案）。
///
/// 宽容口径：两端空白剥掉；端口段非纯数字视为整体主机名的一部分（不猜）；空串/仅
/// 空白回 nullopt。@return 主机名与端口（缺省 22）。
[[nodiscard]] auto parse_quick_connect(std::string_view text)
    -> std::optional<std::pair<std::string, int>>;

/// @brief 一条 quick connect 的临时 SSH 档案（不入库：id 带 `quick:` 前缀，缺省 agent 认证）。
[[nodiscard]] auto quick_connect_profile(std::string host, int port) -> Profile;

}  // namespace borealis::conn
