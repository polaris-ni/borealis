// ============================================================
// Win32 凭据库腿（src/platform/win/credential_store.cpp）
// ------------------------------------------------------------
// `platform::make_credential_store()` 的 Win32 实现（`SPEC.FEAT.CONN.09`）。
// 走 Credential Manager 的通用凭据（`CRED_TYPE_GENERIC`）：
//   * 目标名 `borealis/<profile_id>`（与前缀句柄 `credman:<profile_id>` 一一对应）；
//   * blob 直接放秘密的 UTF-8 字节，不经宽字符（D2：句柄只是引用，秘密只在这里过栈）；
//   * 持久化到本机（`CRED_PERSIST_LOCAL_MACHINE`），随用户配置存活。
//
// **本沙箱无 MSVC／Win32 头，本文件未编译、未实测**（设计稿 F9）：
// 一切 Windows 侧行为按公开 API 形状写，标「推断／未验证」；链接面 `advapi32` 已补。
// ============================================================

#include "platform/credential_store.h"

#include <windows.h>
#include <wincred.h>

#include <cstddef>
#include <string>

#include "aurora/core/log.h"
#include "platform/win/win_text.h"

namespace borealis::platform {

namespace {

/// @brief CredMan 目标名 `borealis/<profile_id>`（与句柄前缀 `credman:` 分开，前者是其命名空间）。
[[nodiscard]] auto target_name(const std::string &profile_id) -> std::wstring {
    return to_wide(std::string{"borealis/"} + profile_id);
}

}  // namespace

class CredmanCredentialStore final : public conn::CredentialStore {
  public:
    // CredMan 是系统自带设施；真正失败（含服务不可用）由各调用当场降级（D4）。
    [[nodiscard]] auto available() const -> bool override { return true; }

    [[nodiscard]] auto store(std::string_view profile_id, std::string_view label,
                             std::string secret) -> conn::SecretHandle override {
        const std::string id{profile_id};
        const std::string caption =
            label.empty() ? std::string{"Borealis connection"} : std::string{label};
        const auto fallback = conn::SecretHandle::ask_every_time(id, caption);
        if (id.empty() || id.find('\0') != std::string::npos) {
            return fallback;
        }
        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            AURORA_LOG_WARN("platform", "credential too large for CredMan, not stored");
            return fallback;
        }
        const std::wstring target = target_name(id);
        if (target.empty()) {
            return fallback;
        }

        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob =
            secret.empty() ? nullptr : reinterpret_cast<LPBYTE>(secret.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (CredWriteW(&credential, 0) == FALSE) {
            AURORA_LOG_WARN("platform", "CredMan write failed for " + id + ", error " +
                                            std::to_string(GetLastError()));
            return fallback;
        }
        return conn::SecretHandle::reference(credential_vault_id(kCredmanVaultPrefix, id));
    }

    [[nodiscard]] auto retrieve(const conn::SecretHandle &handle)
        -> std::optional<std::string> override {
        if (handle.kind != conn::SecretKind::Reference) {
            return std::nullopt;
        }
        const auto profile = credential_vault_profile(handle.vault_id, kCredmanVaultPrefix);
        if (!profile) {
            return std::nullopt;
        }
        const std::wstring target = target_name(*profile);
        if (target.empty()) {
            return std::nullopt;
        }

        PCREDENTIALW credential = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE) {
            return std::nullopt;  // 未命中是常态（换机、清理过或从未存过），不回询问由装配层判。
        }
        std::string out;
        if (credential->CredentialBlob != nullptr && credential->CredentialBlobSize > 0) {
            out.assign(reinterpret_cast<const char *>(credential->CredentialBlob),
                       static_cast<std::size_t>(credential->CredentialBlobSize));
        }
        CredFree(credential);
        return out;
    }

    [[nodiscard]] auto erase(const conn::SecretHandle &handle) -> bool override {
        if (handle.kind != conn::SecretKind::Reference) {
            return false;
        }
        const auto profile = credential_vault_profile(handle.vault_id, kCredmanVaultPrefix);
        if (!profile) {
            return false;
        }
        const std::wstring target = target_name(*profile);
        if (target.empty()) {
            return false;
        }
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE) {
            return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_FOUND) {
            return true;  // 本来不存在也算「库里已无此条」。
        }
        AURORA_LOG_WARN("platform",
                        "CredMan delete failed for " + *profile + ", error " + std::to_string(error));
        return false;
    }
};

auto make_credential_store() -> std::unique_ptr<conn::CredentialStore> {
    return std::make_unique<CredmanCredentialStore>();
}

}  // namespace borealis::platform
