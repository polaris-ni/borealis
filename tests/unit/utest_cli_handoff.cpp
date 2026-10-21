/// 测试类型: unit
/// 目标单元: include/borealis/cli/handoff.h + src/cli/handoff.cpp
/// 测试说明: `SPEC.FEAT.INTEG.04` 单实例转交载荷的线格式——请求 / 回复往返（含 UTF-8
///           档案名与目录）、magic / version / reserved / flags / action 校验、长度与外层
///           总长的截断与尾随字节拒绝、字段超限与 NUL 拒绝。平台通道本身另由
///           `itest_instance_channel` 取证，本套件只验字节面。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "borealis/cli/handoff.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_cli_handoff {

namespace {

using borealis::cli::decode_startup_reply;
using borealis::cli::decode_startup_request;
using borealis::cli::encode_startup_reply;
using borealis::cli::encode_startup_request;
using borealis::cli::StartupAction;
using borealis::cli::StartupReply;
using borealis::cli::StartupRequest;

[[nodiscard]] auto encode_request_or_fail(const StartupRequest &request)
    -> std::vector<std::uint8_t> {
    const auto encoded = encode_startup_request(request);
    AURORA_TEST_REQUIRE_TRUE(encoded.has_value());
    return encoded.value_or(std::vector<std::uint8_t>{});
}

}  // namespace

AURORA_TEST_CASE(request_round_trips_utf8_fields_and_flags) {
    StartupRequest request;
    request.action = StartupAction::NewTab;
    request.profile = "配置";
    request.cwd = "/tmp/目录";
    request.new_window = true;

    const auto encoded = encode_startup_request(request);
    AURORA_TEST_REQUIRE_TRUE(encoded.has_value());
    const auto decoded = decode_startup_request(*encoded);
    AURORA_TEST_REQUIRE_TRUE(decoded.has_value());
    AURORA_TEST_CHECK(*decoded == request);
}

AURORA_TEST_CASE(request_rejects_bad_magic_version_reserved_and_action) {
    StartupRequest request;
    request.profile = "dev";
    const std::vector<std::uint8_t> encoded = encode_request_or_fail(request);

    auto bad_magic = encoded;
    bad_magic[0] = 'X';
    AURORA_TEST_CHECK(!decode_startup_request(bad_magic).has_value());

    auto bad_version = encoded;
    bad_version[4] = 2;
    AURORA_TEST_CHECK(!decode_startup_request(bad_version).has_value());

    auto bad_action = encoded;
    bad_action[5] = 2;
    AURORA_TEST_CHECK(!decode_startup_request(bad_action).has_value());

    auto bad_flags = encoded;
    bad_flags[6] = 0x02;
    AURORA_TEST_CHECK(!decode_startup_request(bad_flags).has_value());

    auto bad_reserved = encoded;
    bad_reserved[7] = 1;
    AURORA_TEST_CHECK(!decode_startup_request(bad_reserved).has_value());
}

AURORA_TEST_CASE(request_rejects_truncation_trailing_bytes_and_bad_lengths) {
    StartupRequest request;
    request.profile = "dev";
    request.cwd = "/tmp/work";
    const std::vector<std::uint8_t> encoded = encode_request_or_fail(request);

    auto truncated = encoded;
    truncated.pop_back();
    AURORA_TEST_CHECK(!decode_startup_request(truncated).has_value());

    auto trailing = encoded;
    trailing.push_back(0);
    AURORA_TEST_CHECK(!decode_startup_request(trailing).has_value());

    auto short_header = encoded;
    short_header.resize(8);
    AURORA_TEST_CHECK(!decode_startup_request(short_header).has_value());

    // profile_len 改大：总长必须逐字节吻合，不能按「剩余的都给 cwd」猜。
    auto bad_profile_len = encoded;
    bad_profile_len[8] = static_cast<std::uint8_t>(encoded.size());
    bad_profile_len[9] = 0;
    AURORA_TEST_CHECK(!decode_startup_request(bad_profile_len).has_value());

    auto bad_cwd_len = encoded;
    bad_cwd_len[10] = 0;
    bad_cwd_len[11] = 0;
    AURORA_TEST_CHECK(!decode_startup_request(bad_cwd_len).has_value());
}

