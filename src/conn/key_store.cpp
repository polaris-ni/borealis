// ============================================================
// SSH 密钥盘传输腿实现（src/conn/key_store.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 批 2a（裁决 7.106 D2/D3/D5/D12，稿 §4 的「key_store」件）。
// libssh 只在本 TU 出现；交回 conn/key_model 的类型全是标准类型（D1① 纪律）。
//
// 本棒新增的两条实测读数（批 2 探针，与稿 §0 的 F1–F12 同法）：
//   F13 **无口令必须给 `nullptr`**：`ssh_pki_export_privkey_file_format(key, "", …)` 本机
//       回 -1 且只留下一份 0 字节文件——空串不是「没有口令」而是「空口令」，库侧走加密分支
//       后写不出来。故本腿把 `passphrase` 的 nullopt 与空串一并折成 nullptr。
//   F14 D5① 的「临时件 → 截断写入 → rename」成立：对已存在的 0600 临时件调导出函数
//       rc=0、写完权限仍是 600（`fopen` 语义下 mode 不受 umask 二次影响），rename 就位后
//       目标路径从未经过「存在且权限过宽」的那一瞬。
// 另有一条要命的释放口径（F9 的补）：`ssh_get_fingerprint_hash` 交回的串**只归 `free`**，
// 再走 `ssh_string_free_char` 本机当场 double free abort——探针读数，写在这里以免有人照
// `ssh_string` 的惯例去释放它。
// ============================================================

#include "conn/key_store.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "platform/file_secure.h"

