// ============================================================
// SSH 密钥管理纯逻辑模型实现（src/conn/key_model.cpp）
// ------------------------------------------------------------
// 接口与逐条口径见 conn/key_model.h（SPEC.FEAT.CONN.10，裁决 7.106）。
// 本 TU 不含 libssh、不含 stat、不含 Aurora 类型——判据就是「无头可证」（第 20 条）。
// ============================================================

#include "conn/key_model.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::conn {

namespace {

/// @brief ASCII 空白（判重与拼装只认这五个，UTF-8 空格类字符按普通字节留在注释里）。
[[nodiscard]] auto is_ascii_space(char c) -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// @brief ASCII 控制字符（含 0x7F）；≥0x80 的字节属 UTF-8 多字节序列，不算。
[[nodiscard]] auto is_control_char(char c) -> bool {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20U || byte == 0x7FU;
}

[[nodiscard]] auto is_path_separator(char c) -> bool {
    return c == '/' || c == '\\';
}

/// @brief 取父目录段：最后一个分隔符**之前**的全部（含该分隔符，便于再拼名字）。
///        无分隔符时回空串——纯字符串操作，不碰文件系统。
[[nodiscard]] auto parent_of(std::string_view path) -> std::string {
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) {
        return {};
    }
    return std::string{path.substr(0, pos + 1)};
}

