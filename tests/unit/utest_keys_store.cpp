/// 测试类型: unit
/// 目标单元: src/conn/key_store.{h,cpp}（SPEC.FEAT.CONN.10 批 2a，裁决 7.106 D2/D3/D5/D12）
/// 测试说明: 传输腿的四件事全在**真盘**上，故用例经 `isolation::temp_dir()` 搭密钥目录夹具，
///           并真调 libssh 的 PKI 入口（ed25519 生成 <1 ms、RSA-2048 72 ms，稿 §0 的 F3 读数，
///           无头可跑）。每条判据各自对着一条裁决或一条实测读数：D5① 的三道闸看权限与
///           「原字节还在」，F13 看「无口令必须折成 nullptr」，F8 看加密态探测，
///           D12 看删除的三条边界，D2①/③ 看成行与跳过的那几条。全部不碰 UI、不碰网络。

#include "conn/key_store.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_keys_store {

namespace {

using namespace borealis;

[[nodiscard]] auto fixture_root() -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()};
}

/// @brief 夹具目录（一用例一枚名字，两批文件不混）。
[[nodiscard]] auto fixture_dir(std::string_view name) -> std::filesystem::path {
    auto path = fixture_root() / std::string{name};
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

auto write_text(const std::filesystem::path &path, std::string_view text) -> void {
    auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string {
    auto in = std::ifstream{path, std::ios::binary};
    if (!in.is_open()) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 一次 ed25519 生成的最小请求（其余字段按缺省：无口令、无注释、位数 0）。
[[nodiscard]] auto ed_request(const std::filesystem::path &directory, std::string_view basename)
    -> conn::GenerateRequest {
    auto request = conn::GenerateRequest{};
    request.directory = directory.string();
    request.basename = std::string{basename};
    return request;
}

/// @brief 断言「就这一行」并交出它（夹具里种几份文件是用例自己写死的，多一行就是判据变了）。
[[nodiscard]] auto only_row(const std::vector<conn::KeyCandidate> &rows) -> conn::KeyCandidate {
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    return rows.front();
}

/// @brief 目录里的条目总数：临时件残留、公钥多余一份都在这里露形。
[[nodiscard]] auto count_entries(const std::filesystem::path &directory) -> std::size_t {
    std::size_t total = 0U;
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto &entry : std::filesystem::directory_iterator{directory, options, ec}) {
        static_cast<void>(entry);
        ++total;
    }
    return total;
}

/// @brief 本目录扫一趟（`~/.ssh` 那条例外不在本件，夹具就是普通目录）。
[[nodiscard]] auto scan_of(const std::filesystem::path &directory) -> std::vector<conn::KeyCandidate> {
    auto dirs = std::vector<conn::KeyDirEntry>{};
    dirs.emplace_back(directory.string(), false, true);
    return conn::scan_keys(dirs);
}

constexpr std::string_view kFingerprintPrefix = "SHA256:";

[[nodiscard]] auto starts_with_fingerprint(const conn::KeyCandidate &row) -> bool {
    return row.fingerprint.starts_with(kFingerprintPrefix);
}

}  // namespace

AURORA_TEST_CASE(generated_ed25519_pair_lands_as_one_row_with_a_fingerprint) {
    const auto directory = fixture_dir("gen_ed");
    auto request = ed_request(directory, "id desk");
    request.comment = "borealis test key";

    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(request)),
                         static_cast<int>(conn::GenerateOutcome::Created));
    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_STREQ(row.basename, "id desk");
    AURORA_TEST_CHECK_EQ(static_cast<int>(row.type), static_cast<int>(conn::KeyType::Ed25519));
    AURORA_TEST_CHECK_EQ(static_cast<int>(row.bits), 0);  // ed25519 无位数概念
    AURORA_TEST_CHECK_TRUE(row.has_public);
    AURORA_TEST_CHECK_STREQ(row.public_path, (directory / "id desk.pub").string());
    AURORA_TEST_CHECK_TRUE(!row.encrypted);
    AURORA_TEST_CHECK_STREQ(row.comment, "borealis test key");
    AURORA_TEST_CHECK_TRUE(starts_with_fingerprint(row));
    // 唯一键＝全路径（D2③ 细则⑷）：行表要能还原出盘上那两份文件。
    AURORA_TEST_CHECK_STREQ(row.path, (directory / "id desk").string());
}