namespace borealis::conn {

namespace {

/// @brief 私钥头嗅探的读取窗口：一行 PEM 头徽标不超过这个数，读得多只是浪费一次 IO。
constexpr std::size_t kHeadSniffBytes = 256U;

/// @brief `.pub` 首行的长度上限（一行公钥远短于此；超长的文件不是公钥文件，不读它的字段）。
constexpr std::size_t kMaxPublicLineBytes = 8192U;

/// @brief 生成期临时件的中段标记（连同前置点号，见 scan_keys 里「点号开头不成行」那条）。
constexpr std::string_view kTempMarker = ".borealis-tmp";

/// @brief libssh 句柄的作用域形态：本腿有十余条失败分支，逐条手拆必漏（SSH_KEY_FREE 宏同款纪律）。
using SshKeyHandle = std::unique_ptr<ssh_key_struct, decltype(&ssh_key_free)>;
using SshCtxHandle = std::unique_ptr<ssh_pki_ctx_struct, decltype(&ssh_pki_ctx_free)>;

[[nodiscard]] auto adopt_key(ssh_key key) -> SshKeyHandle {
    return SshKeyHandle{key, ssh_key_free};
}

[[nodiscard]] auto adopt_ctx(ssh_pki_ctx context) -> SshCtxHandle {
    return SshCtxHandle{context, ssh_pki_ctx_free};
}

/// @brief 路径拼接。分隔符沿用 key_model 那侧的 '/'（`normalize_key_dirs()` 就是拼 '/'），
///        否则「表里写的」与「扫到的」对不上——行唯一键的比较是逐字节的（口径⑵）。
[[nodiscard]] auto join_path(std::string_view dir, std::string_view name) -> std::string {
    auto out = std::string{dir};
    if (!out.empty() && out.back() != '/' && out.back() != '\\') {
        out.push_back('/');
    }
    out.append(name);
    return out;
}

/// @brief 一个路径在盘上是什么。`lstat` 形态：符号链接就是符号链接，绝不跟随。
enum class PathKind : std::uint8_t {
    Absent,      ///< 不存在（含断链：链接本身在，故不归此档）。
    Regular,     ///< 普通文件。
    Directory,   ///< 目录。
    Symlink,     ///< 符号链接（指向什么都一样）。
    Other,       ///< 管道、socket、设备之类。
    Unknowable,  ///< stat 本身失败（权限不足等）。
};

/// @brief 一次 `lstat` 出盘上形态。**判不在场必须排在判 ec 之前**：本机 GCC 15 + libstdc++
///        把 ENOENT 同时报进 `ec` 与 `file_type::not_found`（实测读数），照「先看 ec」的常规顺序
///        写就会把「这里没有文件」读成「这里判不了」，于是每一次生成都被同名闸拦下。
///        其余错误（权限不足、路径过长）才落 `Unknowable`。
[[nodiscard]] auto classify(const std::filesystem::path &path) -> PathKind {
    auto ec = std::error_code{};
    const auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        return PathKind::Absent;
    }
    if (ec) {
        return PathKind::Unknowable;
    }
    switch (status.type()) {
    case std::filesystem::file_type::regular:
        return PathKind::Regular;
    case std::filesystem::file_type::directory:
        return PathKind::Directory;
    case std::filesystem::file_type::symlink:
        return PathKind::Symlink;
    default:
        return PathKind::Other;
    }
}

/// @brief 这个名字是否已被占（生成前的第三闸）。
///
/// 判不了（Unknowable）也算占用：宁可拒一次生成，也不冒 F5 那种静默改写的险。
/// 目录、符号链接、断链一律算占用——它们都让「同名私钥」这件事在盘上变得含糊。
[[nodiscard]] auto occupies_name(const std::filesystem::path &path) -> bool {
    return classify(path) != PathKind::Absent;
}

[[nodiscard]] auto read_head(const std::filesystem::path &path) -> std::optional<std::string> {
    auto stream = std::ifstream{path, std::ios::binary};
    if (!stream.is_open()) {
        return std::nullopt;
    }
    auto buffer = std::string(kHeadSniffBytes, '\0');
    stream.read(buffer.data(), static_cast<std::streamsize>(kHeadSniffBytes));
    const auto got = static_cast<std::size_t>(stream.gcount());
    buffer.resize(got);
    return buffer;
}

/// @brief 私钥头嗅探：只筛掉「压根不是私钥」的文件（`config`、`known_hosts`、`authorized_keys`），
///        不做任何解析——能不能导入由 F8 的那次探测说。
[[nodiscard]] auto looks_like_private_key(std::string_view head) -> bool {
    return head.find("-----BEGIN ") != std::string_view::npos &&
           head.find("PRIVATE KEY") != std::string_view::npos;
}

/// @brief `.pub` 首行的三个字段。
struct PublicFields {
    std::string wire_name;
    std::string base64;
    std::string comment;
};

[[nodiscard]] auto trim_blanks(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

/// @brief 拆 `.pub` 首行。回 nullopt 的三种情形（都让调用方退回「从私钥公共半取字段」）：
///        读不到/超长、字段不齐、首字段不是本仓两族的线名。
///
/// **首字段必须精确等于线名**：带选项前缀（`from="…"`）的行、`#` 注释行、ecdsa/dsa 等
/// 其余算法族都在这一条上退回，而不是把选项串当类型记进行表。
[[nodiscard]] auto read_public_fields(const std::filesystem::path &path) -> std::optional<PublicFields> {
    auto stream = std::ifstream{path};
    if (!stream.is_open()) {
        return std::nullopt;
    }
    auto line = std::string{};
    std::getline(stream, line);
    if (line.size() > kMaxPublicLineBytes) {
        return std::nullopt;
    }
    const auto text = trim_blanks(line);
    const auto first = text.find(' ');
    if (first == std::string_view::npos || first == 0U || first + 1U >= text.size()) {
        return std::nullopt;
    }
    const auto wire = text.substr(0U, first);
    if (wire != key_wire_name(KeyType::Ed25519) && wire != key_wire_name(KeyType::Rsa)) {
        return std::nullopt;
    }
    const auto rest = text.substr(first + 1U);
    const auto second = rest.find(' ');
    const auto blob = second == std::string_view::npos ? rest : rest.substr(0U, second);
    const auto comment = second == std::string_view::npos ? std::string_view{} : rest.substr(second + 1U);
    if (blob.empty()) {
        return std::nullopt;
    }
    return PublicFields{std::string{wire}, std::string{blob}, std::string{trim_blanks(comment)}};
}

/// @brief 把一行文本写出去（`.pub` 用）。返回假＝一个字节都没落实。
[[nodiscard]] auto write_single_line(const std::filesystem::path &path, std::string_view content) -> bool {
    auto stream = std::ofstream{path, std::ios::binary};
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    return stream.good();
}

/// @brief 现算 `SHA256:…`（F9 的唯一来源，本仓不自算哈希）。失败回空串。
[[nodiscard]] auto fingerprint_of(ssh_key key) -> std::string {
    if (key == nullptr) {
        return {};
    }
    auto hash = static_cast<unsigned char *>(nullptr);
    auto length = std::size_t{0U};
    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length) != SSH_OK) {
        return {};
    }
    auto text = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length);
    ssh_clean_pubkey_hash(&hash);  // 这个调用会把 hash 置空，之后再碰它是 nullptr。
    auto out = text != nullptr ? std::string{text} : std::string{};
    std::free(text);               // 见文件头：只归 free，double free 是实测踩过的。
    return out;
}

