# FRAMEWORK_TASK_CONN10.md — `SPEC.FEAT.CONN.10` 目录选择器在 Aurora 框架侧的可派发任务书

> 本文件是 Borealis `SPEC.FEAT.CONN.10`（SSH 密钥管理器）在 Aurora 框架侧的依赖分析与回货判据。
> 依据：Borealis 对 `include/aurora/app/file_dialog.h` 与 `src/aurora/app/file_dialog_win32.cpp` 的读源实测，加本机（Linux，Wayland/GNOME 会话）对桌面侧选择器通道的探针实测，均为 2026-10-10。
> **结论先行**：框架的**公共 API 三条腿齐备**（`open_file` / `save_file` / `open_folder` + 三个 headless 钩子 + `interactive` 开关），真实实现**只有 Win32 一条腿**；非 Win32 是头文件里的内联回退——读钩子、否则回空串，**不弹任何 UI、也不消费 `Options`、更不看 `interactive`**。因此 Borealis 侧「自选密钥目录」的录入腿在 Linux 上点「＋目录」等价取消。缺的是**一条 POSIX 真实现**，不是缺 API。
> 登记形态见 `codespec/SPECIFICATIONS.md` §7 裁决 **7.106** 与附录 A.2 的 **G41** 行；本仓留痕在实现批的 `src/main.cpp`（`pick_key_dir`，按稿 `codespec/UI_KEYS.draft.md` §4 布局落地时挂 `TODO(SPEC.FEAT.CONN.10)`）。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状（读源实测） |
|:---|:---|:---|
| 三个入口的公共签名 | `include/aurora/app/file_dialog.h` | `open_file(const Options&) -> Result<std::vector<std::string>>`、`save_file(...) -> Result<std::string>`、`open_folder(...) -> Result<std::string>`，皆 `[[nodiscard]]`，默认实参 `Options{}` |
| `Options` | 同上 | `title` / `initial_dir` / `filters` 三项；**Win32 腿消费**，非 Win32 回退 `(void)opts` 丢弃 |
| headless 钩子 | 同上 | `headless_open_result`（列表）、`headless_save_result`、`headless_folder_result`（各单值）；置非空即直接返回，两腿共用同一份语义 ⇒ **接线判据可无头证** |
| `interactive` 开关 | 同上 | 注释口径是「`false` 时自动化环境直接回空、避免卡在等待用户」；**实测只有 Win32 腿读了它**（`file_dialog_win32.cpp` 三处 `if (!interactive)`），非 Win32 回退无条件回空，效果相同、语义来源不同 |
| 真实实现 | `src/aurora/app/file_dialog_win32.cpp` | `IFileOpenDialog` / `IFileSaveDialog`（目录模式即 `FOS_PICKFOLDERS`），COM 初始化与创建失败才回 `Error`；取消回 `Ok(空)` |
| 目录约定 | 头注 `Thread: main-thread only` | 两腿一致，Borealis 只在 UI 线程的用户动作当场调用 |

**Borealis 消费现状**：`src/main.cpp` 的 `picked_save_path()` / `picked_open_path()` 与 `src/ui/settings_panel.h` 的 `pick_export_path` / `pick_import_path` 两枚 Hook，已经是本任务书要求的消费形态——**面板不碰对话框、装配层取路径、空串＝取消或平台起不来同途**。`SPEC.FEAT.CONN.10` 的「＋目录」腿照抄该形态，故本仓侧不新增框架概念，只多一个消费方。

---

## 2 缺的那一条腿（POSIX 真实现）

- **形态**：`#else` 分支里的三个 `inline` 函数换成真实实现（或新增 `src/aurora/app/file_dialog_posix.cpp` 之类 TU 并在 CMake 里按平台挑选），**公共签名一字不改**。
- **必须保留**：`headless_*_result` 钩子优先、`interactive == false` 时不弹 UI 直接回空——否则本仓与框架自己的 GUI 测试会卡在等用户。
- **必须真消费**：`Options::title`、`Options::initial_dir`（`filters` 对 `open_folder` 无意义，可忽略但要写明忽略）。当前回退把 `Options` 整个丢掉，是「静默不生效」而不是「报错」。
- **取消不是失败**：用户取消、目录不可选、无可用通道 ⇒ 一律 `Ok(空串)`；只有系统级异常回 `Error`。本仓据此把「取消」与「平台没实现」并成一条不报错的路径（同 `settings_panel` 的导出腿）。
- **不引入链接期新依赖**（本仓立场，非框架硬约束）：Borealis 的 `vcpkg.json` 现在只有 `libssh` 与 Windows 侧 `libiconv`，加 GTK/Qt 会连带把桌面环境依赖推给所有消费者。

### 2.1 本机（Linux / Wayland / GNOME）三条候选通道的实测读数