AURORA_TEST_CASE(the_row_carries_the_public_base64_the_copy_action_hands_over) {
    // 批 3 补字段（裁决 7.109）：判据 4「一键复制」发生在点击回调里，现场读 `.pub` 就是回调中的
    // 同步 IO（AGENTS §4.5 第 25 条）——base64 由扫盘一次带出，行表自己就是可复制字节的来源。
    // 变异注入自证非空转：删掉 `row_of_private_key` 里 `row.public_base64 = fields->base64` 那行，
    // 下面的「行内重建＝盘上一行」当场红。
    const auto directory = fixture_dir("gen_b64");
    auto request = ed_request(directory, "id_b64");
    request.comment = "desk key";
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(request)),
                         static_cast<int>(conn::GenerateOutcome::Created));
    const auto row = only_row(scan_of(directory));

    const auto file_line = read_text(directory / "id_b64.pub");
    AURORA_TEST_REQUIRE(file_line.ends_with("\n"));
    const auto body = file_line.substr(0U, file_line.size() - 1U);
    const auto first_blank = body.find(' ');
    AURORA_TEST_REQUIRE(first_blank != std::string::npos);
    const auto second_blank = body.find(' ', first_blank + 1U);
    AURORA_TEST_REQUIRE_MSG(second_blank != std::string::npos, "comment field present in fixture");
    AURORA_TEST_CHECK_STREQ(row.public_base64, body.substr(first_blank + 1U, second_blank - first_blank - 1U));
    // 复制腿据此重建：`public_line(线名, base64, 注释)` 的产物要与盘上 `.pub` 那一行逐字节一致。
    AURORA_TEST_CHECK_STREQ(conn::public_line(conn::key_wire_name(row.type), row.public_base64, row.comment),
                            body);

    // 取不出 base64 的那一档（带口令又无 `.pub`）留空串：面板据此把复制/推送整枚不画。
    std::error_code ec;
    std::filesystem::remove(directory / "id_b64", ec);
    std::filesystem::remove(directory / "id_b64.pub", ec);
    auto locked = ed_request(directory, "id_b64_locked");
    locked.passphrase = std::string{"pp"};
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(locked)) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    std::filesystem::remove(directory / "id_b64_locked.pub", ec);
    const auto locked_row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_TRUE(locked_row.encrypted);
    AURORA_TEST_CHECK_TRUE(!locked_row.has_public);
    AURORA_TEST_CHECK_STREQ(locked_row.public_base64, "");
}

AURORA_TEST_CASE(the_private_key_is_owner_only_and_no_scratch_file_survives) {
    // D5① 三道闸的读数点：0600（F6 的教训——libssh 自己写会随 umask 落成组可读）、
    // 就位后不留临时件（rename 那条路）、`.pub` 是一行三字段且以换行终止（判据 §6 的复制形态）。
    const auto directory = fixture_dir("gen_perms");
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(ed_request(directory, "id_ed"))),
                         static_cast<int>(conn::GenerateOutcome::Created));

#if !defined(_WIN32)
    const auto permissions = std::filesystem::status(directory / "id_ed").permissions();
    const auto world_bits = permissions & (std::filesystem::perms::group_all |
                                           std::filesystem::perms::others_all);
    // 去掉 create_private_file 那条腿（让 libssh 直接写目标路径）在这里当场红：
    // 本机 umask 之下它落成组可读，而 OpenSSH 会拒用同组可写的私钥。
    AURORA_TEST_CHECK_MSG(world_bits == std::filesystem::perms::none, "private key is owner-only");
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_read) !=
                              std::filesystem::perms::none,
                          "owner can read it back");