/// @brief 公钥 blob 的 base64 段（RSA 位数的输入，也是 `.pub` 的第二字段）。失败回空串。
[[nodiscard]] auto public_base64_of(ssh_key key) -> std::string {
    if (key == nullptr) {
        return {};
    }
    auto raw = static_cast<char *>(nullptr);
    if (ssh_pki_export_pubkey_base64(key, &raw) != SSH_OK || raw == nullptr) {
        return {};
    }
    auto out = std::string{raw};
    std::free(raw);
    return out;
}

/// @brief libssh 密钥类型 → 本仓枚举（本腿唯一知道 libssh 枚举取值的地方，
///        `translate_host_key_state` 先例）。两族之外的算法族回 `Unknown`，行照成、徽标为空。
[[nodiscard]] auto to_key_type(ssh_keytypes_e type) -> KeyType {
    switch (type) {
    case SSH_KEYTYPE_ED25519:
        return KeyType::Ed25519;
    case SSH_KEYTYPE_RSA:
        return KeyType::Rsa;
    default:
        return KeyType::Unknown;
    }
}

/// @brief 本仓枚举 → libssh 生成入口要的取值；`Unknown` 回 SSH_KEYTYPE_UNKNOWN 让调用方拒。
[[nodiscard]] auto to_libssh_type(KeyType type) -> ssh_keytypes_e {
    switch (type) {
    case KeyType::Ed25519:
        return SSH_KEYTYPE_ED25519;
    case KeyType::Rsa:
        return SSH_KEYTYPE_RSA;
    case KeyType::Unknown:
        break;
    }
    return SSH_KEYTYPE_UNKNOWN;
}

/// @brief 无口令的两种写法（nullopt 与空串）折成 libssh 要的 nullptr（F13）。
[[nodiscard]] auto libssh_passphrase(const std::optional<std::string> &passphrase) -> const char * {
    if (!passphrase.has_value() || passphrase->empty()) {
        return nullptr;
    }
    return passphrase->c_str();
}