/// @brief 裁首尾 ASCII 空白。
[[nodiscard]] auto trim(std::string_view text) -> std::string_view {
    std::size_t begin = 0U;
    while (begin < text.size() && is_ascii_space(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_ascii_space(text[end - 1U])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// @brief 按 ASCII 空白切token（连续空白不产生空 token）。
[[nodiscard]] auto tokenize(std::string_view text) -> std::vector<std::string_view> {
    auto out = std::vector<std::string_view>{};
    std::size_t cursor = 0U;
    while (cursor < text.size()) {
        while (cursor < text.size() && is_ascii_space(text[cursor])) {
            ++cursor;
        }
        const auto start = cursor;
        while (cursor < text.size() && !is_ascii_space(text[cursor])) {
            ++cursor;
        }
        if (cursor > start) {
            out.push_back(text.substr(start, cursor - start));
        }
    }
    return out;
}

[[nodiscard]] auto contains_blank(std::string_view text) -> bool {
    return std::any_of(text.begin(), text.end(), [](char c) { return is_ascii_space(c); });
}

[[nodiscard]] auto contains_control(std::string_view text) -> bool {
    return std::any_of(text.begin(), text.end(), is_control_char);
}

/// @brief 父段比较用的归一：比较前把两侧都补成「带尾分隔符」的形态。
[[nodiscard]] auto parent_key(std::string_view path) -> std::string {
    auto parent = parent_of(path);
    if (parent.empty()) {
        return {};
    }
    // `dir` 参数可能不带尾分隔符，比较前统一去掉，避免「同目录两种写法」。
    while (!parent.empty() && is_path_separator(parent.back())) {
        parent.pop_back();
    }
    return parent;
}

[[nodiscard]] auto dir_key(std::string_view dir) -> std::string {
    auto out = std::string{dir};
    while (out.size() > 1U && is_path_separator(out.back())) {
        out.pop_back();
    }
    return out;
}

/// @brief 标准 base64 字母表取值；非法字符回 -1（`=` 填充由调用侧先行剥掉）。
[[nodiscard]] auto base64_value(char c) -> int {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

/// @brief 解标准 base64（只用于公钥 blob，故不收 URL-safe 变体、不收换行）。
///
/// 长度不是 4 的倍数时按「残缺的最后一组」处理：余 2 或 3 个实字符可出 1 或 2 字节，
/// 余 1 个不可能——那是坏数据，回空串让调用方按「解不出」办。
[[nodiscard]] auto base64_decode(std::string_view text) -> std::optional<std::string> {
    auto payload = std::string_view{trim(text)};
    while (!payload.empty() && payload.back() == '=') {
        payload.remove_suffix(1U);
    }
    auto out = std::string{};
    out.reserve(payload.size() / 4U * 3U + 3U);
    auto group = std::array<int, 4>{0, 0, 0, 0};
    std::size_t index = 0U;
    for (const char c : payload) {
        const auto value = base64_value(c);
        if (value < 0) {
            return std::nullopt;  // 含空白、含非法字符：不是公钥 blob 该有的样子
        }
        group[index] = value;
        ++index;
        if (index == 4U) {
            out.push_back(static_cast<char>((group[0] << 2) | (group[1] >> 4)));
            out.push_back(static_cast<char>(((group[1] & 0x0F) << 4) | (group[2] >> 2)));
            out.push_back(static_cast<char>(((group[2] & 0x03) << 6) | group[3]));
            index = 0U;
        }
    }
    if (index == 1U) {
        return std::nullopt;
    }
    if (index == 2U) {
        out.push_back(static_cast<char>((group[0] << 2) | (group[1] >> 4)));
    } else if (index == 3U) {
        out.push_back(static_cast<char>((group[0] << 2) | (group[1] >> 4)));
        out.push_back(static_cast<char>(((group[1] & 0x0F) << 4) | (group[2] >> 2)));
    }
    return out;
}

/// @brief 线格式里的一个 `uint32` 大端长度前缀；越界回 nullopt（不猜、不截断读取）。
[[nodiscard]] auto read_length(std::string_view blob, std::size_t &cursor) -> std::optional<std::size_t> {
    if (cursor + 4U > blob.size()) {
        return std::nullopt;
    }
    auto value = std::size_t{0U};
    for (std::size_t i = 0U; i < 4U; ++i) {
        value = (value << 8U) | static_cast<unsigned char>(blob[cursor + i]);
    }
    cursor += 4U;
    if (value > blob.size() - cursor) {
        return std::nullopt;  // 长度字段说的字节不在 blob 里：坏数据
    }
    return value;
}

/// @brief 线格式里的一个 `string`（长度前缀 + 原始字节）。
[[nodiscard]] auto read_wire_string(std::string_view blob, std::size_t &cursor)
    -> std::optional<std::string_view> {
    const auto length = read_length(blob, cursor);
    if (!length.has_value()) {
        return std::nullopt;
    }
    const auto out = blob.substr(cursor, *length);
    cursor += *length;
    return out;
}

}  // namespace

auto key_wire_name(KeyType type) -> std::string_view {
    switch (type) {
    case KeyType::Ed25519:
        return "ssh-ed25519";
    case KeyType::Rsa:
        return "ssh-rsa";
    case KeyType::Unknown:
        break;
    }
    return {};
}

auto rsa_bits_is_supported(std::uint16_t bits) -> bool {
    return bits == static_cast<std::uint16_t>(RsaBits::Bits2048) ||
           bits == static_cast<std::uint16_t>(RsaBits::Bits3072) ||
           bits == static_cast<std::uint16_t>(RsaBits::Bits4096);
}

auto rsa_bits_from_public_base64(std::string_view base64) -> std::uint16_t {
    const auto decoded = base64_decode(base64);
    if (!decoded.has_value()) {
        return 0U;
    }
    const auto blob = std::string_view{*decoded};
    std::size_t cursor = 0U;
    const auto wire_name = read_wire_string(blob, cursor);
    if (!wire_name.has_value() || *wire_name != "ssh-rsa") {
        return 0U;  // ed25519 与其余线名都没有位数概念
    }
    const auto exponent = read_wire_string(blob, cursor);  // mpint e：读过即弃
    const auto modulus = read_wire_string(blob, cursor);
    if (!exponent.has_value() || !modulus.has_value() || modulus->empty()) {
        return 0U;
    }
    auto bytes = std::size_t{modulus->size()};
    if (modulus->front() == '\0') {
        --bytes;  // OpenSSH 的符号位填充：最高位为 1 时前置一个 0x00，不计位数
    }
    if (bytes == 0U || bytes > (0xFFFFU / 8U)) {
        return 0U;  // 全零模数与荒谬长度都不出数（uint16 装不下即判解不出，而非判合法）
    }
    return static_cast<std::uint16_t>(bytes * 8U);
}

auto sort_rows(std::vector<KeyCandidate> &rows, const std::vector<std::string> &dirs)
    -> std::vector<KeyCandidate> & {
    // 目录表的次序是**用户次序**（`~/.ssh` 恒在表首），不在表内的排最后：
    // 快照与并发改盘都可能带来表外行，丢行比乱序更糟。
    auto rank = std::vector<std::string>{};
    rank.reserve(dirs.size());
    for (const auto &dir : dirs) {
        rank.push_back(dir_key(dir));
    }
    const auto position_of = [&rank](const KeyCandidate &row) {
        const auto parent = parent_key(row.path);
        const auto found = std::find(rank.begin(), rank.end(), parent);
        return found == rank.end() ? rank.size() : static_cast<std::size_t>(found - rank.begin());
    };
    std::stable_sort(rows.begin(), rows.end(), [&position_of](const KeyCandidate &a, const KeyCandidate &b) {
        const auto pa = position_of(a);
        const auto pb = position_of(b);
        if (pa != pb) {
            return pa < pb;
        }
        if (a.basename != b.basename) {
            return a.basename < b.basename;
        }
        // 同目录同名（理论上只可能是大小写不同的文件名）时用全路径定序，保证两次扫盘同序。
        return a.path < b.path;
    });
    return rows;
}

auto normalize_key_dirs(std::span<const std::string> raw, std::string_view user_dir,
                        const std::function<bool(std::string_view)> &probe) -> std::vector<KeyDirEntry> {
    auto home = std::string{};
    if (!user_dir.empty()) {
        // 用户目录可能带尾分隔符，拼时不重复加。
        auto base = std::string{user_dir};
        while (base.size() > 1U && is_path_separator(base.back())) {
            base.pop_back();
        }
        base.push_back('/');
        base.append(".ssh");
        home = std::move(base);
    }

    auto out = std::vector<KeyDirEntry>{};
    const auto push_unique = [&out, &probe](std::string path, bool is_home) {
        const auto duplicate = std::any_of(out.begin(), out.end(), [&path](const KeyDirEntry &entry) {
            return entry.path == path;
        });
        if (duplicate) {
            return;
        }
        auto entry = KeyDirEntry{std::move(path), is_home, true};
        if (probe) {
            entry.reachable = probe(entry.path);
        }
        out.push_back(std::move(entry));
    };

    // `~/.ssh` 恒在且恒在表首（D2③ 细则⑵：否则「首屏零配置可用」的 PREF.06 不成立）。
    // 拿不到用户目录时不追加——凭空造一条扫面比少一条更糟。
    if (!home.empty()) {
        push_unique(home, true);
    }
    for (const auto &dir : raw) {
        const auto trimmed = trim(dir);
        if (trimmed.empty()) {
            continue;  // 空串不是目录，装载侧也不代填。
        }
        // 表里写死了 `~/.ssh` 的也并进首条：逐字节相等才算同一条（口径⑵）。
        push_unique(std::string{trimmed}, !home.empty() && trimmed == home);
    }
    return out;
}

auto without_dir(const std::vector<KeyDirEntry> &dirs, std::string_view path) -> std::vector<KeyDirEntry> {
    auto out = dirs;
    const auto target = dir_key(path);
    const auto found = std::find_if(out.begin(), out.end(), [&target](const KeyDirEntry &entry) {
        return dir_key(entry.path) == target;
    });
    if (found == out.end()) {
        return out;
    }
    if (found->is_home_dir) {
        // 「移除 ~/.ssh」整条请求作废：不移除、也不留一份残缺表（判据 §6 的独立判据）。
        return dirs;
    }
    out.erase(found);
    return out;
}

auto validate_key_name(std::string_view name) -> NameIssue {
    if (name.empty()) {
        return NameIssue::Empty;
    }
    if (name.size() > kMaxKeyNameLength) {
        return NameIssue::TooLong;
    }
    if (std::any_of(name.begin(), name.end(), is_path_separator)) {
        return NameIssue::HasSeparator;
    }
    if (std::any_of(name.begin(), name.end(), is_control_char)) {
        return NameIssue::HasControlChar;
    }
    if (name.front() == '.') {
        // 判在控制字符之后：`.` 与 `..` 的坏处在「越目录」，与隐藏文件的坏处在「看不见」，
        // 后者对 StartsDot 更有描述力，故 Reserved 只收这两个字面量。
        return name == "." || name == ".." ? NameIssue::Reserved : NameIssue::StartsDot;
    }
    return NameIssue::None;
}

auto validate_comment(std::string_view comment) -> CommentIssue {
    if (comment.size() > kMaxCommentLength) {
        return CommentIssue::TooLong;
    }
    // 空注释合法（F7 的 `user@host` 或不填）；注释里的空格也合法——OpenSSH 认带空格的注释。
    if (std::any_of(comment.begin(), comment.end(), is_control_char)) {
        return CommentIssue::HasControlChar;
    }
    return CommentIssue::None;
}

auto would_collide_with_rows(const std::vector<KeyCandidate> &rows, std::string_view dir,
                             std::string_view basename) -> bool {
    if (basename.empty()) {
        return true;  // 空名不是「不碰撞」，是压根不能生成。
    }
    const auto target_dir = dir_key(dir);
    for (const auto &row : rows) {
        if (parent_key(row.path) != target_dir) {
            continue;
        }
        // 行表里只看得见私钥（D2①：孤 `.pub` 不成行），故这里判的是「同目录同私钥名」。
        // 「磁盘上还有一份无主的 `<name>.pub` 也占名」那一半**不在本函数能力内**——它需要盘上
        // 存在性检查，归传输腿（`key_store`，D5① 的第三闸）；此处不留一条永远走不到的分支。
        if (row.basename == basename) {
            return true;
        }
    }
    return false;
}

auto duplicate_basenames(const std::vector<KeyCandidate> &rows) -> std::vector<std::string> {
    auto out = std::vector<std::string>{};
    for (const auto &row : rows) {
        if (std::find(out.begin(), out.end(), row.basename) != out.end()) {
            continue;
        }
        const auto count =
            std::count_if(rows.begin(), rows.end(), [&row](const KeyCandidate &other) { return other.basename == row.basename; });
        if (count > 1) {
            out.push_back(row.basename);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

auto public_line(std::string_view wire_name, std::string_view base64, std::string_view comment) -> std::string {
    // 空线名/空 base64 与其中夹空白都是废行：宁可回空串，让调用方报错，也不产出一行对端认不出的数据。
    if (wire_name.empty() || base64.empty() || contains_blank(wire_name) || contains_blank(base64)) {
        return {};
    }
    const auto tail = trim(comment);
    if (contains_control(tail)) {
        return {};  // 注释里的控制字符会把一行劈成两行。
    }
    auto out = std::string{wire_name};
    out.push_back(' ');
    out.append(base64);
    if (!tail.empty()) {
        out.push_back(' ');
        out.append(tail);
    }
    return out;
}

auto is_authorized(std::string_view remote_content, std::string_view wire_name, std::string_view base64) -> bool {
    if (wire_name.empty() || base64.empty()) {
        return false;
    }
    std::size_t cursor = 0U;
    while (cursor <= remote_content.size()) {
        const auto end = remote_content.find('\n', cursor);
        const auto line = remote_content.substr(cursor, end == std::string_view::npos ? std::string_view::npos
                                                                                      : end - cursor);
        // 同一行里同时出现线名与 base64 才算命中：注释不同也算（授权的是同一把钥匙）。
        // 带选项前缀（`from="…"`）的行因此天然覆盖，独立注释行（`#…`）因此天然不误命中。
        const auto tokens = tokenize(trim(line));
        const auto has_name = std::find(tokens.begin(), tokens.end(), wire_name) != tokens.end();
        const auto has_blob = std::find(tokens.begin(), tokens.end(), base64) != tokens.end();
        if (has_name && has_blob) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1U;
    }
    return false;
}

auto plan_append(std::string_view remote_content, std::string_view line) -> AppendPlan {
    auto plan = AppendPlan{};
    plan.line = std::string{trim(line)};
    const auto tokens = tokenize(plan.line);
    if (tokens.size() < 2U) {
        return plan;  // 不成行的东西既不判重也不发写：payload 空＝不发写。
    }
    const auto wire = std::string_view{tokens[0]};
    const auto blob = std::string_view{tokens[1]};
    plan.already_authorized = is_authorized(remote_content, wire, blob);
    if (plan.already_authorized) {
        return plan;
    }
    // 前置换行是这条函数的存在理由：远端末行没有换行时直接追加会把两把钥匙粘成两把废钥匙。
    const auto needs_separator = !remote_content.empty() && remote_content.back() != '\n';
    if (needs_separator) {
        plan.payload.push_back('\n');
    }
    plan.payload.append(plan.line);
    // 自带尾随换行：与剪贴板那条「无尾随换行」相反，追加写要让 authorized_keys 保持行终止。
    plan.payload.push_back('\n');
    return plan;
}

}  // namespace borealis::conn