#endif
    // 临时件与密钥本体一起数：只有私钥 + `.pub` 两份，说明 `.id_ed.borealis-tmp` 已随 rename 消失。
    AURORA_TEST_CHECK_EQ(count_entries(directory), 2U);

    const auto public_text = read_text(directory / "id_ed.pub");
    AURORA_TEST_CHECK_MSG(public_text.ends_with("\n"), "public line is newline terminated");
    AURORA_TEST_CHECK_MSG(public_text.starts_with("ssh-ed25519 "), "wire name first");
    auto newlines = std::size_t{0U};
    for (const char c : public_text) {
        newlines += c == '\n' ? 1U : 0U;
    }
    AURORA_TEST_CHECK_EQ(newlines, 1U);  // 单行：注释里塞进换行会把公钥劈成两半
}

AURORA_TEST_CASE(generation_never_overwrites_a_key_or_a_stray_public_half) {
    // D5① 第一闸 + 裁决 7.107 ④ 的盘上一半：行表看不见孤 `.pub`（key_model 那侧的这条死码已删），
    // 故「无主公钥也占名」只能在这里证。
    const auto directory = fixture_dir("gen_overwrite");
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(ed_request(directory, "id_keep"))),
                         static_cast<int>(conn::GenerateOutcome::Created));
    const auto precious = read_text(directory / "id_keep");

    auto again = ed_request(directory, "id_keep");
    again.passphrase = std::string{"another passphrase"};
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(again)),
                         static_cast<int>(conn::GenerateOutcome::NameTaken));
    AURORA_TEST_CHECK_MSG(read_text(directory / "id_keep") == precious, "existing key bytes intact");
    AURORA_TEST_CHECK_EQ(count_entries(directory), 2U);  // 连临时件都没留下

    // 孤 `.pub`：没有对应私钥、列表里没有那一行，但名字在盘上就是被占了。
    write_text(directory / "stray.pub", "ssh-ed25519 AAAAB3NzaC1yc2E= orphan\n");
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(ed_request(directory, "stray"))),
                         static_cast<int>(conn::GenerateOutcome::NameTaken));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(directory / "stray"));
}

AURORA_TEST_CASE(rsa_generation_carries_the_requested_size_into_the_row) {
    // 稿 §2 判据 2 的那枚「rsa 2048」徽标：位数的唯一来源是本仓自己解公钥 blob 的 mpint n
    //（F12：libssh 0.12 公共头没有位数访问器）。下拉档位与行里的读数必须相等，否则下拉是摆设。
    const auto directory = fixture_dir("gen_rsa");
    auto request = ed_request(directory, "id_rsa2k");
    request.type = conn::KeyType::Rsa;
    request.rsa_bits = static_cast<std::uint16_t>(conn::RsaBits::Bits2048);
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(request)),
                         static_cast<int>(conn::GenerateOutcome::Created));

    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_EQ(static_cast<int>(row.type), static_cast<int>(conn::KeyType::Rsa));
    AURORA_TEST_CHECK_EQ(static_cast<int>(row.bits), 2048);
    AURORA_TEST_CHECK_TRUE(starts_with_fingerprint(row));

    // 档位之外的数（1024 太弱）一律 InvalidRequest，且不碰盘。
    auto off_grid = ed_request(directory, "id_weak");
    off_grid.type = conn::KeyType::Rsa;
    off_grid.rsa_bits = 1024U;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(off_grid)),
                         static_cast<int>(conn::GenerateOutcome::InvalidRequest));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(directory / "id_weak"));
}