/// @brief 一把私钥在盘上的全部事实（稿 §5 的「扫盘」那一步，字段来源见 key_store.h 的⑴⑵⑶）。
[[nodiscard]] auto row_of_private_key(const std::filesystem::path &path) -> KeyCandidate {
    const auto path_text = path.string();
    auto row = KeyCandidate{};
    row.path = path_text;
    row.basename = path.filename().string();

    // ---- 配对 `.pub`：在场与否是一个盘上事实，字段能不能取到是另一件事 ----
    // 符号链接的 `.pub` 算在场但**不读它**（扫面不跟链接，D2③⑶）：那一行因此不显「公钥缺失」、
    // 也不会让「导出公钥」去覆写链接指向的文件。
    const auto pub_path = std::filesystem::path{path_text + ".pub"};
    const auto pub_kind = classify(pub_path);
    row.has_public = pub_kind == PathKind::Regular || pub_kind == PathKind::Symlink;
    if (row.has_public) {
        row.public_path = pub_path.string();
    }
    if (pub_kind == PathKind::Regular) {
        if (const auto fields = read_public_fields(pub_path); fields.has_value()) {
            row.type = to_key_type(ssh_key_type_from_name(fields->wire_name.c_str()));
            row.comment = fields->comment;
            row.public_base64 = fields->base64;
            if (row.type == KeyType::Rsa) {
                row.bits = rsa_bits_from_public_base64(fields->base64);
            }
            auto imported = ssh_key{nullptr};
            if (ssh_pki_import_pubkey_base64(fields->base64.c_str(), to_libssh_type(row.type),
                                             &imported) == SSH_OK) {
                row.fingerprint = fingerprint_of(imported);
            }
            auto imported_handle = adopt_key(imported);
        }
    }

    // ---- F8 的一次探测：不解密，只问「无口令能不能导入」 ----
    auto imported_private = ssh_key{nullptr};
    const auto import_rc =
        ssh_pki_import_privkey_file(path_text.c_str(), nullptr, nullptr, nullptr, &imported_private);
    auto private_handle = adopt_key(imported_private);
    row.encrypted = import_rc != SSH_OK;

    // ---- 来源⑵：`.pub` 不在场（或字段取不出）而私钥可无口令导入 ----
    if (private_handle) {
        if (row.type == KeyType::Unknown) {
            row.type = to_key_type(ssh_key_type(private_handle.get()));
        }
        if (row.public_base64.empty()) {
            // 符号链接的 `.pub` 走的就是这一支：不读链接指向的文件，而**导出**公钥的那一行
            // 与**复制**的那一行都得有个 base64 来源，于是从私钥的公共部分现算。
            row.public_base64 = public_base64_of(private_handle.get());
        }
        if (row.type == KeyType::Rsa && row.bits == 0U) {
            row.bits = rsa_bits_from_public_base64(row.public_base64);
        }
        if (row.fingerprint.empty()) {
            row.fingerprint = fingerprint_of(private_handle.get());
        }
        // 注释只有一个来源（`.pub` 第三字段）：libssh 0.12 没有注释访问器（F12），
        // 私钥文件里也不存注释，故无 `.pub` 的行注释恒空——面板据此不显注释段，不编一个。
    }
    return row;
}

}  // namespace

auto scan_keys(const std::vector<KeyDirEntry> &dirs) -> std::vector<KeyCandidate> {
    auto rows = std::vector<KeyCandidate>{};
    for (const auto &dir : dirs) {
        if (!dir.reachable || dir.path.empty()) {
            continue;  // 不可达条目按 D2③⑶ 只标不删；这一趟扫面跳过它，留痕归面板侧的目录注记。
        }
        auto ec = std::error_code{};
        // skip_permission_denied：读不了的目录跳过，而不是整趟扫面失败（sftp_panel 本地列目录同法）。
        auto iterator = std::filesystem::directory_iterator{
            std::filesystem::path{dir.path}, std::filesystem::directory_options::skip_permission_denied, ec};
        if (ec) {
            AURORA_LOG_WARN("conn", "keys: scan skipped '", dir.path, "': ", ec.message());
            continue;
        }
        for (const auto &entry : iterator) {
            auto entry_ec = std::error_code{};
            const auto name = entry.path().filename().string();
            // ⑴ **符号链接不成行**（D2③⑶：与删除腿 D12⑶ 同一条理由，链接与目标的语义歧义）；
            // ⑵ 点号开头的隐藏件不成行——本仓的名称闸禁 '.' 起头（`validate_key_name` 的 StartsDot），
            //    扫进来也对应不到一行可生成的密钥，而生成中途崩溃残留的 `.<name>.borealis-tmp`
            //    正是这一档的常客（它躺着一整把私钥，列出来反而像个能用的东西）；
            // ⑶ `.pub` 不成行（D2①：孤零零的公钥不是「一把钥匙」）。
            if (name.empty() || name.front() == '.' || entry.is_symlink(entry_ec) ||
                name.ends_with(".pub")) {
                continue;
            }
            if (entry_ec || !entry.is_regular_file(entry_ec)) {
                continue;  // 目录、管道与读不了的条目都不是一行密钥。
            }
            const auto head = read_head(entry.path());
            if (!head.has_value() || !looks_like_private_key(*head)) {
                continue;
            }
            rows.push_back(row_of_private_key(entry.path()));
        }
    }
    return rows;
}

