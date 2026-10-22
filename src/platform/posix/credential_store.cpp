// ============================================================
// POSIX 凭据库腿（src/platform/posix/credential_store.cpp）
// ------------------------------------------------------------
// `platform::make_credential_store()` 的 POSIX 实现（`SPEC.FEAT.CONN.09`）。
// 经 dlopen 在运行期接 libsecret，不引入构建期依赖（设计稿 D1）：
// 本机有 `libsecret-1.so.0` 运行库而无开发头，且 glib/libsecret 进 vcpkg 会显著拖慢构建。
//
// ABI 依据 libsecret 0.21.7 的公开头（`secret-types.h` / `secret-password.h`）：
//   SecretSchema{name, flags, attributes[32], reserved, reserved1..7}
//   secret_password_store_sync(schema, collection, label, password, cancellable, error, ...)
//   secret_password_lookup_sync(schema, cancellable, error, ...)
//   secret_password_clear_sync(schema, cancellable, error, ...)
// 升级系统 libsecret 时须重核这四处；符号不全一律按「库不可用」降级。
//
// 线程：dlopen 句柄只读、libsecret 同步调用可跨线程；装配层仍把它们放进后台任务
// （设计稿 D8，AGENTS.md §4.5 第 25 条）。
// ============================================================

#include "platform/credential_store.h"

#include <dlfcn.h>

#include <memory>
#include <string>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

// ---- libsecret 最小 ABI（见文件头依据） ----

struct SecretSchemaAttribute {
    const char *name;
    int type;  // SecretSchemaAttributeType；SECRET_SCHEMA_ATTRIBUTE_STRING == 0。
};

struct SecretSchema {
    const char *name;
    int flags;  // SecretSchemaFlags；SECRET_SCHEMA_NONE == 0。
    SecretSchemaAttribute attributes[32];
    int reserved;
    void *reserved1;
    void *reserved2;
    void *reserved3;
    void *reserved4;
    void *reserved5;
    void *reserved6;
    void *reserved7;
};

// GError 的前三字段（GQuark domain; gint code; gchar *message）。只读 message 用于诊断。
struct GErrorLite {
    unsigned int domain;
    int code;
    char *message;
};