AURORA_TEST_CASE(the_request_gates_run_before_any_disk_exists) {
    // 「未碰盘」是 InvalidRequest 那一档的定义而不是修辞：闸没过时连目标目录都不该被建出来
    //（实现里 create_directories 排在闸之后，这条把顺序钉住）。
    const auto missing = fixture_root() / "gate_dir_not_created";
    std::error_code ec;
    std::filesystem::remove_all(missing, ec);

    auto through_dir = ed_request(missing, "bad/name");  // 名字里带分隔符＝越出所选目录
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(through_dir)),
                         static_cast<int>(conn::GenerateOutcome::InvalidRequest));

    auto long_comment = ed_request(missing, "id_ok");
    long_comment.comment = std::string(conn::kMaxCommentLength + 1U, 'x');
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(long_comment)),
                         static_cast<int>(conn::GenerateOutcome::InvalidRequest));

    auto hidden = ed_request(missing, ".invisible");  // StartsDot：生成的私钥会成隐藏文件
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(hidden)),
                         static_cast<int>(conn::GenerateOutcome::InvalidRequest));

    auto unknown_type = ed_request(missing, "id_ok");
    unknown_type.type = conn::KeyType::Unknown;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(unknown_type)),
                         static_cast<int>(conn::GenerateOutcome::InvalidRequest));

    AURORA_TEST_CHECK_MSG(!std::filesystem::exists(missing),
                          "no directory created for a refused request");
}

AURORA_TEST_CASE(a_passphrase_protected_key_is_marked_without_being_decrypted) {
    // F8 + D3①：一次 `import_privkey_file(…, nullptr, …)` 就判出「有没有口令」，且这是探测不是解密。
    // F13 的另一半不在这里而在上一个无口令用例：无口令时传空串被 libssh 拒写（rc=-1 只留 0 字节文件），
    // 故实现把 nullopt 与空串一并折成 nullptr——删掉那句折叠，`generated_ed25519_…` 即红。
    const auto directory = fixture_dir("gen_pass");
    auto request = ed_request(directory, "id_locked");
    request.passphrase = std::string{"correct horse"};
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(request)),
                         static_cast<int>(conn::GenerateOutcome::Created));

    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_TRUE(row.encrypted);
    AURORA_TEST_CHECK_TRUE(row.has_public);  // 公钥半照旧写得出去，类型与指纹都从它取
    AURORA_TEST_CHECK_TRUE(starts_with_fingerprint(row));
    AURORA_TEST_CHECK_EQ(static_cast<int>(row.type), static_cast<int>(conn::KeyType::Ed25519));
    const auto private_text = read_text(directory / "id_locked");
    AURORA_TEST_CHECK_MSG(private_text.find("-----BEGIN OPENSSH PRIVATE KEY") != std::string::npos,
                          "openssh wire format");
    // 明文纪律（CONN.09）：口令不以任何形态躺在私钥文件里。
    AURORA_TEST_CHECK_MSG(private_text.find("correct horse") == std::string::npos,
                          "the passphrase is not stored in the clear");
}