| 通道 | 实测 | 评估 |
|:---|:---|:---|
| **xdg-desktop-portal** | `org.freedesktop.portal.Desktop` 在册（`busctl --user list` 命中 1 条），`/org/freedesktop/portal/desktop` 可 introspect，impl `xdg-desktop-por`（GNOME 与 GTK 两份）都在跑；`FileChooser` 接口暴露 `OpenFile` / `SaveFile` / `SaveFiles` 三法，**签名是 `(s parent_window, s title, a{sv} options) -> o handle`** | 沙箱内唯一**无需装额外桌面工具**即存在的通道，且是 Wayland 下的正统做法。代价：方法**异步**（回 handle，结果走 `org.freedesktop.portal.Request::Response` 信号），要 DBus 客户端与事件循环；`gdbus` / `dbus-send` 二进制本机在。⚠ **该版本 introspect 未见 `OpenDirectory`** ⇒ 若走此路，目录模式要靠 `OpenFile` 的 `options` 里那档 `multiple`/`directory` 扩展属性，**存在性未实测**，框架侧需先验证再定形态 |
| **`zenity`** | `/usr/bin/zenity` 在（`kdialog` / `yad`  absent） | 一条 `--file-selection --directory` 子进程即可同步拿结果，工程量最小；但把「装了 zenity」变成运行时前提，且 GNOME/KDE/无 DE 三态要分别兜 |
| **Aurora 自绘目录浮层** | 用框架自家控件（`Tree`/`List` + 命中盒）画，零外部依赖 | 观感与 Win32 不一致，但两腿行为自持、可无头断言；工程量最大，属框架级组件而非补丁 |

**本仓不替框架选路**：三条任一都能闭合本需求那一腿，选路权在 Aurora（其 AGENTS 的依赖与分层约束优先）。本仓只要求 §3 的判据成立。

---

## 3 回货判据（四条）

1. **签名与语义不变**：`open_folder()` 在 POSIX 腿返回用户所选**绝对路径**；取消回 `Ok(空串)`；`headless_folder_result` 置非空时**仍然优先返回钩子**且不弹 UI；`interactive == false` 时不弹 UI。
2. **`Options` 真生效**：给定 `title` 与 `initial_dir` 时，弹出的框以该标题起、以该目录为初始位置（判据形态：以 `initial_dir` 存在性为观测点，而非截图比字）。
3. **无头可证**：钩子路径在两腿走同一份代码语义 ⇒ 本仓与框架侧可在不开 GUI 的情况下断「给了钩子就拿到、没给就回空」，且这条断言在 Win32 与 POSIX 上同绿。
4. **不静默说谎**：若某平台态确实拿不出选择器（无 portal、无 zenity、无 DISPLAY），实现**必须** `AURORA_LOG_WARN` 留痕后回空，而不是弹一个半残框或回假路径——本仓把「空串＝取消或平台起不来」并成一条路，靠的就是这条 WARN 可归因。

---

## 4 配套用例（框架侧）

- `utest_file_dialog_headless_folder_hook`：置 `headless_folder_result` → `open_folder()` 回该值并清空钩子；钩子空 → 回空串；`interactive=false` 且钩子空 → 回空串且不进入任何 UI 路径。
- `utest_file_dialog_options_consumed`（POSIX 真机腿）：以临时目录作 `initial_dir`，断返回路径落在其内或等于它。
- `itest_file_dialog_cancel_is_not_error`：取消路径回 `Ok`（非 `Error`），空串。
- 若走 portal 路：`utest_file_dialog_portal_probe`——探测不到 `org.freedesktop.portal.Desktop` 时按判据 4 留 WARN 回空，而不是挂住（**超时必须有上界**，本仓启动路径不能被一个不可达的总线卡死）。

---

## 5 本仓接货复验形态

- **落点**：`src/main.cpp` 的 `pick_key_dir`（`SPEC.FEAT.CONN.10` 批 3）——`au::file_dialog::open_folder({.title="选择密钥目录", .initial_dir=<当前目录表首项>})`，取到即交 `key_model` 规范化（去重、`~/.ssh` 恒在不可移、存在性）后落 `Settings.key_dirs` 并重扫；空串整条不跑、不留痕。
- **接货前**（现状）：Linux 腿该按钮**等价取消**——判据仍在（`itest_keys_panel` 经 `headless_folder_result` 证接线），真机走查项如实记「Linux 腿无响应」，不称「可用」。
- **接货后复验**：撤 `TODO(SPEC.FEAT.CONN.10)`；`SPECIFICATIONS.md` 的 `SPEC.FEAT.CONN.10` 现状句与 §7 裁决 **7.106** 就地更正为「已闭合」、附录 A.2 的 **G41** 行改为「已闭合」并写回货形态；`CHANGELOG.md` 补一条接货复验版本；`codespec/UI_KEYS.draft.md` §7 的延后项「POSIX 真选择器」撤除。
- **变异自证**：m1 装配层不接 `pick_key_dir` → 面板「＋目录」点了不落盘（红）；m2 钩子置空 → 整条不跑、目录表一字不变（证明「取消」不产生副作用）；m3 传入不可达目录 → 条目保留并行内留痕，不在装载时偷偷删。

---

## 6 工作量评估

框架侧新增：POSIX 真实现一条腿。

- 走 **zenity** 路：约 40–60 行（进程拉起 + 同步等待 + 退出码兜底）+ 一份单测 ⇒ **小**，但带运行时前提。
- 走 **portal** 路：需 DBus 客户端与异步请求/信号收尾（含超时上界），且要先验证目录模式那档 options 属性是否存在 ⇒ **中**，Wayland 下最正统。
- 走 **自绘浮层** 路：新增框架级组件（目录树 + 命中 + 键盘导航）⇒ **大**，两腿观感与行为自持，可无头断言最强。

**本仓优先级**：**中**——不阻塞 `SPEC.FEAT.CONN.10` 其余三子项与删除/生成/推送腿的落地（那几腿与选择器无关），只阻塞「自选目录」在 Linux 腿的真机可用性。故本棒按 AGENTS §5.1 出件分流，本仓不等不绕、也不自建替代实现。
