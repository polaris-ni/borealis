#include "settings_i18n.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "aurora/i18n/message_catalog.h"
#include "aurora/i18n/message_id.h"

#include "borealis/config/settings.h"

#include "config/schema_names.h"

namespace borealis::ui {
namespace {

/// @brief 本仓应用词表的 Id 基址：与 `src/CMakeLists.txt` 里 `aurora_i18n_generate(ID_BASE 1024)`
/// 同一枚数。Aurora 把 `[0, 1024)` 留给库内置词表（`codespec/i18n_builtin.json`），应用词表从 1024
/// 起直排，于是「基址上有没有名字」就是「bin 进没进这张目录」的可判定读点。
constexpr std::uint32_t kVocabularyIdBase = 1024U;

/// @brief 某一档的 `Locale`：区域标签逐字取落盘名，映射唯一在 `config::language_tag()`。
[[nodiscard]] auto locale_for(config::InterfaceLanguage language) -> aurora::Locale {
    return aurora::Locale{std::string{config::language_tag(language)}, {}, {}};
}

/// @brief 整读构建期产出的 `AUM1` 词表（全语言合一），只读一次、字节常驻。
///
/// 首选构建期注入的绝对路径（`AURORA_I18N_BIN_PATH`，随 `aurora_i18n_generate` 给出），退而求其次取
/// 当前目录同名文件——bin 不属任何源表，安装交付形态下由应用自行安置，届时读不到就走文件头③的
/// 「空白而不是漏 key」退化形态。
[[nodiscard]] auto vocabulary_bytes() -> const std::vector<std::byte> & {
    static const std::vector<std::byte> bytes = []() -> std::vector<std::byte> {
        std::vector<std::filesystem::path> candidates;
#ifdef AURORA_I18N_BIN_PATH
        candidates.emplace_back(AURORA_I18N_BIN_PATH);
#endif
        candidates.emplace_back("messages_zh.bin");
        for (const std::filesystem::path &candidate : candidates) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(candidate, ec)) {
                continue;
            }
            std::ifstream in(candidate, std::ios::binary);
            if (!in) {
                continue;
            }
            const std::vector<char> raw{(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{}};
            std::vector<std::byte> out(raw.size());
            for (std::size_t k = 0; k < raw.size(); ++k) {
                out[k] = static_cast<std::byte>(static_cast<unsigned char>(raw[k]));
            }
            return out;
        }
        return {};
    }();
    return bytes;
}

/// @brief 该目录里是否已有本仓词表（按基址探一枚规范名，不重复 `load_binary`）。
[[nodiscard]] auto vocabulary_is_loaded(const aurora::MessageCatalog &catalog) -> bool {
    return !catalog.canonical_name(aurora::MessageId{kVocabularyIdBase}).empty();
}

/// @brief 应用词表的全部 key：从基址起沿 Id 直排读规范名，读到空洞即停。
///
/// 空表（还没装填）时不落地缓存，下一次调用重新数——否则「先数表后装填」的调用序会把覆盖率证人
/// 变成一条恒真的空集断言。
[[nodiscard]] auto keys_for() -> const std::vector<std::string> & {
    static std::vector<std::string> keys;
    if (!keys.empty()) {
        return keys;
    }
    const aurora::MessageCatalog &catalog = aurora::default_catalog();
    for (std::uint32_t id = kVocabularyIdBase;; ++id) {
        const std::string_view name = catalog.canonical_name(aurora::MessageId{id});
        if (name.empty()) {
            break;
        }
        keys.emplace_back(name);
    }
    return keys;
}

/// @brief 进程当前语言档的唯一副本：只有收 `InterfaceLanguage` 的那一枚装填函数写它。
///
/// 写点按文件头①的纪律排在**任何控件绘制之前**（`src/main.cpp` 里那一句），所以「一处构造、逐次
/// 同值」在运行期依然成立；而 `Locale::tag()` 每次调用现拼串，面板侧因此只留这一份 `Locale`。
auto current_locale() -> aurora::Locale & {
    static aurora::Locale locale = locale_for(config::InterfaceLanguage::Zh);
    return locale;
}

/// @brief 装填的共用腿：词表进目录（已在则跳过读盘与重建），缺省档定到选定那一档。
auto install_into(aurora::MessageCatalog &catalog, config::InterfaceLanguage language) -> bool {
    bool loaded = true;
    if (!vocabulary_is_loaded(catalog)) {
        const std::vector<std::byte> &bytes = vocabulary_bytes();
        if (bytes.empty()) {
            loaded = false;
        } else if (auto result = catalog.load_binary(std::span<const std::byte>{bytes}); !result) {
            AURORA_LOG_ERROR("i18n", "borealis vocabulary rejected: ", result.error().message);
            loaded = false;
        }
    }
    catalog.set_default_locale(locale_for(language));
    return loaded;
}

/// @brief `CommitIssue` → 词条 key：本件是唯一把该枚举翻成文案的地方（表单件只回枚举，不产文案）。
[[nodiscard]] auto issue_key(CommitIssue issue) -> std::string_view {
    switch (issue) {
    case CommitIssue::UnknownKey:
        return "settings.issue.unknown_key";
    case CommitIssue::NotLoaded:
        return "settings.issue.not_loaded";
    case CommitIssue::DomainMismatch:
        return "settings.issue.domain_mismatch";
    case CommitIssue::TextNotAccepted:
        return "settings.issue.text_not_accepted";
    case CommitIssue::MalformedNumber:
        return "settings.issue.malformed_number";
    case CommitIssue::NotIntegral:
        return "settings.issue.not_integral";
    case CommitIssue::OutOfRange:
        return "settings.issue.out_of_range";
    case CommitIssue::NotAChoice:
        return "settings.issue.not_a_choice";
    case CommitIssue::MalformedColor:
        return "settings.issue.malformed_color";
    case CommitIssue::UnsetNotAllowed:
        return "settings.issue.unset_not_allowed";
    case CommitIssue::SlotOutOfRange:
        return "settings.issue.slot_out_of_range";
    case CommitIssue::TableSizeWrong:
        return "settings.issue.table_size_wrong";
    case CommitIssue::None:
        return {};
    }
    return {};
}

}  // namespace