AURORA_TEST_CASE(exporting_a_missing_public_half_and_refusing_to_rewrite_it) {
    // 稿 §2 判据 3 的那枚「导出公钥」动作：先造出「私钥在、公钥没了」那一态（D2① 的行源之一）。
    const auto directory = fixture_dir("export_leg");
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::generate_key(ed_request(directory, "id_exp"))),
                         static_cast<int>(conn::GenerateOutcome::Created));
    std::error_code ec;
    std::filesystem::remove(directory / "id_exp.pub", ec);

    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_TRUE(!row.has_public);
    AURORA_TEST_CHECK_STREQ(row.public_path, "");
    AURORA_TEST_CHECK_TRUE(starts_with_fingerprint(row));  // 来源⑵：私钥的公共半
    AURORA_TEST_CHECK_STREQ(row.comment, "");  // F12：没有注释访问器，无 `.pub` 就不编一个

    const auto private_path = (directory / "id_exp").string();
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::export_public_key(private_path, std::nullopt)),
                         static_cast<int>(conn::ExportOutcome::Written));
    const auto after = only_row(scan_of(directory));
    AURORA_TEST_CHECK_TRUE(after.has_public);
    AURORA_TEST_CHECK_MSG(after.fingerprint == row.fingerprint, "same key, same fingerprint");
    // 第二次导出必须是 AlreadyExists（多半是列表过期）：覆写不在本件的能力里。
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::export_public_key(private_path, std::nullopt)),
                         static_cast<int>(conn::ExportOutcome::AlreadyExists));

    // 带口令而手上没口令：与删除腿相反的一档（D12⑵ 只管删除，导出必须能验「口令对不对」）。
    auto locked = ed_request(directory, "id_locked_exp");
    locked.passphrase = std::string{"pp"};
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(locked)) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    std::filesystem::remove(directory / "id_locked_exp.pub", ec);
    const auto locked_path = (directory / "id_locked_exp").string();
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::export_public_key(locked_path, std::nullopt)),
                         static_cast<int>(conn::ExportOutcome::NeedsPassphrase));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::export_public_key(locked_path, std::string{"wrong one"})),
                         static_cast<int>(conn::ExportOutcome::Failed));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::export_public_key(locked_path, std::string{"pp"})),
                         static_cast<int>(conn::ExportOutcome::Written));
    AURORA_TEST_CHECK_TRUE(std::filesystem::exists(directory / "id_locked_exp.pub"));
}

AURORA_TEST_CASE(delete_clears_one_pair_and_needs_no_passphrase) {
    // D12⑴ 一次清一对 + D12⑵「删除不读私钥内容 ⇒ 有口令的钥匙不需要口令就能删」：本腿一次
    // import 都不做，`delete_key_pair` 压根没有口令形参，顺手加询问的实现无从在此留下痕迹。
    const auto directory = fixture_dir("delete_leg");
    auto ec = std::error_code{};
    auto request = ed_request(directory, "id_gone");
    request.passphrase = std::string{"pp"};
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(request)) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    const auto private_path = (directory / "id_gone").string();

    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::delete_key_pair(private_path)),
                         static_cast<int>(conn::DeleteOutcome::Deleted));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(directory / "id_gone"));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(directory / "id_gone.pub"));  // 不留无主公钥
    AURORA_TEST_CHECK_EQ(scan_of(directory).size(), 0U);
    // 边界⑷：再删一次是 NotFound（并发删除或列表过期），不是「成功」。
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::delete_key_pair(private_path)),
                         static_cast<int>(conn::DeleteOutcome::NotFound));

    // 孤儿私钥（本就没有 `.pub`）只删本体，同样回 Deleted。
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(ed_request(directory, "id_orphan"))) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    std::filesystem::remove(directory / "id_orphan.pub", ec);
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::delete_key_pair((directory / "id_orphan").string())),
                         static_cast<int>(conn::DeleteOutcome::Deleted));
    AURORA_TEST_CHECK_EQ(count_entries(directory), 0U);
}

