/// 测试类型: unit
/// 目标单元: src/conn/key_model.{h,cpp}
/// 测试说明: SPEC.FEAT.CONN.10 批 1 的纯逻辑层（稿 §6 给这一件的判据＝「校验/目录表规范化/
///           碰撞/判重/公钥拼装」，无头、无 IO）：线名字面量、行序（唯一键＝全路径）、
///           目录表规范化（去重、`~/.ssh` 恒在表首且不可移、不可达只记载不删）、
///           名称与注释校验的字符/长度档、同名碰撞（含「无主公钥也占名」那一半）、
///           `authorized_keys` 判重（忽略注释与选项前缀）与追加计划（末行无换行时前置分隔符）。
///           可达性经**注入的判定函数**给出——本件不许 stat，这条本身就是被测事实。

#include "conn/key_model.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_keys_model {

namespace {

using namespace borealis;
using conn::KeyCandidate;
using conn::KeyDirEntry;

[[nodiscard]] auto row(std::string path, std::string basename, conn::KeyType type = conn::KeyType::Ed25519,
                       bool has_public = true) -> KeyCandidate {
    auto out = KeyCandidate{};
    out.path = std::move(path);
    out.basename = std::move(basename);
    out.type = type;
    out.has_public = has_public;
    if (has_public) {
        out.public_path = out.path + ".pub";
    }
    return out;
}

/// @brief 造一份「除某目录外都可达」的判定，可达性由调用方注入而非本件自查。
[[nodiscard]] auto probe_except(std::vector<std::string> unreachable) -> std::function<bool(std::string_view)> {
    return [unreachable = std::move(unreachable)](std::string_view path) {
        return std::find(unreachable.begin(), unreachable.end(), std::string{path}) == unreachable.end();
    };
}

[[nodiscard]] auto paths_of(const std::vector<KeyDirEntry> &dirs) -> std::vector<std::string> {
    auto out = std::vector<std::string>{};
    for (const auto &entry : dirs) {
        out.push_back(entry.path);
    }
    return out;
}

constexpr std::string_view kHome{"/home/dev"};
constexpr std::string_view kSshDir{"/home/dev/.ssh"};

}  // namespace

AURORA_TEST_CASE(wire_names_are_the_openssh_literals_and_unknown_produces_nothing) {
    // 这两个串是线上格式而非词条：写错了 `.pub` 与 `authorized_keys` 直接不可用。
    AURORA_TEST_CHECK_MSG(conn::key_wire_name(conn::KeyType::Ed25519) == "ssh-ed25519", "ed25519 line name");
    AURORA_TEST_CHECK_MSG(conn::key_wire_name(conn::KeyType::Rsa) == "ssh-rsa", "rsa line name");
    AURORA_TEST_CHECK_MSG(conn::key_wire_name(conn::KeyType::Unknown).empty(), "unknown yields empty");

    AURORA_TEST_CHECK_TRUE(conn::rsa_bits_is_supported(2048));
    AURORA_TEST_CHECK_TRUE(conn::rsa_bits_is_supported(3072));
    AURORA_TEST_CHECK_TRUE(conn::rsa_bits_is_supported(4096));
    // 档位之外的数（含 4095 这类「差一位」）不收：下拉之外的手填路径也走同一道闸。
    AURORA_TEST_CHECK_FALSE(conn::rsa_bits_is_supported(1024));
    AURORA_TEST_CHECK_FALSE(conn::rsa_bits_is_supported(4095));
    AURORA_TEST_CHECK_FALSE(conn::rsa_bits_is_supported(0));
}