auto settings_locale() -> const aurora::Locale & { return current_locale(); }

auto install_settings_strings(config::InterfaceLanguage language) -> bool {
    current_locale() = locale_for(language);
    return install_into(aurora::default_catalog(), language);
}

auto install_settings_strings(aurora::MessageCatalog &catalog, config::InterfaceLanguage language) -> bool {
    return install_into(catalog, language);
}

auto settings_string_keys() -> const std::vector<std::string> & { return keys_for(); }

auto settings_string_text(config::InterfaceLanguage language, std::string_view key) -> std::string {
    const aurora::MessageId id = aurora::default_catalog().find_by_name(key);
    if (id == aurora::AURORA_INVALID_MESSAGE_ID) {
        return {};
    }
    return aurora::default_catalog().resolve(aurora::LocalizedString::tr(id), locale_for(language));
}

auto settings_text(std::string_view key, std::vector<aurora::MessageArg> args) -> aurora::LocalizedString {
    const aurora::MessageId id = aurora::default_catalog().find_by_name(key);
    if (id == aurora::AURORA_INVALID_MESSAGE_ID) {
        return aurora::LocalizedString{std::string_view{}};
    }
    return aurora::LocalizedString::tr(id, std::move(args));
}

auto settings_label(std::string_view key, std::vector<aurora::MessageArg> args) -> std::string {
    return settings_text(key, std::move(args)).resolve(&aurora::default_catalog(), settings_locale());
}

auto settings_resolve(const aurora::LocalizedString &text) -> std::string {
    return text.resolve(&aurora::default_catalog(), settings_locale());
}

auto has_settings_string(std::string_view key) -> bool {
    const aurora::MessageId id = aurora::default_catalog().find_by_name(key);
    if (id == aurora::AURORA_INVALID_MESSAGE_ID) {
        return false;
    }
    return !aurora::default_catalog().resolve(aurora::LocalizedString::tr(id), settings_locale()).empty();
}

auto settings_issue_text(CommitIssue issue) -> aurora::LocalizedString {
    const auto key = issue_key(issue);
    if (key.empty()) {
        return aurora::LocalizedString{std::string_view{}};
    }
    return settings_text(key);
}

}  // namespace borealis::ui