#if !defined(_WIN32)
AURORA_TEST_CASE(a_symlinked_half_of_the_pair_refuses_the_whole_delete) {
    // D12⑶：「删链接」与「删目标」语义歧义，那会让列表成为指向任意文件的口子。
    // 两条路径都要过这道闸，且**动任何一物之前**先过——半删状态下链接还指着别处，更难查。
    const auto directory = fixture_dir("delete_symlink");
    const auto elsewhere = fixture_dir("elsewhere");
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(ed_request(directory, "id_link"))) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    const auto private_bytes = read_text(directory / "id_link");
    write_text(elsewhere / "victim.pub", "ssh-ed25519 AAAA somebody else\n");
    const auto victim = read_text(elsewhere / "victim.pub");
    std::error_code ec;
    std::filesystem::remove(directory / "id_link.pub", ec);
    std::filesystem::create_symlink(elsewhere / "victim.pub", directory / "id_link.pub", ec);
    AURORA_TEST_REQUIRE_MSG(!ec, "symlink built for the case");

    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::delete_key_pair((directory / "id_link").string())),
                         static_cast<int>(conn::DeleteOutcome::RefusedSymlink));
    AURORA_TEST_CHECK_MSG(read_text(directory / "id_link") == private_bytes, "the private key is intact");
    AURORA_TEST_CHECK_MSG(read_text(elsewhere / "victim.pub") == victim, "the link target is intact");
    AURORA_TEST_CHECK_MSG(std::filesystem::is_symlink(directory / "id_link.pub"),
                          "the link itself is still there");
    // 扫面对链接的口径（D2③⑶ 的另一半）：链接的 `.pub` **算在场但不读它**——那一行不显
    // 「公钥缺失」，也就不会让「导出公钥」去覆写链接指向的别人的文件；字段退回私钥公共半。
    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_TRUE(row.has_public);
    AURORA_TEST_CHECK_STREQ(row.public_path, (directory / "id_link.pub").string());
    AURORA_TEST_CHECK_STREQ(row.comment, "");
    AURORA_TEST_CHECK_TRUE(starts_with_fingerprint(row));
}

AURORA_TEST_CASE(scan_takes_no_row_from_links_orphans_hidden_or_plain_text) {
    // 成行判据的反面全在这里：符号链接不成行（D2③⑶）、孤 `.pub` 不成行（D2①）、
    // 点号开头的隐藏件不成行（生成期临时件的残留形态）、非私钥文件不成行（PEM 头嗅探）。
    const auto directory = fixture_dir("scan_shape");
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(ed_request(directory, "id_real"))) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    std::error_code ec;
    std::filesystem::create_symlink(directory / "id_real", directory / "linked", ec);
    write_text(directory / "orphan.pub", "ssh-ed25519 AAAA orphan\n");
    write_text(directory / ".hidden", read_text(directory / "id_real"));  // 整把私钥，但点号开头
    write_text(directory / "authorized_keys", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5 user@host\n");
    write_text(directory / "config", "Host example\n  User me\n");
    std::filesystem::create_directory(directory / "notes");

    const auto row = only_row(scan_of(directory));
    AURORA_TEST_CHECK_STREQ(row.basename, "id_real");
}
#endif

AURORA_TEST_CASE(rows_are_keyed_by_path_and_unreachable_dirs_are_skipped) {
    // 细则⑷的唯一键 + 细则⑶的「不可达只标不删」在扫面侧的落点：条目还在表里，这一趟跳过它。
    const auto first = fixture_dir("two_dirs_a");
    const auto second = fixture_dir("two_dirs_b");
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(ed_request(first, "id_shared"))) ==
                        static_cast<int>(conn::GenerateOutcome::Created));
    AURORA_TEST_REQUIRE(static_cast<int>(conn::generate_key(ed_request(second, "id_shared"))) ==
                        static_cast<int>(conn::GenerateOutcome::Created));

    auto dirs = std::vector<conn::KeyDirEntry>{};
    dirs.emplace_back(first.string(), true, true);
    dirs.emplace_back(second.string(), false, true);
    dirs.emplace_back((fixture_root() / "not_on_disk").string(), false, false);
    dirs.emplace_back("", false, true);

    const auto rows = conn::scan_keys(dirs);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_STREQ(rows[0].basename, "id_shared");
    AURORA_TEST_CHECK_STREQ(rows[1].basename, "id_shared");
    AURORA_TEST_CHECK_TRUE(rows[0].path != rows[1].path);  // 同 basename 的两把钥匙靠路径分得开
    AURORA_TEST_CHECK_EQ(conn::duplicate_basenames(rows).size(), 1U);
    AURORA_TEST_CHECK_TRUE(rows[0].fingerprint != rows[1].fingerprint);  // 各生成一把，不是同一把扫了两遍
}

}  // namespace borealis::test_cases::utest_keys_store