auto generate_key(const GenerateRequest &request) -> GenerateOutcome {
    // 纯逻辑闸先过（第 5 节那条分界：字符与长度合不合规矩归 key_model，本件不造第二套裁决）。
    if (request.directory.empty() || validate_key_name(request.basename) != NameIssue::None ||
        validate_comment(request.comment) != CommentIssue::None ||
        to_libssh_type(request.type) == SSH_KEYTYPE_UNKNOWN ||
        (request.type == KeyType::Rsa && !rsa_bits_is_supported(request.rsa_bits))) {
        AURORA_LOG_ERROR("conn", "keys: generate refused by the request gates (dir='",
                         request.directory, "', name='", request.basename, "')");
        return GenerateOutcome::InvalidRequest;
    }

    const auto directory = std::filesystem::path{request.directory};
    // 目标目录可以还不存在（`~/.ssh` 首次生成正是这种时候）。权限的口径：**只有本次建的、
    // 且里面还没有东西的那一个目录**才钉 0700；用户已有的目录一律不擅自改，哪怕它 0777——
    // 那不是本动作的职权（第 1 条），而 `~/.ssh` 本就该是 0700，缺这一条等于把 F6 的教训
    // 从文件挪到目录上重犯一遍。
    auto ec = std::error_code{};
    const auto created_directory = std::filesystem::create_directories(directory, ec);
    if (ec || classify(directory) != PathKind::Directory) {
        AURORA_LOG_ERROR("conn", "keys: target directory unusable: ", request.directory,
                         ec ? " - " + ec.message() : std::string{});
        return GenerateOutcome::DirectoryUnusable;
    }
    if (created_directory) {
        auto empty_ec = std::error_code{};
        if (std::filesystem::is_empty(directory, empty_ec) && !empty_ec) {
            auto perm_ec = std::error_code{};
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace, perm_ec);
            if (perm_ec) {
                AURORA_LOG_WARN("conn", "keys: cannot tighten '", request.directory, "': ",
                                perm_ec.message());
            }
        }
    }

    const auto private_text = join_path(request.directory, request.basename);
    const auto pub_text = private_text + ".pub";
    const auto private_path = std::filesystem::path{private_text};
    const auto pub_path = std::filesystem::path{pub_text};
    // D5① 的第三闸在盘上：私钥本体与 `.pub` 任一在场即拒（孤 `.pub` 不成行，key_model 那半
    // 看不见它，故这里必须自己判，见裁决 7.107 ④）。
    if (occupies_name(private_path) || occupies_name(pub_path)) {
        AURORA_LOG_WARN("conn", "keys: refused to overwrite an existing name: ", private_text);
        return GenerateOutcome::NameTaken;
    }

    const auto temp_text = join_path(request.directory, "." + request.basename + std::string{kTempMarker});
    const auto temp_path = std::filesystem::path{temp_text};
    const auto created = platform::create_private_file(temp_text);
    if (created != platform::PrivateFileOutcome::Created) {
        // AlreadyExists 在这一档只剩一个解释：上一次崩在这里留下的临时件。它是 0600 的私钥材料，
        // 本件不覆写也不删（那是用户的盘），把路径记进日志让人自己处置。
        AURORA_LOG_ERROR("conn", "keys: cannot create the 0600 scratch file: ", temp_text,
                         created == platform::PrivateFileOutcome::AlreadyExists
                             ? " (a leftover from an interrupted run?)"
                             : "");
        return GenerateOutcome::DirectoryUnusable;
    }
    const auto rollback = [](const std::filesystem::path &leftover) {
        auto remove_ec = std::error_code{};
        std::filesystem::remove(leftover, remove_ec);  // 半成品不留，失败也不改归因（盘上都已判过）。
    };

    auto key = ssh_key{nullptr};
    {
        auto context = adopt_ctx(ssh_pki_ctx_new());
        if (!context) {
            AURORA_LOG_ERROR("conn", "keys: ssh_pki_ctx_new returned null");
            rollback(temp_path);
            return GenerateOutcome::Failed;
        }
        if (request.type == KeyType::Rsa) {
            // F2 的读数形态：位数是经 ctx 选项传的，不是生成函数的实参。
            auto bits = static_cast<int>(request.rsa_bits);
            if (ssh_pki_ctx_options_set(context.get(), SSH_PKI_OPTION_RSA_KEY_SIZE, &bits) != SSH_OK) {
                AURORA_LOG_ERROR("conn", "keys: cannot set the RSA size option: ", request.rsa_bits);
                rollback(temp_path);
                return GenerateOutcome::Failed;
            }
        }
        // F3：这一行是百毫秒级长计算，调用方已在 worker 线程（稿 §0 的那条纪律）。
        if (ssh_pki_generate_key(to_libssh_type(request.type), context.get(), &key) != SSH_OK ||
            key == nullptr) {
            AURORA_LOG_ERROR("conn", "keys: generation failed for ", request.basename);
            rollback(temp_path);
            return GenerateOutcome::Failed;
        }
    }
    auto handle = adopt_key(key);

    if (ssh_pki_export_privkey_file_format(handle.get(), libssh_passphrase(request.passphrase),
                                          nullptr, nullptr, temp_text.c_str(),
                                          SSH_FILE_FORMAT_OPENSSH) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "keys: cannot write the private key: ", temp_text);
        rollback(temp_path);
        return GenerateOutcome::Failed;
    }
    auto rename_ec = std::error_code{};
    std::filesystem::rename(temp_path, private_path, rename_ec);
    if (rename_ec) {
        AURORA_LOG_ERROR("conn", "keys: cannot put the private key in place: ", private_text,
                         " - ", rename_ec.message());
        rollback(temp_path);
        return GenerateOutcome::Failed;
    }

    // `.pub` 在私钥就位**之后**才写：写失败得到的是「公钥缺失」那一态（稿 §2 判据 3 里有出口：
    // 「导出公钥」），比反过来留一份无主公钥好。故这里不回 Failed，只把话记进日志。
    const auto line = public_line(key_wire_name(request.type), public_base64_of(handle.get()),
                                  request.comment);
    if (line.empty() || !write_single_line(pub_path, line + "\n")) {
        AURORA_LOG_WARN("conn", "keys: the key is in place but its public half could not be written: ",
                        pub_text);
    }
    return GenerateOutcome::Created;
}