AURORA_TEST_CASE(row_order_follows_the_dir_table_then_basename) {
    const auto dirs = std::vector<std::string>{std::string{kSshDir}, "/srv/keys"};
    auto rows = std::vector<KeyCandidate>{
        row("/srv/keys/b_key", "b_key"),
        row("/home/dev/.ssh/zeta", "zeta"),
        row("/home/dev/.ssh/alpha", "alpha"),
        row("/mnt/usb/forgot", "forgot"),  // 表外行：来自上一次快照，排最后但不许丢
    };
    static_cast<void>(conn::sort_rows(rows, dirs));

    AURORA_TEST_REQUIRE_EQ(rows.size(), 4U);
    AURORA_TEST_CHECK_MSG(rows[0].path == "/home/dev/.ssh/alpha", "home dir first, then basename order");
    AURORA_TEST_CHECK_MSG(rows[1].path == "/home/dev/.ssh/zeta", "second home row");
    AURORA_TEST_CHECK_MSG(rows[2].path == "/srv/keys/b_key", "second table dir");
    AURORA_TEST_CHECK_MSG(rows[3].path == "/mnt/usb/forgot", "off-table row lands last, never dropped");
}

AURORA_TEST_CASE(dir_table_is_deduped_and_home_is_pinned_first) {
    const auto raw = std::vector<std::string>{
        "/srv/keys",
        std::string{kSshDir},  // 用户把 `~/.ssh` 也写进过表：并进首条，不许多出一条
        "/srv/keys",           // 重复项只留第一次出现
        "",                    // 空串不是目录
        "  /opt/vaults  ",     // 首尾空白是录入噪声，裁掉
    };
    const auto dirs = conn::normalize_key_dirs(raw, kHome);

    AURORA_TEST_REQUIRE_EQ(dirs.size(), 3U);
    AURORA_TEST_CHECK_MSG((paths_of(dirs) == std::vector<std::string>{"/home/dev/.ssh", "/srv/keys", "/opt/vaults"}),
                          "home first then user order");
    AURORA_TEST_CHECK_TRUE(dirs[0].is_home_dir);
    AURORA_TEST_CHECK_FALSE(dirs[1].is_home_dir);
    AURORA_TEST_CHECK_TRUE(dirs[0].reachable);  // 未注入判定时全算可达
}

AURORA_TEST_CASE(an_empty_home_leaves_the_table_exactly_as_the_user_wrote_it) {
    // 拿不到 HOME 的极端现场不凭空造扫面：宁可按用户表跑，也不扫一个猜出来的目录。
    const auto raw = std::vector<std::string>{"/srv/keys"};
    const auto dirs = conn::normalize_key_dirs(raw, {});
    AURORA_TEST_REQUIRE_EQ(dirs.size(), 1U);
    AURORA_TEST_CHECK_FALSE(dirs[0].is_home_dir);
}

AURORA_TEST_CASE(unreachable_dirs_are_recorded_not_deleted) {
    // D2③ 细则⑶：不可达目录**保留条目并行内留痕**——装载/规范化时偷偷删掉用户录入的东西，
    // 挂载盘一掉线目录表就永久少一条，那是比「扫不到」更坏的失败。
    const auto raw = std::vector<std::string>{"/srv/gone", "/srv/here"};
    const auto dirs = conn::normalize_key_dirs(raw, kHome, probe_except({"/srv/gone"}));
    AURORA_TEST_REQUIRE_EQ(dirs.size(), 3U);
    AURORA_TEST_CHECK_MSG((paths_of(dirs) == std::vector<std::string>{"/home/dev/.ssh", "/srv/gone", "/srv/here"}),
                          "the dead entry is still in the table");
    AURORA_TEST_CHECK_FALSE(dirs[1].reachable);
    AURORA_TEST_CHECK_TRUE(dirs[2].reachable);
}