AURORA_TEST_CASE(request_encoding_enforces_value_and_value_cap_inclusive) {
    StartupRequest at_cap;
    at_cap.profile.assign(borealis::cli::kMaxStartupValueBytes, 'a');
    at_cap.cwd.assign(borealis::cli::kMaxStartupValueBytes, 'b');
    const auto encoded = encode_startup_request(at_cap);
    AURORA_TEST_REQUIRE_TRUE(encoded.has_value());
    AURORA_TEST_CHECK(encoded->size() ==
                      12 + borealis::cli::kMaxStartupValueBytes +
                          borealis::cli::kMaxStartupValueBytes);
    const auto decoded = decode_startup_request(*encoded);
    AURORA_TEST_REQUIRE_TRUE(decoded.has_value());
    AURORA_TEST_CHECK(*decoded == at_cap);

    StartupRequest too_long;
    too_long.profile.assign(borealis::cli::kMaxStartupValueBytes + 1, 'a');
    AURORA_TEST_CHECK(!encode_startup_request(too_long).has_value());

    StartupRequest with_nul;
    with_nul.cwd = std::string{"a\0b", 3};
    AURORA_TEST_CHECK(!encode_startup_request(with_nul).has_value());
}

AURORA_TEST_CASE(reply_round_trips_success_and_ascii_error) {
    const StartupReply ok{.ok = true, .message = {}};
    const auto ok_bytes = encode_startup_reply(ok);
    AURORA_TEST_REQUIRE_TRUE(ok_bytes.has_value());
    const auto ok_decoded = decode_startup_reply(*ok_bytes);
    AURORA_TEST_REQUIRE_TRUE(ok_decoded.has_value());
    AURORA_TEST_CHECK(*ok_decoded == ok);

    const StartupReply error{.ok = false, .message = "profile not found: dev"};
    const auto error_bytes = encode_startup_reply(error);
    AURORA_TEST_REQUIRE_TRUE(error_bytes.has_value());
    const auto error_decoded = decode_startup_reply(*error_bytes);
    AURORA_TEST_REQUIRE_TRUE(error_decoded.has_value());
    AURORA_TEST_CHECK(*error_decoded == error);
}

AURORA_TEST_CASE(reply_rejects_bad_header_status_length_and_trailing) {
    const StartupReply error{.ok = false, .message = "bad request"};
    const std::vector<std::uint8_t> encoded = encode_startup_reply(error).value_or(
        std::vector<std::uint8_t>{});

    auto bad_magic = encoded;
    bad_magic[3] = 'X';
    AURORA_TEST_CHECK(!decode_startup_reply(bad_magic).has_value());

    auto bad_version = encoded;
    bad_version[4] = 2;
    AURORA_TEST_CHECK(!decode_startup_reply(bad_version).has_value());

    auto bad_status = encoded;
    bad_status[5] = 2;
    AURORA_TEST_CHECK(!decode_startup_reply(bad_status).has_value());

    auto bad_length = encoded;
    bad_length[6] = static_cast<std::uint8_t>(encoded.size());
    bad_length[7] = 0;
    AURORA_TEST_CHECK(!decode_startup_reply(bad_length).has_value());

    auto trailing = encoded;
    trailing.push_back(1);
    AURORA_TEST_CHECK(!decode_startup_reply(trailing).has_value());

    auto truncated = encoded;
    truncated.pop_back();
    AURORA_TEST_CHECK(!decode_startup_reply(truncated).has_value());

    StartupReply too_long;
    too_long.message.assign(borealis::cli::kMaxHandoffReplyBytes + 1, 'a');
    AURORA_TEST_CHECK(!encode_startup_reply(too_long).has_value());
}

}  // namespace borealis::test_cases::utest_cli_handoff