auto export_public_key(std::string_view private_path, std::optional<std::string> passphrase)
    -> ExportOutcome {
    if (private_path.empty()) {
        return ExportOutcome::Failed;
    }
    const auto target = std::filesystem::path{std::string{private_path} + ".pub"};
    if (occupies_name(target)) {
        // 多半是列表过期（上一次扫盘之后别处写了这份公钥）：不覆写，让调用方重扫。
        AURORA_LOG_WARN("conn", "keys: the public file is already there: ", target.string());
        return ExportOutcome::AlreadyExists;
    }

    auto key = ssh_key{nullptr};
    const auto pass = libssh_passphrase(passphrase);
    if (ssh_pki_import_privkey_file(std::string{private_path}.c_str(), pass, nullptr, nullptr, &key) !=
        SSH_OK) {
        // 与扫面那次探测同一档读数：库里不区分「要口令」与「读不懂」。没交出口令时按前者报，
        // 让面板去问；已经交了口令还失败就是后者（或口令不对），归 Failed 不再追问。
        auto handle = adopt_key(key);
        if (pass == nullptr) {
            return ExportOutcome::NeedsPassphrase;
        }
        AURORA_LOG_ERROR("conn", "keys: private key cannot be opened with the given passphrase: ",
                         private_path);
        return ExportOutcome::Failed;
    }
    auto private_handle = adopt_key(key);

    auto public_key = ssh_key{nullptr};
    if (ssh_pki_export_privkey_to_pubkey(private_handle.get(), &public_key) != SSH_OK) {
        auto orphan = adopt_key(public_key);  // 失败时库可能已分配，一样要拆。
        AURORA_LOG_ERROR("conn", "keys: cannot take the public half: ", private_path);
        return ExportOutcome::Failed;
    }
    auto public_handle = adopt_key(public_key);
    const auto wire = key_wire_name(to_key_type(ssh_key_type(public_handle.get())));
    const auto line = public_line(wire, public_base64_of(public_handle.get()), {});
    if (line.empty()) {
        AURORA_LOG_ERROR("conn", "keys: the public line could not be assembled for ", private_path);
        return ExportOutcome::Failed;
    }
    // 注释留空是**库的能力边界**不是遗漏：F12——0.12 公共头没有注释访问器，私钥文件里也不存注释。
    if (!write_single_line(target, line + "\n")) {
        AURORA_LOG_ERROR("conn", "keys: cannot write ", target.string());
        return ExportOutcome::Failed;
    }
    return ExportOutcome::Written;
}