AURORA_TEST_CASE(the_home_entry_cannot_be_removed_but_the_others_can) {
    const auto raw = std::vector<std::string>{"/srv/keys"};
    const auto dirs = conn::normalize_key_dirs(raw, kHome);
    AURORA_TEST_REQUIRE_EQ(dirs.size(), 2U);

    // 「~/.ssh 不可移」有独立判据：试图移除后表内仍在，且**表本身不变**（不留残缺表）。
    const auto refused = conn::without_dir(dirs, std::string{kSshDir});
    AURORA_TEST_CHECK_EQ(refused.size(), dirs.size());
    AURORA_TEST_CHECK_TRUE(conn::without_dir(refused, std::string{kSshDir})[0].is_home_dir);

    const auto removed = conn::without_dir(dirs, "/srv/keys");
    AURORA_TEST_REQUIRE_EQ(removed.size(), 1U);
    AURORA_TEST_CHECK_MSG(removed[0].path == "/home/dev/.ssh", "only the user dir is gone");

    // 表里没有的路径：原表照回，不报错也不误删别条。
    AURORA_TEST_CHECK_EQ(conn::without_dir(dirs, "/not/in/table").size(), dirs.size());
}

AURORA_TEST_CASE(key_names_are_checked_for_shape_not_for_existence) {
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("id_ed25519")),
                         static_cast<int>(conn::NameIssue::None));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("deploy-key.2")),
                         static_cast<int>(conn::NameIssue::None));
    // CJK-LITERAL: cjk-fixture - 「非 ASCII 的名字是合法名」这条被测事实换成英文即消失
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("公司跳板")),
                         static_cast<int>(conn::NameIssue::None));

    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name({})),
                         static_cast<int>(conn::NameIssue::Empty));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name(std::string(conn::kMaxKeyNameLength + 1U, 'k'))),
                         static_cast<int>(conn::NameIssue::TooLong));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name(std::string(conn::kMaxKeyNameLength, 'k'))),
                         static_cast<int>(conn::NameIssue::None));
    // 带分隔符＝越出所选目录；控制字符会毁掉后续一切按名字拼路径的地方。
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("../escape")),
                         static_cast<int>(conn::NameIssue::HasSeparator));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("sub/dir")),
                         static_cast<int>(conn::NameIssue::HasSeparator));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("win\\path")),
                         static_cast<int>(conn::NameIssue::HasSeparator));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("bad\tname")),
                         static_cast<int>(conn::NameIssue::HasControlChar));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("bad\nname")),
                         static_cast<int>(conn::NameIssue::HasControlChar));
    // 点开头＝隐藏文件：扫盘与 `ls` 都看不见它，用户会得到一份不在列表里的私钥。
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name(".hidden")),
                         static_cast<int>(conn::NameIssue::StartsDot));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name(".")),
                         static_cast<int>(conn::NameIssue::Reserved));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_key_name("..")),
                         static_cast<int>(conn::NameIssue::Reserved));
}

AURORA_TEST_CASE(comments_allow_spaces_and_reject_only_control_chars) {
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_comment("deploy key for jump host")),
                         static_cast<int>(conn::CommentIssue::None));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_comment({})),
                         static_cast<int>(conn::CommentIssue::None));  // 空注释合法（F7 的 user@host 或不填）
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_comment(std::string(conn::kMaxCommentLength + 1U, 'c'))),
                         static_cast<int>(conn::CommentIssue::TooLong));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_comment("two\nlines")),
                         static_cast<int>(conn::CommentIssue::HasControlChar));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_comment("tab\there")),
                         static_cast<int>(conn::CommentIssue::HasControlChar));
}

