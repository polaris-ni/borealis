/// 测试类型: unit
/// 目标单元: src/platform/credential_store.cpp + src/platform/credential_store.h
/// 测试说明: `SPEC.FEAT.CONN.09` 的句柄编码（前缀 + 档案 id，不含秘密）与两个替身的
///           `erase` 语义。真实 OS 后端的往返由 `etest_libsecret_credential_store` 守；
///           本件不碰任何钥匙环，可在无桌面会话的沙箱里跑。

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
#include "platform/credential_store.h"

namespace borealis::test_cases::utest_credential_store {

using borealis::conn::InMemoryCredentialStore;
using borealis::conn::SecretHandle;
using borealis::conn::SecretKind;
using borealis::conn::UnavailableCredentialStore;
using borealis::platform::credential_vault_id;
using borealis::platform::credential_vault_profile;
using borealis::platform::kCredmanVaultPrefix;
using borealis::platform::kLibsecretVaultPrefix;

AURORA_TEST_CASE(vault_ids_round_trip_and_reject_foreign_input) {
    const std::string id = credential_vault_id(kLibsecretVaultPrefix, "gw.example.com");
    AURORA_TEST_CHECK_EQ(id, std::string{"libsecret:gw.example.com"});
    const auto back = credential_vault_profile(id, kLibsecretVaultPrefix);
    AURORA_TEST_REQUIRE(back.has_value());
    AURORA_TEST_CHECK_EQ(*back, std::string{"gw.example.com"});

    // 档案 id 自带冒号（ssh 导入的确定性 id）不参与分隔歧义：前缀是最长的字面前缀。
    const auto imported = credential_vault_profile(
        credential_vault_id(kCredmanVaultPrefix, "ssh-import:host"), kCredmanVaultPrefix);
    AURORA_TEST_REQUIRE(imported.has_value());
    AURORA_TEST_CHECK_EQ(*imported, std::string{"ssh-import:host"});

    // 空输入不可编码、不可解码。
    AURORA_TEST_CHECK_TRUE(credential_vault_id("", "p").empty());
    AURORA_TEST_CHECK_TRUE(credential_vault_id(kLibsecretVaultPrefix, "").empty());
    AURORA_TEST_CHECK_FALSE(credential_vault_profile("libsecret:", kLibsecretVaultPrefix).has_value());
    AURORA_TEST_CHECK_FALSE(credential_vault_profile("", kLibsecretVaultPrefix).has_value());

    // 跨腿句柄不命中：前缀不符即回空。
    AURORA_TEST_CHECK_FALSE(
        credential_vault_profile("credman:p", kLibsecretVaultPrefix).has_value());
    AURORA_TEST_CHECK_FALSE(credential_vault_profile("p", kLibsecretVaultPrefix).has_value());

    // NUL 会让 JSON 与平台 API 的 NUL 终止参数无法往返，编码两侧都拒绝。
    const std::string with_nul{"a\0b", 3};
    AURORA_TEST_CHECK_TRUE(credential_vault_id(kLibsecretVaultPrefix, with_nul).empty());
    const std::string encoded_with_nul{"libsecret:a\0b", 13};
    AURORA_TEST_CHECK_FALSE(
        credential_vault_profile(encoded_with_nul, kLibsecretVaultPrefix).has_value());
}

AURORA_TEST_CASE(memory_store_erase_covers_own_handles_only) {
    InMemoryCredentialStore store;
    const auto handle = store.store("p1", "label", "hunter2");
    AURORA_TEST_CHECK_EQ(handle.kind, SecretKind::Reference);
    AURORA_TEST_CHECK_TRUE(store.retrieve(handle).has_value());

    // 别家前缀的句柄不是本库的：不能报删除成功，更不能误删 arch 里的同 id 条目。
    AURORA_TEST_CHECK_FALSE(store.erase(SecretHandle::reference("libsecret:p1")));
    AURORA_TEST_CHECK_TRUE(store.retrieve(handle).has_value());

    AURORA_TEST_CHECK_FALSE(store.erase(SecretHandle::ask_every_time("p1", "label")));

    AURORA_TEST_CHECK_TRUE(store.erase(handle));
    AURORA_TEST_CHECK_FALSE(store.retrieve(handle).has_value());
    // 本来不存在也算「库里已无此条」：清理路径可重复呼。
    AURORA_TEST_CHECK_TRUE(store.erase(handle));
}

AURORA_TEST_CASE(unavailable_store_never_erases_or_saves) {
    UnavailableCredentialStore store;
    AURORA_TEST_CHECK_FALSE(store.available());
    const auto handle = store.store("p1", "label", "hunter2");
    AURORA_TEST_CHECK_EQ(handle.kind, SecretKind::AskEveryTime);
    AURORA_TEST_CHECK_FALSE(store.retrieve(handle).has_value());
    AURORA_TEST_CHECK_FALSE(store.erase(handle));
    AURORA_TEST_CHECK_FALSE(store.erase(SecretHandle::reference("libsecret:p1")));
}

AURORA_TEST_CASE(handle_encoding_carries_no_secret) {
    // 句柄是落盘物：它只能带档案 id，任何调用方传进来的秘密都不会出现在编码结果里。
    const std::string secret = "correct horse battery staple";
    const std::string id = credential_vault_id(kLibsecretVaultPrefix, "profile-7");
    AURORA_TEST_CHECK_TRUE(id.find(secret) == std::string::npos);
    AURORA_TEST_CHECK_EQ(id, std::string{"libsecret:profile-7"});
}

}  // namespace borealis::test_cases::utest_credential_store
