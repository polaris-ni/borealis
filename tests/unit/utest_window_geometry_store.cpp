/// 测试类型: unit
/// 目标单元: src/config/window_geometry_store.{h,cpp}（`SPEC.FEAT.WS.03` 稿 D9／§6 批 5）
/// 测试说明: 逐窗几何那份存储的四件事——键的形态、绑到哪个文件、提交之后还能不能读回来、以及
///           框架那条自动链**读的是哪一层键**。第三与第四件是对着稿 §0 的 F26 写的：
///           ①`main` 与 `window-2`、`window-3`…（创建序，序号从 2 起）——主窗只有一个名字，
///             故第一扇不能既叫 `main` 又叫 `window-1`；
///           ②文件绑到 `<目录>/windows.json`，且**顶层**键才可被自动链读到：框架
///             `restore_geometry_for` 调的是 `load_window_geometry(Preferences&, key)` 那一枚重载，
///             把几何写进 `group("windows")` 就得到「存得回、恢复取不到」——这条错位在生产里只表现
///             为「窗口每次都回到 960×640」，没有任何日志会说键找错了层，所以它必须有证人；
///           ③`flush()` 之后换一个实例读同一份文件仍拿得到同一格几何（框架无隐式 I/O，落盘时机
///             是本件与装配层的全部职责）；
///           ④可用性回退与失败原因：越界几何经 `load_window_geometry` 折成空值（POSIX 腿那张合成的
///             1920×1080 单显，见 F26），写不进目标时 `flush()` 回一条 ASCII 原因而不是抛出。
///           全部不碰 UI；只碰临时目录里的一个 JSON 文件。

#include "config/window_geometry_store.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "aurora/app/window_geometry.h"
#include "aurora/core/json.h"
#include "aurora/preferences/preferences.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_window_geometry_store {

