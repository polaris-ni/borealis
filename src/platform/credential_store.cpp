// ============================================================
// OS 凭据库句柄编码（src/platform/credential_store.cpp）
// ------------------------------------------------------------
// `platform/credential_store.h` 的纯字符串部分（`SPEC.FEAT.CONN.09` D2）：只做
// 「前缀 + 档案 id」的编码与逆解析，不碰平台 API，故两腿共编一份、可无头单测。
// ============================================================

#include "platform/credential_store.h"

namespace borealis::platform {

namespace {

/// @brief 引用串会落进 JSON 并交给平台 API 的 NUL 终止参数，含 NUL 即不可往返。
[[nodiscard]] auto has_nul(std::string_view text) -> bool {
    return text.find('\0') != std::string_view::npos;
}

}  // namespace

auto credential_vault_id(std::string_view prefix, std::string_view profile_id) -> std::string {
    if (prefix.empty() || profile_id.empty() || has_nul(prefix) || has_nul(profile_id)) {
        return {};
    }
    std::string out;
    out.reserve(prefix.size() + profile_id.size());
    out.append(prefix);
    out.append(profile_id);
    return out;
}

auto credential_vault_profile(std::string_view vault_id, std::string_view prefix)
    -> std::optional<std::string> {
    if (prefix.empty() || vault_id.size() <= prefix.size() || !vault_id.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto rest = vault_id.substr(prefix.size());
    if (has_nul(rest)) {
        return std::nullopt;
    }
    return std::string{rest};
}

}  // namespace borealis::platform
