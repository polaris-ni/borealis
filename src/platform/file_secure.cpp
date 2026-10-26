// ============================================================
// 私有文件的「建 + 写 + 就位」共享腿（src/platform/file_secure.cpp）
// ------------------------------------------------------------
// platform::write_private_file 的**共享**实现：临时件命名、失败即删、rename 就位这一套次序是
// 平台无关的纪律（裁决 7.106 D5① 的三道闸 + 稿 UI_SERIAL_MONITOR §10.2 D23①），两腿各写一遍
// 只会漂移；留给两腿的只有「这一串 UTF-8 在盘上叫什么」（native_path_of）与「怎么建那份 0600」
// （create_private_file）两条平台知识（AGENTS.md §4.5 第 23 条）。
//
// 顺序不可换的两处：
//   ⑴ `create_private_file` 在前、`ofstream(trunc)` 在后——截断开一个**已存在**的文件不改它的
//      mode，所以 0600 是这次建出来就钉死的那一份；反过来先开后 chmod 会留一个窗口。
//   ⑵ `rename` 在写完成并 close 之后——点名路径上因此要么还是老文件、要么是完整新文件。
// ============================================================

#include "platform/file_secure.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "aurora/core/log.h"
#include "platform/native_path.h"

namespace borealis::platform {

namespace {

/// @brief 临时件名的档数：首档 `X.tmp`，撞名后依次 `X.tmp1` … `X.tmp7`。
inline constexpr int kScratchAttempts = 8;

/// @brief 第 `attempt` 档临时件的完整路径串（UTF-8，与目标同目录）。
[[nodiscard]] auto scratch_name(std::string_view path, int attempt) -> std::string {
    auto out = std::string{path};
    out += ".tmp";
    if (attempt > 0) {
        out += std::to_string(attempt);
    }
    return out;
}

/// @brief 删掉**本次**建的临时件；删不掉也不改归因（盘上已判过，那已经是另一层问题）。
auto discard_scratch(const std::filesystem::path &temp) -> void {
    std::error_code ec;
    static_cast<void>(std::filesystem::remove(temp, ec));
}

}  // namespace

auto write_private_file(std::string_view path, std::string_view text) -> PrivateWriteOutcome {
    // 与 create_private_file 同一条前置：空串与内嵌 NUL 都不是名字，让它们进系统调用只会落到
    // 另一份路径上——那是比失败更糟的结果。
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        AURORA_LOG_ERROR("platform", "write_private_file refused: the path is empty or contains a NUL");
        return PrivateWriteOutcome::InvalidPath;
    }
    const auto target = native_path_of(path);
    if (target.empty()) {
        AURORA_LOG_ERROR("platform", "write_private_file refused: the path is not usable: ",
                        std::string{path});
        return PrivateWriteOutcome::InvalidPath;
    }

    for (int attempt = 0; attempt < kScratchAttempts; ++attempt) {
        const auto temp_text = scratch_name(path, attempt);
        const auto temp = native_path_of(temp_text);
        const auto created = create_private_file(temp_text);
        if (created == PrivateFileOutcome::AlreadyExists) {
            continue;  // 名字被占：换下一档。不覆写也不删——那可能是用户自己的文件
        }
        if (created != PrivateFileOutcome::Created) {
            AURORA_LOG_ERROR("platform", "write_private_file cannot create the scratch: ", temp_text);
            return PrivateWriteOutcome::Failed;
        }
        {
            std::ofstream out{temp, std::ios::binary | std::ios::trunc};
            if (!out) {
                AURORA_LOG_ERROR("platform", "write_private_file cannot open the scratch: ", temp_text);
                discard_scratch(temp);
                return PrivateWriteOutcome::Failed;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out) {
                AURORA_LOG_ERROR("platform", "write_private_file failed writing the scratch: ", temp_text);
                discard_scratch(temp);
                return PrivateWriteOutcome::Failed;
            }
        }
        auto ec = std::error_code{};
        std::filesystem::rename(temp, target, ec);
        if (ec) {
            AURORA_LOG_ERROR("platform", "write_private_file cannot put it in place: ",
                            std::string{path}, " - ", ec.message());
            discard_scratch(temp);
            return PrivateWriteOutcome::Failed;
        }
        return PrivateWriteOutcome::Written;
    }
    AURORA_LOG_ERROR("platform", "write_private_file found no free scratch name next to: ",
                    std::string{path});
    return PrivateWriteOutcome::NoScratchName;
}

}  // namespace borealis::platform