AURORA_TEST_CASE(collision_is_judged_per_directory_and_a_lone_pub_is_outside_this_view) {
    const auto rows = std::vector<KeyCandidate>{
        row("/home/dev/.ssh/id_rsa", "id_rsa", conn::KeyType::Rsa),
        row("/srv/keys/id_ed25519", "id_ed25519"),
        row("/home/dev/.ssh/legacy", "legacy", conn::KeyType::Rsa, /*has_public=*/false),
    };
    // 唯一键＝全路径（D2③ 细则⑷）：另一个目录里的同名不算撞。
    AURORA_TEST_CHECK_FALSE(conn::would_collide_with_rows(rows, "/srv/keys", "id_rsa"));
    AURORA_TEST_CHECK_TRUE(conn::would_collide_with_rows(rows, "/home/dev/.ssh", "id_rsa"));
    // 目录段带不带尾分隔符都得算同一目录（表里写的与面板拼出来的两种形态）。
    AURORA_TEST_CHECK_TRUE(conn::would_collide_with_rows(rows, "/home/dev/.ssh/", "id_rsa"));
    // 孤儿私钥行（无配对 `.pub`）也照样占名：名字冲突看的是私钥行，不是配对是否完整。
    AURORA_TEST_CHECK_TRUE(conn::would_collide_with_rows(rows, "/home/dev/.ssh", "legacy"));
    // 空名不是「不碰撞」，是压根不能生成。
    AURORA_TEST_CHECK_TRUE(conn::would_collide_with_rows(rows, "/srv/keys", std::string{}));
    AURORA_TEST_CHECK_FALSE(conn::would_collide_with_rows(rows, "/srv/keys", "fresh_key"));
    // **本件的边界**：盘上若只有一份无主 `stray.pub`（没有私钥），它不成行、这里就看不见，
    // 于是回假。那一半由传输腿的盘上存在性检查兜住（`key_store`，D5① 第三闸）——
    // 判据留在这里是为了让「模型看不见」这件事本身可证，而不是让它悄悄溜进实现。
    AURORA_TEST_CHECK_FALSE(conn::would_collide_with_rows(rows, "/srv/keys", "stray"));
}

AURORA_TEST_CASE(duplicate_basenames_are_each_reported_once) {
    const auto rows = std::vector<KeyCandidate>{
        row("/home/dev/.ssh/id_ed25519", "id_ed25519"),
        row("/srv/keys/id_ed25519", "id_ed25519"),
        row("/opt/vaults/id_ed25519", "id_ed25519"),
        row("/home/dev/.ssh/only_here", "only_here"),
    };
    const auto dups = conn::duplicate_basenames(rows);
    // 三个目录撞同一个名，也只报一次——面板要的是「哪些名字需要看路径」，不是计数。
    AURORA_TEST_CHECK_MSG((dups == std::vector<std::string>{"id_ed25519"}), "one entry per colliding name");
    AURORA_TEST_CHECK_TRUE(conn::duplicate_basenames({rows[3]}).empty());
}

AURORA_TEST_CASE(public_line_is_one_line_with_no_trailing_newline) {
    const auto line = conn::public_line("ssh-ed25519", "AAAAC3NzaC1lZDI1NTE5", "laptop key");
    AURORA_TEST_CHECK_MSG(line == "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5 laptop key", "three fields, one line");
    AURORA_TEST_CHECK_MSG(line.back() != '\n', "no trailing newline (judgement 6 copy leg)");

    // 注释空 ⇒ 两字段，**不留尾随空格**。
    AURORA_TEST_CHECK_MSG(conn::public_line("ssh-rsa", "AAAAB3", {}) == "ssh-rsa AAAAB3", "no dangling space");
    AURORA_TEST_CHECK_MSG(conn::public_line("ssh-rsa", "AAAAB3", "  padded  ") == "ssh-rsa AAAAB3 padded",
                          "comment ends are trimmed");
    // 废行不产出：线名/base64 为空或夹空白、注释带控制字符，都回空串让调用方报错。
    AURORA_TEST_CHECK_TRUE(conn::public_line({}, "AAA", "c").empty());
    AURORA_TEST_CHECK_TRUE(conn::public_line("ssh-ed25519", {}, "c").empty());
    AURORA_TEST_CHECK_TRUE(conn::public_line("ssh ed25519", "AAA", "c").empty());
    AURORA_TEST_CHECK_TRUE(conn::public_line("ssh-ed25519", "AA A", "c").empty());
    AURORA_TEST_CHECK_TRUE(conn::public_line("ssh-ed25519", "AAA", "bad\ncomment").empty());
}

