#pragma once

// ============================================================
// OS 凭据库后端接口（src/platform/credential_store.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.09` 的真实后端接缝：`conn::CredentialStore` 只定义纯逻辑语义，
// libsecret（POSIX）与 CredMan（Win32）在本层落到平台实现（AGENTS.md §4.5 第 23/24 条）。
// 平台假设不进公共头：工厂与前缀常量都是私有头，只有装配层与平台腿消费。
//
// 句柄编码（D2）：`vault_id` 是「前缀 + 档案 id」，只含可公开的引用，不含任何秘密。
// 前缀本身也是归属标记：`retrieve`／`erase` 只认本腿前缀，跨腿句柄一律回空／false。
// ============================================================

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"

namespace borealis::platform {

/// @brief POSIX（libsecret）句柄前缀。
inline constexpr std::string_view kLibsecretVaultPrefix{"libsecret:"};
/// @brief Win32（CredMan）句柄前缀。
inline constexpr std::string_view kCredmanVaultPrefix{"credman:"};

/// @brief 按平台构造真实凭据库；平台后端不可用时回 `UnavailableCredentialStore` 语义的替身
///        （`available()==false`，`store` 只回询问哨兵）。
[[nodiscard]] auto make_credential_store() -> std::unique_ptr<conn::CredentialStore>;

/// @brief 把档案 id 编成句柄的 `vault_id`（前缀 + id）。
/// @param prefix 平台前缀（`kLibsecretVaultPrefix` / `kCredmanVaultPrefix`）。
/// @param profile_id 档案 id。
/// @return 编码结果；前缀或 id 为空、或含 NUL 时回空串（调用方按「无法编码」处理）。
[[nodiscard]] auto credential_vault_id(std::string_view prefix, std::string_view profile_id) -> std::string;

/// @brief 从 `vault_id` 解回档案 id（`credential_vault_id` 的逆）。
/// @param vault_id 句柄里的引用串。
/// @param prefix 本腿前缀；不匹配即回空。
/// @return 档案 id；前缀不符、剩余为空或含 NUL 时回 `std::nullopt`。
[[nodiscard]] auto credential_vault_profile(std::string_view vault_id, std::string_view prefix)
    -> std::optional<std::string>;

}  // namespace borealis::platform
