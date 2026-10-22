/// 测试类型: e2e
/// 目标单元: src/platform/posix/credential_store.cpp（经真实 Secret Service）
/// 测试说明: `SPEC.FEAT.CONN.09` 的 POSIX 真实后端验收：在运行中的 libsecret／Secret Service 上
///           走 store→retrieve 逐字节往返（含 UTF-8 与空口令）、覆盖写、`erase` 后取不到，
///           以及跨腿句柄不命中。空口令与「未命中」必须是两条可区分的结果（设计稿 D3/F5）。
///
///           用例使用固定档案 id，并在开头先 `erase` 一次：上一次失败留下的条目会在本次跑前
///           收走，尽量不往用户钥匙环里攒垃圾。真实服务不可用（无 libsecret 运行库、无
///           gnome-keyring 会话）时本套件会红——它本来就是真后端腿，`ctest -E etest_` 默认排除。
///
///           Windows 侧由 CMake 按 `etest_posix_` 前缀过滤（文件不存在时也不会注册空套件）。

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
#include "platform/credential_store.h"

namespace borealis::test_cases::etest_libsecret_credential_store {

using borealis::conn::CredentialStore;
using borealis::conn::SecretHandle;
using borealis::conn::SecretKind;
using borealis::platform::credential_vault_id;
using borealis::platform::credential_vault_profile;
using borealis::platform::kLibsecretVaultPrefix;
using borealis::platform::make_credential_store;

namespace {

constexpr std::string_view kLabel = "Borealis CONN.09 e2e";

[[nodiscard]] auto handle_for(std::string_view profile_id) -> SecretHandle {
    return SecretHandle::reference(credential_vault_id(kLibsecretVaultPrefix, profile_id));
}

/// @brief 打开真实后端并清掉本用例上次可能遗留的条目；不可用即让用例当场红。
[[nodiscard]] auto open_store() -> std::unique_ptr<CredentialStore> {
    auto store = make_credential_store();
    AURORA_TEST_REQUIRE_MSG(store != nullptr, "make_credential_store returned null");
    AURORA_TEST_REQUIRE_MSG(store->available(),
                            "libsecret runtime symbols unavailable (no libsecret-1.so.0 / glib)");
    return store;
}

}  // namespace

AURORA_TEST_CASE(secret_round_trips_through_the_real_secret_service) {
    auto store = open_store();
    const std::string profile_id = "conn09-etest-roundtrip";
    const auto handle = handle_for(profile_id);
    AURORA_TEST_CHECK_TRUE(store->erase(handle));  // 清上次失败遗留。

    // UTF-8 口令按字节往返；句柄只含档案 id，绝不回显秘密（D2）。
    const std::string secret = "p@ss-" "\xE4\xB8\xAD\xE6\x96\x87" "-42";
    const auto stored = store->store(profile_id, kLabel, secret);
    AURORA_TEST_REQUIRE_MSG(stored.kind == SecretKind::Reference,
                            "Secret Service unavailable or locked: store fell back to ask-every-time");
    AURORA_TEST_CHECK_EQ(stored.vault_id, std::string{"libsecret:conn09-etest-roundtrip"});
    AURORA_TEST_CHECK_TRUE(stored.vault_id.find(secret) == std::string::npos);

    const auto round_trip = store->retrieve(stored);
    AURORA_TEST_REQUIRE(round_trip.has_value());
    AURORA_TEST_CHECK_EQ(*round_trip, secret);

    AURORA_TEST_CHECK_TRUE(store->erase(stored));
    AURORA_TEST_CHECK_FALSE(store->retrieve(stored).has_value());
}

AURORA_TEST_CASE(empty_secret_and_overwrite_are_both_distinct) {
    auto store = open_store();
    const std::string profile_id = "conn09-etest-overwrite";
    const auto handle = handle_for(profile_id);
    AURORA_TEST_CHECK_TRUE(store->erase(handle));

    const auto first = store->store(profile_id, kLabel, "first-value");
    AURORA_TEST_REQUIRE(first.kind == SecretKind::Reference);
    AURORA_TEST_REQUIRE(store->retrieve(first).has_value());
    AURORA_TEST_CHECK_EQ(*store->retrieve(first), std::string{"first-value"});

    // 同一档案 id 覆盖写：句柄不变，值换新。
    const auto second = store->store(profile_id, kLabel, "second-value");
    AURORA_TEST_REQUIRE(second.kind == SecretKind::Reference);
    AURORA_TEST_CHECK_EQ(second.vault_id, first.vault_id);
    AURORA_TEST_REQUIRE(store->retrieve(second).has_value());
    AURORA_TEST_CHECK_EQ(*store->retrieve(second), std::string{"second-value"});

    // 空口令是合法输入：命中回 optional{""}，与「取不到」的 nullopt 分开（D3/F5）。
    const auto empty = store->store(profile_id, kLabel, "");
    AURORA_TEST_REQUIRE(empty.kind == SecretKind::Reference);
    const auto fetched = store->retrieve(empty);
    AURORA_TEST_REQUIRE_MSG(fetched.has_value(), "empty secret was read back as a miss");
    AURORA_TEST_CHECK_TRUE(fetched->empty());

    AURORA_TEST_CHECK_TRUE(store->erase(empty));
    AURORA_TEST_CHECK_FALSE(store->retrieve(empty).has_value());
}

AURORA_TEST_CASE(foreign_or_missing_handles_do_not_touch_the_vault) {
    auto store = open_store();

    // 跨腿前缀与询问哨兵都不归本库管：取回空、删除不报成功。
    AURORA_TEST_CHECK_FALSE(store->retrieve(SecretHandle::reference("credman:conn09-etest")).has_value());
    AURORA_TEST_CHECK_FALSE(store->erase(SecretHandle::reference("credman:conn09-etest")));
    AURORA_TEST_CHECK_FALSE(store->retrieve(SecretHandle::ask_every_time("conn09-etest", "l")).has_value());
    AURORA_TEST_CHECK_FALSE(store->erase(SecretHandle::ask_every_time("conn09-etest", "l")));
    AURORA_TEST_CHECK_FALSE(store->retrieve(SecretHandle::reference("libsecret:")).has_value());

    // 从未存过的本腿句柄：取不到；删除按「库里已无此条」回成功（可重复清理）。
    const auto absent = handle_for("conn09-etest-never-stored");
    AURORA_TEST_CHECK_FALSE(store->retrieve(absent).has_value());
    AURORA_TEST_CHECK_TRUE(store->erase(absent));
    AURORA_TEST_CHECK_TRUE(store->erase(absent));
}

AURORA_TEST_CASE(vault_profile_parser_is_the_inverse_of_the_encoder) {
    // 真后端之外，编码/解析在 e2e 侧再核一次：句柄落盘后重启进程仍能解回档案 id。
    const std::string profile_id = "conn09-etest-parse:with:colons";
    const auto handle = handle_for(profile_id);
    const auto parsed = credential_vault_profile(handle.vault_id, kLibsecretVaultPrefix);
    AURORA_TEST_REQUIRE(parsed.has_value());
    AURORA_TEST_CHECK_EQ(*parsed, profile_id);
    AURORA_TEST_CHECK_FALSE(credential_vault_profile(handle.vault_id, "credman:").has_value());
}

}  // namespace borealis::test_cases::etest_libsecret_credential_store