auto delete_key_pair(std::string_view private_path) -> DeleteOutcome {
    if (private_path.empty()) {
        return DeleteOutcome::NotFound;
    }
    const auto target = std::filesystem::path{private_path};
    const auto pub_target = std::filesystem::path{std::string{private_path} + ".pub"};
    const auto kind = classify(target);
    const auto pub_kind = classify(pub_target);

    // D12⑶：**两条路径都先过符号链接闸再动任何一物**。半删状态下链接还指着别处，
    // 比整对不见更难查，故这里不做「私钥删了、公钥是链接就留着它」那种折中。
    if (kind == PathKind::Symlink || pub_kind == PathKind::Symlink) {
        AURORA_LOG_WARN("conn", "keys: delete refused for a symlinked path: ", private_path);
        return DeleteOutcome::RefusedSymlink;
    }
    if (kind == PathKind::Absent) {
        return DeleteOutcome::NotFound;  // 并发删除或列表过期（D12⑷）。
    }
    if (kind == PathKind::Unknowable || kind == PathKind::Directory || kind == PathKind::Other) {
        // 判不了的目录条目不删（第 1 条：不推断需求外行为）；不是一行密钥的东西本就不该在行表里。
        AURORA_LOG_ERROR("conn", "keys: delete cannot proceed on ", private_path);
        return DeleteOutcome::Failed;
    }
    // D12⑵：删除**不读私钥内容**，故有口令的钥匙不需要口令就能删——这里一次 import 都没有。
    if (pub_kind == PathKind::Unknowable || pub_kind == PathKind::Directory ||
        pub_kind == PathKind::Other) {
        AURORA_LOG_ERROR("conn", "keys: the public slot is not a plain file: ", pub_target.string());
        return DeleteOutcome::Failed;
    }

    auto remove_ec = std::error_code{};
    if (pub_kind == PathKind::Regular) {
        std::filesystem::remove(pub_target, remove_ec);
        if (remove_ec) {
            AURORA_LOG_ERROR("conn", "keys: cannot unlink ", pub_target.string(), " - ",
                             remove_ec.message());
            return DeleteOutcome::Failed;  // 私钥一物未动，行还在列表里。
        }
    }
    std::filesystem::remove(target, remove_ec);
    if (remove_ec) {
        // 公钥已经不在、私钥删不掉：这一行退化成「公钥缺失」态，面板那枚「导出公钥」就是出口。
        AURORA_LOG_ERROR("conn", "keys: cannot unlink ", private_path, " - ", remove_ec.message());
        return DeleteOutcome::Failed;
    }
    return DeleteOutcome::Deleted;
}

}  // namespace borealis::conn