AURORA_TEST_CASE(authorized_lookup_ignores_the_comment_and_option_prefixes) {
    const auto blob = std::string_view{"AAAAC3NzaC1lZDI1NTE5AAAAIExample"};
    const auto wire = std::string_view{"ssh-ed25519"};

    AURORA_TEST_CHECK_TRUE(conn::is_authorized(std::string{"ssh-ed25519 "} + std::string{blob} + " old comment\n", wire, blob));
    // 带选项前缀的行（`from="…"`）授权的是同一把钥匙，必须算命中。
    AURORA_TEST_CHECK_TRUE(conn::is_authorized("from=\"10.0.0.7\" ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample forced\n",
                                         wire, blob));
    // 同一 blob 换了算法族不算（判据要能区分「同一把钥匙」与「恰好同一段 base64」）。
    AURORA_TEST_CHECK_FALSE(conn::is_authorized("ssh-rsa AAAAC3NzaC1lZDI1NTE5AAAAIExample\n", wire, blob));
    // 独立注释行、只有半行的行、别的钥匙，都不算命中。
    AURORA_TEST_CHECK_FALSE(conn::is_authorized("# a note mentioning ssh-ed25519\n", wire, blob));
    AURORA_TEST_CHECK_FALSE(conn::is_authorized("ssh-ed25519 AAAAother\n", wire, blob));
    AURORA_TEST_CHECK_FALSE(conn::is_authorized({}, wire, blob));
    // 末行没有换行也要能命中（远端手写过一半的行是常态）。
    AURORA_TEST_CHECK_TRUE(conn::is_authorized("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample", wire, blob));
    // 空线名/空 blob 判不出「已授权」：宁可回假，让追加照常走。
    AURORA_TEST_CHECK_FALSE(conn::is_authorized("ssh-ed25519 AAAA\n", {}, blob));
    AURORA_TEST_CHECK_FALSE(conn::is_authorized("ssh-ed25519 AAAA\n", wire, {}));
}

AURORA_TEST_CASE(append_plan_protects_the_last_remote_line_and_carries_its_own_newline) {
    const auto line = std::string{"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample laptop"};

    const auto fresh = conn::plan_append({}, line);
    AURORA_TEST_CHECK_FALSE(fresh.already_authorized);
    // 追加写**自带尾随换行**（与剪贴板那条相反）：否则下一个客户端又会粘上来。
    AURORA_TEST_CHECK_MSG(fresh.payload == line + "\n", "new file: just the line, newline-terminated");

    const auto tidy = conn::plan_append(line + "\n", "ssh-rsa AAAAB3NzaC1l new");
    AURORA_TEST_CHECK_FALSE(tidy.already_authorized);
    AURORA_TEST_CHECK_MSG(tidy.payload == "ssh-rsa AAAAB3NzaC1l new\n", "well-formed file: no extra blank line");

    // 末行无换行 ⇒ 前置一个换行。少这一步就是把两把钥匙粘成两把废钥匙。
    const auto glued = conn::plan_append("ssh-rsa AAAAB3NzaC1l", line);
    AURORA_TEST_CHECK_MSG(glued.payload.front() == '\n', "separator newline prepended");
    AURORA_TEST_CHECK_MSG(glued.payload == "\n" + line + "\n", "exactly one newline on each side");

    // 已授权 ⇒ 不发写（D7① 的本地判重），`line` 仍留给「已在授权表里」那句留痕。
    const auto hit = conn::plan_append("from=\"10.0.0.7\" " + line + "\n", line);
    AURORA_TEST_CHECK_TRUE(hit.already_authorized);
    AURORA_TEST_CHECK_MSG(hit.payload.empty(), "nothing to write");
    AURORA_TEST_CHECK_MSG(hit.line == line, "line kept for the notice");

    // 不成行的输入既不判重也不发写：payload 空＝不发写。
    const auto junk = conn::plan_append({}, "only-one-field");
    AURORA_TEST_CHECK_FALSE(junk.already_authorized);
    AURORA_TEST_CHECK_MSG(junk.payload.empty(), "a malformed line never reaches the wire");
    AURORA_TEST_CHECK_MSG(conn::plan_append({}, {}).payload.empty(), "empty line is not a key");
}

}  // namespace borealis::test_cases::utest_keys_model