constinit const SecretSchema kSchema{
    "org.borealis.Connection",
    0,
    {{"profile_id", 0}},
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

using StoreFn = int (*)(const SecretSchema *, const char *, const char *, const char *, void *,
                        GErrorLite **, ...);
using LookupFn = char *(*)(const SecretSchema *, void *, GErrorLite **, ...);
using ClearFn = int (*)(const SecretSchema *, void *, GErrorLite **, ...);
using PasswordFreeFn = void (*)(char *);
using ErrorFreeFn = void (*)(GErrorLite *);

struct LibsecretApi {
    StoreFn store{nullptr};
    LookupFn lookup{nullptr};
    ClearFn clear{nullptr};
    PasswordFreeFn password_free{nullptr};
    ErrorFreeFn error_free{nullptr};

    [[nodiscard]] auto ready() const -> bool {
        return store != nullptr && lookup != nullptr && clear != nullptr &&
               password_free != nullptr && error_free != nullptr;
    }
};

auto load_api() -> LibsecretApi {
    // 句柄不 dlclose：进程级设施，且短生命周期句柄会让其它线程手里的符号悬空。
    void *secret = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
    if (secret == nullptr) {
        AURORA_LOG_WARN("platform", "libsecret not loadable; credentials fall back to ask-every-time");
        return {};
    }
    void *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (glib == nullptr) {
        AURORA_LOG_WARN("platform", "glib not loadable; credentials fall back to ask-every-time");
        return {};
    }

    LibsecretApi api;
    api.store = reinterpret_cast<StoreFn>(dlsym(secret, "secret_password_store_sync"));
    api.lookup = reinterpret_cast<LookupFn>(dlsym(secret, "secret_password_lookup_sync"));
    api.clear = reinterpret_cast<ClearFn>(dlsym(secret, "secret_password_clear_sync"));
    api.password_free = reinterpret_cast<PasswordFreeFn>(dlsym(secret, "secret_password_free"));
    api.error_free = reinterpret_cast<ErrorFreeFn>(dlsym(glib, "g_error_free"));
    if (!api.ready()) {
        AURORA_LOG_WARN("platform", "libsecret symbols incomplete; credentials fall back to ask-every-time");
        return {};
    }
    return api;
}

[[nodiscard]] auto api() -> const LibsecretApi & {
    static const LibsecretApi loaded = load_api();
    return loaded;
}

[[nodiscard]] auto error_text(const GErrorLite *error) -> std::string {
    return (error != nullptr && error->message != nullptr) ? std::string{error->message} : std::string{};
}

auto free_error(GErrorLite *error) -> void {
    if (error != nullptr) {
        api().error_free(error);
    }
}

class LibsecretCredentialStore final : public conn::CredentialStore {
  public:
    [[nodiscard]] auto available() const -> bool override { return api().ready(); }

    [[nodiscard]] auto store(std::string_view profile_id, std::string_view label,
                             std::string secret) -> conn::SecretHandle override {
        const std::string id{profile_id};
        const std::string caption =
            label.empty() ? std::string{"Borealis connection"} : std::string{label};
        const auto fallback = conn::SecretHandle::ask_every_time(id, caption);
        const std::string vault_id = credential_vault_id(kLibsecretVaultPrefix, profile_id);
        if (!api().ready() || vault_id.empty()) {
            return fallback;
        }

        GErrorLite *error = nullptr;
        const int ok = api().store(&kSchema, "default", caption.c_str(), secret.c_str(), nullptr,
                                   &error, "profile_id", id.c_str(), nullptr);
        if (ok == 0) {
            // 只带档案 id 与库错误文案；秘密本身绝不进日志。
            AURORA_LOG_WARN("platform", "libsecret store failed for " + id + ": " + error_text(error));
            free_error(error);
            return fallback;
        }
        free_error(error);
        return conn::SecretHandle::reference(vault_id);
    }

    [[nodiscard]] auto retrieve(const conn::SecretHandle &handle)
        -> std::optional<std::string> override {
        if (!api().ready() || handle.kind != conn::SecretKind::Reference) {
            return std::nullopt;
        }
        const auto profile = credential_vault_profile(handle.vault_id, kLibsecretVaultPrefix);
        if (!profile) {
            return std::nullopt;
        }

        GErrorLite *error = nullptr;
        char *password = api().lookup(&kSchema, nullptr, &error, "profile_id", profile->c_str(), nullptr);
        if (password == nullptr) {
            // 未命中通常不带 GError；带错误才是真失败。空口令命中回非空指针的空串（F5）。
            if (error != nullptr) {
                AURORA_LOG_WARN("platform",
                                "libsecret lookup failed for " + *profile + ": " + error_text(error));
                free_error(error);
            }
            return std::nullopt;
        }
        std::string out{password};
        api().password_free(password);
        return out;
    }

    [[nodiscard]] auto erase(const conn::SecretHandle &handle) -> bool override {
        if (!api().ready() || handle.kind != conn::SecretKind::Reference) {
            return false;
        }
        const auto profile = credential_vault_profile(handle.vault_id, kLibsecretVaultPrefix);
        if (!profile) {
            return false;
        }

        GErrorLite *error = nullptr;
        const int ok = api().clear(&kSchema, nullptr, &error, "profile_id", profile->c_str(), nullptr);
        if (ok == 0) {
            // 实证：libsecret 0.21.7 对「无匹配项」回 FALSE 且**不设 GError**（一次性探针）；
            // 那是「库里已无此条」，按 erase 契约算成功。带 GError 才是真失败。
            if (error == nullptr) {
                return true;
            }
            AURORA_LOG_WARN("platform",
                            "libsecret clear failed for " + *profile + ": " + error_text(error));
            free_error(error);
            return false;
        }
        free_error(error);
        return true;
    }
};

}  // namespace

auto make_credential_store() -> std::unique_ptr<conn::CredentialStore> {
    if (!api().ready()) {
        return std::make_unique<conn::UnavailableCredentialStore>();
    }
    return std::make_unique<LibsecretCredentialStore>();
}

auto credential_vault_prefix() -> std::string_view { return kLibsecretVaultPrefix; }

}  // namespace borealis::platform