namespace {

using borealis::config::kWindowGeometryFileName;
using borealis::config::WindowGeometryStore;
using borealis::config::window_persist_id;

/// @brief 一次几何快照（物理像素，与框架的坐标系同源）。
[[nodiscard]] auto geometry_at(float x, float y, float width, float height) -> au::WindowGeometry {
    au::WindowGeometry out{};
    out.origin = au::Point{.x = x, .y = y};
    out.size = au::Size{.width = width, .height = height};
    out.mode = au::WindowMode::Normal;
    out.display_id = 0;
    return out;
}

/// @brief 用例自己的夹具目录：一件一个名字，两份文件不混。
[[nodiscard]] auto fixture_dir(std::string_view name) -> std::filesystem::path {
    const auto path = std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

}  // namespace

AURORA_TEST_CASE(the_first_window_is_main_and_the_rest_number_from_two) {
    AURORA_TEST_CHECK_EQ(window_persist_id(0U), std::string{"main"});
    AURORA_TEST_CHECK_EQ(window_persist_id(1U), std::string{"window-2"});
    AURORA_TEST_CHECK_EQ(window_persist_id(2U), std::string{"window-3"});
    AURORA_TEST_CHECK_EQ(window_persist_id(11U), std::string{"window-12"});
    // 键里没有点号：框架的 `reconcile` 会把键里的点拆成一层层嵌套（裁决 7.27⑤），拆了就找不到键。
    AURORA_TEST_CHECK(window_persist_id(7U).find('.') == std::string::npos);
}

AURORA_TEST_CASE(the_store_binds_windows_json_under_the_given_directory) {
    const auto dir = fixture_dir("bind");
    WindowGeometryStore store{dir / kWindowGeometryFileName};
    AURORA_TEST_CHECK_EQ(store.path(), dir / kWindowGeometryFileName);
    AURORA_TEST_REQUIRE(store.preferences() != nullptr);
    // 文件模式：`flush` 才有效，内存模式的 flush 是恒失败的空转。
    AURORA_TEST_CHECK(store.preferences()->is_persistent());
    // 与 `borealis.json` 同一层的另一枚名字：几何绝不进设置本体（稿 D9 的那条「跨机导出即坏值」）。
    AURORA_TEST_CHECK_NE(store.path().string(), (dir / "borealis.json").string());
}

AURORA_TEST_CASE(a_flushed_geometry_is_read_back_by_another_instance) {
    const auto dir = fixture_dir("round_trip");
    const auto file = dir / kWindowGeometryFileName;
    {
        WindowGeometryStore store{file};
        au::save_window_geometry(*store.preferences(), window_persist_id(1U), geometry_at(120, 80, 1280, 720));
        AURORA_TEST_CHECK(!store.flush().has_value());
    }
    AURORA_TEST_CHECK(std::filesystem::exists(file));  // 没 flush 就没有这份文件
    WindowGeometryStore reopened{file};
    const auto restored = au::load_window_geometry(*reopened.preferences(), window_persist_id(1U));
    AURORA_TEST_REQUIRE(restored.has_value());
    AURORA_TEST_CHECK_EQ(restored->size.width, 1280.0F);
    AURORA_TEST_CHECK_EQ(restored->size.height, 720.0F);
    AURORA_TEST_CHECK_EQ(restored->origin.x, 120.0F);
}

AURORA_TEST_CASE(a_group_key_is_invisible_to_the_framework_auto_chain) {
    // F26② 的那一处错位：自动链读的是顶层的 `persist_id`，分组里那一格它看不见。本件不许把几何
    // 写进 `group("windows")`，这条用例钉的就是「写错了层会怎样」——两跳都真做，才证得到分叉。
    const auto dir = fixture_dir("group_trap");
    WindowGeometryStore store{dir / kWindowGeometryFileName};
    const auto keyed = window_persist_id(0U);
    au::save_window_geometry(store.preferences()->group("windows"), keyed, geometry_at(0, 0, 1024, 768));
    AURORA_TEST_CHECK(!store.flush().has_value());

    WindowGeometryStore reopened{dir / kWindowGeometryFileName};
    AURORA_TEST_CHECK(au::load_window_geometry(reopened.preferences()->group("windows"), keyed).has_value());
    AURORA_TEST_CHECK(!au::load_window_geometry(*reopened.preferences(), keyed)
                          .has_value());  // ← 顶层没有：恢复腿取的就是这一层
}

AURORA_TEST_CASE(an_unusable_geometry_falls_back_to_nothing) {
    const auto dir = fixture_dir("unusable");
    WindowGeometryStore store{dir / kWindowGeometryFileName};
    // POSIX 腿的 `list_displays()` 是张合成的 1920×1080 单显（F26④）：完全落在它之外的几何按
    // 「显示器被拔掉」处理，回空值让调用方保留默认尺寸，而不是把窗口恢复到摸不着的地方。
    au::save_window_geometry(*store.preferences(), "off_screen", geometry_at(90000.0F, 90000.0F, 800, 600));
    au::save_window_geometry(*store.preferences(), "zero_size", geometry_at(10, 10, 0.0F, 600.0F));
    AURORA_TEST_CHECK(!au::load_window_geometry(*store.preferences(), "off_screen").has_value());
    AURORA_TEST_CHECK(!au::load_window_geometry(*store.preferences(), "zero_size").has_value());
    // 部分越界仍算可用（多屏拼接、任务栏遮挡不该拒绝恢复）。
    au::save_window_geometry(*store.preferences(), "half_out", geometry_at(1900.0F, 1000.0F, 800, 600));
    AURORA_TEST_CHECK(au::load_window_geometry(*store.preferences(), "half_out").has_value());
}

AURORA_TEST_CASE(a_failed_flush_comes_back_as_a_reason_not_an_exception) {
    const auto dir = fixture_dir("flush_blocked");
    std::error_code ec;
    // 路径上先占一枚**普通文件**：框架的 `auto_create_dir` 造不出「文件下面的目录」，于是这一跳必然失败。
    const auto blocker = dir / "blocker";
    {
        auto out = std::ofstream{blocker, std::ios::binary | std::ios::trunc};
        out << "not a directory";
    }
    WindowGeometryStore store{blocker / kWindowGeometryFileName};
    au::save_window_geometry(*store.preferences(), window_persist_id(0U), geometry_at(0, 0, 800, 600));
    const auto reason = store.flush();
    AURORA_TEST_REQUIRE(reason.has_value());
    AURORA_TEST_CHECK(!reason->empty());  // 装配层要拿它落一条 WARN，空串会把这条日志变成噪音
}

}  // namespace borealis::test_cases::utest_window_geometry_store
