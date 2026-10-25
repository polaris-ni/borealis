// ============================================================
// SFTP 浏览器面板实现（src/ui/sftp_panel.cpp）
// ------------------------------------------------------------
// 布局锚 `codespec/UI_SFTP.draft.md` §4（D1–D9，裁决 7.95）。线程分工：
// UI 线程只组装树、取收件箱、置取消旗标；SftpClient 与 std::filesystem 的
// 一切 IO 都在单 worker 线程上串行跑（D7①）。面板卡片是非模态右停靠浮层
// （CenterRight 对齐，先例 connection_sidebar 的 CenterLeft 镜像）；
// 删除确认 / 改名输入才走模态 Dialog（D6①）。
// ============================================================

#include "sftp_panel.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "aurora/core/color.h"
#include "aurora/core/log.h"
#include "aurora/widget/alignment.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/text.h"

#include "conn/reconnect.h"  // sftp_error_drops_link（裁决 7.99 D7④ 的掉线类判据）
#include "sftp_format.h"
#include "settings_i18n.h"
#include "settings_panel.h"

namespace borealis::ui {

namespace {

constexpr float kCardWidth = 560.0F;
constexpr float kPaneWidth = 244.0F;
constexpr float kTransferColumnWidth = 44.0F;
constexpr float kNameWidth = 116.0F;
constexpr float kSizeWidth = 52.0F;
constexpr float kMtimeWidth = 96.0F;
constexpr float kPermWidth = 56.0F;

/// @brief 收件箱容量：有界交换（SPEC.NF.PERF.06）——worker 只会领先 UI 几帧，
/// 塞满说明 UI 已长时间不来取，过期的旧列表/完成态不如最新一条有用，丢最旧。
constexpr std::size_t kInboxCapacity = 64;

/// @brief 双击判定窗口：框架按钮只有单击回调，500 ms 内同条目二击按 D3① 进入。
constexpr auto kDoubleClickWindow = std::chrono::milliseconds{500};

/// @brief 首败问人闸的定时限（稿 §2 的第四出口）：panel 的 tick 与对话框都不是保证
/// 存在的对象，无人应答到限按「中止」自收，免得 worker 卡进不了析构 join。
constexpr int kBatchAskTimeoutMs = 60'000;

/// @brief 本地条目排序：目录置顶、名字 ASCII 大小写折叠、平手按字节序——与
/// conn::sort_entries 同一口径（模型头注释），两栏行序一致。
[[nodiscard]] auto name_fold_less(std::string_view lhs, std::string_view rhs) -> bool {
    const auto fold = [](unsigned char c) -> char {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32U : c);
    };
    for (std::size_t i = 0; i < lhs.size() && i < rhs.size(); ++i) {
        if (const char a = fold(lhs[i]), b = fold(rhs[i]); a != b) {
            return a < b;
        }
    }
    if (lhs.size() != rhs.size()) {
        return lhs.size() < rhs.size();
    }
    return lhs < rhs;
}

[[nodiscard]] auto sort_local_entries(std::vector<SftpPanel::LocalEntry> entries)
    -> std::vector<SftpPanel::LocalEntry> {
    std::stable_sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) -> bool {
        if (lhs.is_directory != rhs.is_directory) {
            return lhs.is_directory;
        }
        return name_fold_less(lhs.name, rhs.name);
    });
    return entries;
}

/// @brief 次级按钮的统一形态（先例 connection_sidebar 的组装参数）。
[[nodiscard]] auto make_button(const std::string &label, float min_width,
                               std::function<void()> on_click) -> std::shared_ptr<aurora::Button> {
    const auto chrome = settings_chrome();
    auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = label,
        .color = chrome.control_bg,
        .on_color = chrome.text,
        .border_color = chrome.card_line,
        .border_width = 1.0F,
        .min_width = min_width,
    });
    button->set_on_click(std::move(on_click));
    return button;
}

[[nodiscard]] auto make_text(const std::string &content, aurora::Color color) -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(aurora::TextProps{.content = content, .text_color = color});
}

}  // namespace

// ============================================================
// 生命周期
// ============================================================

SftpPanel::SftpPanel(aurora::OverlayHost &host, Hooks hooks) : host_{&host}, hooks_{std::move(hooks)} {
    // worker 与面板同生共死：线程在构造即起（拨号请求随 open() 投递），
    // 析构置停机位后 join，SftpClient 的析构腿兜底断链。
    worker_ = std::thread([this]() -> void { run_worker(); });
}

// 先摘后换（头文件的判据）：`close()` 先置取消旗再投断链，并把对话框层与主卡片按既有次序从**旧宿主**
// 摘回。搬窗等于换了一条会话生命周期——列目录的在途传输不跟着搬到另一扇窗继续跑。
auto SftpPanel::set_overlay_host(aurora::OverlayHost &host) -> void {
    if (host_ == &host) {
        return;
    }
    close();
    host_ = &host;
}

SftpPanel::~SftpPanel() {
    close();
    {
        std::lock_guard<std::mutex> lock{requests_mutex_};
        worker_stop_ = true;
        requests_.clear();
    }
    requests_cv_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

auto SftpPanel::open() -> void {
    if (open_) {
        return;
    }
    open_ = true;
    state_ = ConnState::Connecting;
    last_error_ = conn::SftpError::None;
    remote_path_ = "/";
    local_path_.clear();
    selected_remote_.clear();
    selected_local_.clear();
    transfer_label_.clear();
    batch_trace_ = BatchTrace{};
    transfers_posted_ = 0;
    transfers_done_ = 0;
    cancel_flag_ = false;

    std::optional<conn::SshProfile> profile;
    if (hooks_.current_ssh_profile != nullptr) {
        profile = hooks_.current_ssh_profile();
    }
    if (!profile.has_value()) {
        state_ = ConnState::NotSsh;  // D8① 空态：非 SSH 标签不拨号，只给状态行。
    }
    overlay_index_ = host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(build_root())});
    if (profile.has_value()) {
        std::optional<std::string> secret;
        if (hooks_.resolve_secret != nullptr) {
            secret = hooks_.resolve_secret();
        }
        conn::SshAlgorithmOverrides algorithms;
        if (hooks_.algorithm_overrides != nullptr) {
            algorithms = hooks_.algorithm_overrides();
        }
        Request req{.kind = Request::Kind::Connect,
                    .profile = std::move(*profile),
                    .secret = std::move(secret),
                    .algorithms = std::move(algorithms)};
        post(std::move(req));
    }
}

auto SftpPanel::close() -> void {
    if (!open_) {
        return;
    }
    // 先置取消再投 Disconnect：当前传输若在跑，worker 走完取消清理即到断链请求。
    cancel_flag_ = true;
    // 收摊档（稿 §2 第四出口）：批次若正停在「问人」闸上，无人可答了，闸按「中止」自答，
    // worker 才能走完清理腿弹出后面的 Disconnect——析构 join 才有尽头。
    batch_latch_.abort();
    post(Request{.kind = Request::Kind::Disconnect});
    clear_state();
}

auto SftpPanel::clear_state() -> void {
    close_dialog();
    if (overlay_index_.has_value()) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    transfer_text_.reset();
    progress_bar_.reset();
    remote_entries_.clear();
    local_entries_.clear();
    transfer_label_.clear();
    batch_trace_ = BatchTrace{};
    // 重新打开＝新的一条会话生命周期：一回重拨的旗标随之复位（裁决 7.99 D7④ 细则 (ii)）。
    redial_armed_ = true;
    open_ = false;
}

// ============================================================
// worker 线程
// ============================================================

auto SftpPanel::post(Request req) -> void {
    {
        std::lock_guard<std::mutex> lock{requests_mutex_};
        requests_.push_back(std::move(req));
    }
    requests_cv_.notify_one();
}

auto SftpPanel::push_inbox(InboxMessage msg) -> void {
    std::lock_guard<std::mutex> lock{inbox_mutex_};
    if (inbox_.size() >= kInboxCapacity) {
        inbox_.pop_front();  // 有界交换：丢最旧保最新（容量锚见常量注释）。
    }
    inbox_.push_back(std::move(msg));
}

auto SftpPanel::store_progress(const conn::SftpProgress &progress) -> void {
    // 块级回调只覆写最新快照（D9①）：UI 每帧至多取走一次，节流由帧粒度天然满足。
    std::lock_guard<std::mutex> lock{inbox_mutex_};
    progress_ = progress;
    progress_active_ = true;
}

auto SftpPanel::push_remote_listing(const std::string &dir) -> void {
    auto entries = client_.list_directory(dir);
    if (entries.has_value()) {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::RemoteListing,
                                .ok = true,
                                .path = dir,
                                .remote_entries = conn::sort_entries(std::move(*entries))});
    } else {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::RemoteListing,
                                .error = client_.last_error(),
                                .path = dir});
    }
}

auto SftpPanel::push_local_listing(const std::filesystem::path &dir) -> void {
    std::vector<LocalEntry> entries;
    std::error_code ec;
    // skip_permission_denied：无权目录跳过而不是整列失败（远端腿同语义由服务器给）。
    std::filesystem::directory_iterator it{
        dir, std::filesystem::directory_options::skip_permission_denied, ec};
    if (ec) {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::LocalListing,
                                .error = conn::SftpError::LocalIo,
                                .path = dir.string()});
        return;
    }
    for (const std::filesystem::directory_entry &entry : it) {
        std::error_code entry_ec;
        LocalEntry row;
        row.name = entry.path().filename().string();
        row.is_symlink = entry.is_symlink(entry_ec);
        row.is_directory = !entry_ec && !row.is_symlink && entry.is_directory(entry_ec);
        if (!entry_ec && !row.is_directory) {
            row.size_bytes = entry.file_size(entry_ec);
        }
        const auto ftime = entry.last_write_time(entry_ec);
        if (!entry_ec) {
            row.mtime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::clock_cast<std::chrono::system_clock>(ftime)
                                .time_since_epoch())
                            .count();
        }
        entries.push_back(std::move(row));
    }
    push_inbox(InboxMessage{.kind = InboxMessage::Kind::LocalListing,
                            .ok = true,
                            .path = dir.string(),
                            .local_entries = sort_local_entries(std::move(entries))});
}

auto SftpPanel::op_and_relist(bool ok, const std::string &relist_dir) -> void {
    push_inbox(InboxMessage{.kind = InboxMessage::Kind::OpDone,
                            .ok = ok,
                            .error = ok ? conn::SftpError::None : client_.last_error()});
    push_remote_listing(relist_dir);
}

auto SftpPanel::create_remote_file(const std::string &remote_path, const std::string &relist_dir) -> void {
    // D4②：新建空文件＝本地 0 字节临时件走 upload 腿——传输层的组合，不改腿。
    std::error_code ec;
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path(ec) /
        ("borealis-sftp-empty-" + std::to_string(++temp_seq_));
    bool ok = false;
    if (!ec) {
        {
            std::ofstream creator{temp, std::ios::binary | std::ios::trunc};
        }
        ok = client_.upload_file(temp, remote_path,
                                 [this](const conn::SftpProgress &progress) -> void {
                                     store_progress(progress);
                                 },
                                 cancel_flag_);
        std::error_code remove_ec;
        std::filesystem::remove(temp, remove_ec);  // 临时件无论成败都不留。
    }
    if (!ok) {
        AURORA_LOG_WARN("sftp", "create empty file failed: ", remote_path);
        // 临时件没建出来时传输腿没被碰过，last_error 是上一次的残值，归到 LocalIo。
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::OpDone,
                                .ok = false,
                                .error = ec ? conn::SftpError::LocalIo : client_.last_error()});
        push_remote_listing(relist_dir);
        return;
    }
    op_and_relist(true, relist_dir);
}

auto SftpPanel::enumerate_remote_tree(std::string_view dir, unsigned level,
                                      const conn::BatchLimits &limits, conn::BatchSkips &skips,
                                      std::size_t &files_seen, bool &list_failed)
    -> std::optional<conn::BatchDirNode> {
    auto entries = client_.list_directory(dir);
    if (!entries.has_value()) {
        list_failed = true;
        return std::nullopt;
    }
    conn::BatchDirNode node;
    for (const conn::SftpEntry &entry : *entries) {
        // 计数线的代表件收满即止：拒绝档不携带残树，多列一层只会白烧往返（D4①）。
        if (files_seen > limits.max_files) {
            break;
        }
        switch (entry.kind) {
        case conn::SftpEntryKind::Directory:
            if (level + 1 > limits.max_depth) {
                // 深度 max_depth+1 的**空占位节点**就足以让折叠件判越线：再往下列是白烧。
                node.dirs.push_back(conn::BatchDirNode{.name = entry.name});
                break;
            }
            {
                auto child = enumerate_remote_tree(conn::sftp_path_join(dir, entry.name),
                                                   level + 1, limits, skips, files_seen,
                                                   list_failed);
                if (list_failed) {
                    return std::nullopt;  // 残树不往上拼：断过一层的枚举整批作废。
                }
                child->name = entry.name;  // 子节点自身不知自己名字，由父侧回填（根不用此字段）。
                node.dirs.push_back(std::move(*child));
            }
            break;
        case conn::SftpEntryKind::File:
            // 远端 File 的 size_bytes 是 attr 现值（F1），总量口径直接可信。
            node.files.push_back(conn::BatchFileNode{
                .name = entry.name, .size_bytes = entry.size_bytes, .size_known = true});
            ++files_seen;
            break;
        case conn::SftpEntryKind::Symlink:
            ++skips.symlinks;  // D1①：条目卡不跟随软链是既有模型口径，批次只计数。
            break;
        case conn::SftpEntryKind::Other:
            ++skips.others;
            break;
        }
    }
    return node;
}

auto SftpPanel::enumerate_local_tree(const std::filesystem::path &dir, unsigned level,
                                     const conn::BatchLimits &limits, conn::BatchSkips &skips,
                                     std::size_t &files_seen, bool &list_failed)
    -> std::optional<conn::BatchDirNode> {
    std::error_code ec;
    // skip_permission_denied 与本地列栏腿同口径；目录迭代只走 worker（AGENTS §4.5 第 25 条）。
    std::filesystem::directory_iterator it{
        dir, std::filesystem::directory_options::skip_permission_denied, ec};
    if (ec) {
        list_failed = true;
        return std::nullopt;
    }
    conn::BatchDirNode node;
    for (const std::filesystem::directory_entry &entry : it) {
        if (files_seen > limits.max_files) {
            break;
        }
        std::error_code link_ec;
        if (entry.is_symlink(link_ec)) {
            ++skips.symlinks;  // D1① 双向同规：本地软链也只计数不跟随。
            continue;
        }
        std::error_code kind_ec;
        const bool is_dir = entry.is_directory(kind_ec);
        if (!kind_ec && is_dir) {
            if (level + 1 > limits.max_depth) {
                node.dirs.push_back(
                    conn::BatchDirNode{.name = entry.path().filename().string()});
                continue;
            }
            auto child = enumerate_local_tree(entry.path(), level + 1, limits, skips, files_seen,
                                              list_failed);
            if (list_failed) {
                return std::nullopt;
            }
            child->name = entry.path().filename().string();
            node.dirs.push_back(std::move(*child));
            continue;
        }
        std::error_code regular_ec;
        const bool is_regular = entry.is_regular_file(regular_ec);
        if (kind_ec || regular_ec || !is_regular) {
            ++skips.others;  // 特殊文件与读不出形状的坏 inode：跳过计数，不为一颗断 inode 停整批。
            continue;
        }
        std::error_code size_ec;
        const std::uint64_t size = static_cast<std::uint64_t>(entry.file_size(size_ec));
        node.files.push_back(conn::BatchFileNode{
            .name = entry.path().filename().string(),
            .size_bytes = size_ec ? 0U : size,
            .size_known = !size_ec});
        ++files_seen;
    }
    return node;
}

auto SftpPanel::create_remote_mirror(const std::string &base,
                                     const std::vector<std::string> &dirs) -> bool {
    // 折叠件的 dirs 是父先于子的入序清单（push 在递归之前），逐级 mkdir 天然满足依赖。
    for (const std::string &rel : dirs) {
        if (cancel_flag_) {
            return false;
        }
        if (!client_.make_directory(conn::sftp_normalize(base + "/" + rel))) {
            return false;
        }
    }
    return true;
}

auto SftpPanel::run_batch(const Request &req) -> void {
    const bool download = req.kind == Request::Kind::BatchDownload;
    // 排队期间被取消（close/取消钮先于出队落旗）：不烧预枚举的往返，直接按取消收摊。
    if (cancel_flag_) {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                                .ok = false,
                                .is_download = download,
                                .error = conn::SftpError::Cancelled});
        return;
    }
    const std::string root_name =
        download ? std::string{conn::sftp_basename_of(req.a)} : req.local.filename().string();

    // ① 预枚举 + 折叠：守卫线与枚举失败都在纯逻辑件里判（plan_batch_transfer，有 utest 证人）。
    conn::BatchLimits limits{};  // D4①：缺省两线（深度 64 / 件数 100 000）即模型定义。
    conn::BatchSkips skips{};
    std::size_t files_seen = 0;
    bool list_failed = false;
    auto tree = download ? enumerate_remote_tree(req.a, 0, limits, skips, files_seen, list_failed)
                         : enumerate_local_tree(req.local, 0, limits, skips, files_seen,
                                                list_failed);
    auto plan = conn::plan_batch_transfer(tree.value_or(conn::BatchDirNode{}), "", limits,
                                          list_failed || !tree.has_value());
    plan.skips = skips;  // 跳过件的分类计数只有枚举腿看得见（形状树里没有 kind）。

    if (!plan.engaged) {
        // D4① fail-closed：整批拒绝，不进传输、不建目录，只落留痕（不弹框）。
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                                .is_download = download,
                                .batch_reject = plan.reject});
        return;
    }

    // 批次对传输条与排队计数恒呈「一件在途」（F3）：开始与收摊各一条消息，件数只进留痕。
    push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferBegin,
                            .is_download = download,
                            .path = root_name});

    const std::filesystem::path local_base =
        download ? req.local / std::filesystem::path{root_name} : req.local;
    const std::string remote_base =
        download ? std::string{req.a} : conn::sftp_path_join(req.a, root_name);

    // ② 镜像建目录（先判取消：发起钮按下到此处之间用户可能已收摊）。
    if (cancel_flag_) {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                                .ok = false,
                                .is_download = download,
                                .error = conn::SftpError::Cancelled});
        return;
    }
    if (download) {
        std::error_code ec;
        std::filesystem::create_directories(local_base, ec);
        for (const std::string &rel : plan.dirs) {
            std::filesystem::create_directories(local_base / std::filesystem::path{rel}, ec);
            if (ec) {
                break;
            }
        }
        if (ec) {
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                                    .ok = false,
                                    .is_download = download,
                                    .error = conn::SftpError::LocalIo});
            return;
        }
    } else if (!create_remote_mirror(remote_base, plan.dirs)) {
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                                .ok = false,
                                .is_download = download,
                                .error = cancel_flag_ ? conn::SftpError::Cancelled
                                                       : client_.last_error()});
        return;
    }

    // ③ 逐文件投递：进度折进批次总账（fold_batch_progress，人拍口径③）；首败问人（稿 §2）。
    unsigned ok_count = 0;
    unsigned ask_skip_count = 0;
    unsigned fail_count = 0;
    std::uint64_t bytes_before = 0;
    conn::SftpError stop_error = conn::SftpError::None;
    for (const std::string &rel : plan.files) {
        if (cancel_flag_) {
            stop_error = conn::SftpError::Cancelled;  // 取消＝整批止（稿 §1），当前件半成品已由腿清理。
            break;
        }
        std::uint64_t file_done_final = 0;
        const auto pump = [this, &file_done_final, bytes_before, &plan](
                              const conn::SftpProgress &progress) -> void {
            file_done_final = progress.bytes_done;
            store_progress(conn::fold_batch_progress(bytes_before, progress, plan.total_bytes,
                                                     plan.bytes_complete));
        };
        const std::filesystem::path local_side = local_base / std::filesystem::path{rel};
        const std::string remote_side = conn::sftp_normalize(remote_base + "/" + rel);
        const bool file_ok =
            download ? client_.download_file(remote_side, local_side, pump, cancel_flag_)
                     : client_.upload_file(local_side, remote_side, pump, cancel_flag_);
        if (file_ok) {
            ++ok_count;
            bytes_before += file_done_final;
            continue;
        }
        const conn::SftpError err = client_.last_error();
        if (err == conn::SftpError::Cancelled || cancel_flag_) {
            stop_error = conn::SftpError::Cancelled;
            break;
        }
        // 非取消类首败：问人（答话/超时/取消/收摊四出口都在闸上，BatchDecisionLatch 有 utest）。
        push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferAsk,
                                .error = err,
                                .path = rel});
        const BatchDecision decision = batch_latch_.wait_for_decision(kBatchAskTimeoutMs);
        // 失败件已走过的字节照计入总账：回拨进度条比多算几字节更误导。
        bytes_before += file_done_final;
        if (decision == BatchDecision::Skip) {
            ++ask_skip_count;
            continue;
        }
        ++fail_count;
        stop_error = err;
        break;  // D7①：中止是缺省档——超时与收摊都落到这里。
    }

    push_inbox(InboxMessage{.kind = InboxMessage::Kind::BatchDone,
                            .ok = stop_error == conn::SftpError::None,
                            .is_download = download,
                            .error = stop_error,
                            .batch_ok = ok_count,
                            .batch_skip = ask_skip_count + plan.skips.total(),
                            .batch_fail = fail_count});
    if (download) {
        push_local_listing(req.local);  // 与单文件腿同款：落地即刷目标栏。
    } else {
        push_remote_listing(req.a);
    }
}

auto SftpPanel::run_worker() -> void {
    for (;;) {
        Request req;
        {
            std::unique_lock<std::mutex> lock{requests_mutex_};
            requests_cv_.wait(lock, [this]() -> bool { return worker_stop_ || !requests_.empty(); });
            if (worker_stop_ && requests_.empty()) {
                return;
            }
            req = std::move(requests_.front());
            requests_.pop_front();
        }
        switch (req.kind) {
        case Request::Kind::Connect: {
            client_.disconnect();
            const bool ok = client_.connect(req.profile, std::move(req.secret), req.algorithms);
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::ConnectDone,
                                    .ok = ok,
                                    .error = ok ? conn::SftpError::None : client_.last_error()});
            break;
        }
        case Request::Kind::Disconnect:
            client_.disconnect();
            {
                // 断链后旧请求全部作废：挂着只会逐条失败，再刷一串错误态。
                std::lock_guard<std::mutex> lock{requests_mutex_};
                requests_.clear();
            }
            break;
        case Request::Kind::ListRemote:
            push_remote_listing(req.a);
            break;
        case Request::Kind::ListLocal:
            // 空路径＝首列：current_path 的 IO 也只在 worker（UI 线程零阻塞 IO）。
            push_local_listing(req.a.empty() ? std::filesystem::current_path() : std::filesystem::path{req.a});
            break;
        case Request::Kind::Mkdir:
            op_and_relist(client_.make_directory(req.a), req.b);
            break;
        case Request::Kind::CreateFile:
            create_remote_file(req.a, req.b);
            break;
        case Request::Kind::RenameRemote:
            op_and_relist(client_.rename(req.a, req.b), conn::sftp_parent_of(req.a));
            break;
        case Request::Kind::RemoveRemote:
            op_and_relist(req.is_directory ? client_.remove_directory(req.a) : client_.remove_file(req.a),
                          req.b);
            break;
        case Request::Kind::Download: {
            const std::filesystem::path target =
                req.local / std::filesystem::path{req.a}.filename();
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferBegin,
                                    .is_download = true,
                                    .path = std::filesystem::path{req.a}.filename().string()});
            const bool ok =
                client_.download_file(req.a, target,
                                      [this](const conn::SftpProgress &progress) -> void {
                                          store_progress(progress);
                                      },
                                      cancel_flag_);
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferDone,
                                    .ok = ok,
                                    .error = ok ? conn::SftpError::None : client_.last_error()});
            push_local_listing(req.local);  // 下载落地即刷本地栏。
            break;
        }
        case Request::Kind::Upload: {
            const std::string target =
                conn::sftp_path_join(req.a, req.local.filename().string());
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferBegin,
                                    .path = req.local.filename().string()});
            const bool ok =
                client_.upload_file(req.local, target,
                                    [this](const conn::SftpProgress &progress) -> void {
                                        store_progress(progress);
                                    },
                                    cancel_flag_);
            push_inbox(InboxMessage{.kind = InboxMessage::Kind::TransferDone,
                                    .ok = ok,
                                    .error = ok ? conn::SftpError::None : client_.last_error()});
            push_remote_listing(req.a);  // 上传落地即刷远端栏。
            break;
        }
        case Request::Kind::BatchDownload:
        case Request::Kind::BatchUpload:
            run_batch(req);
            break;
        case Request::Kind::Shutdown:
            return;
        }
    }
}

// ============================================================
// UI 侧：取件与重建
// ============================================================

auto SftpPanel::tick() -> void {
    if (!open_) {
        return;
    }
    // D8①：活动标签不再是 SSH 会话（关闭/切走）即自动收摊，先于一切取件。
    if (hooks_.ssh_tab_alive != nullptr && !hooks_.ssh_tab_alive()) {
        close();
        return;
    }
    std::deque<InboxMessage> batch;
    {
        std::lock_guard<std::mutex> lock{inbox_mutex_};
        batch.swap(inbox_);
    }
    bool need_rebuild = false;
    while (!batch.empty()) {
        if (apply_message(batch.front())) {
            need_rebuild = true;
        }
        batch.pop_front();
    }
    refresh_progress_in_place();
    if (need_rebuild) {
        rebuild_overlay();
    }
}

auto SftpPanel::apply_message(const InboxMessage &msg) -> bool {
    switch (msg.kind) {
    case InboxMessage::Kind::ConnectDone:
        state_ = msg.ok ? ConnState::Connected : ConnState::Failed;
        last_error_ = msg.error;
        if (msg.ok) {
            // 一回掉线只重拨一回：新的活会话到手，旗标就此重新武装（裁决 7.99 D7④ 细则 (ii)）。
            // 复列断线前那个目录不需要额外的腿——下面这两条 List 用的就是原样的 `remote_path_`。
            redial_armed_ = true;
            Request remote{.kind = Request::Kind::ListRemote, .a = remote_path_};
            post(std::move(remote));
            Request local{.kind = Request::Kind::ListLocal, .a = local_path_};
            post(std::move(local));
        }
        return true;
    case InboxMessage::Kind::RemoteListing:
        if (msg.ok) {
            remote_path_ = msg.path;
            remote_entries_ = msg.remote_entries;
            last_error_ = conn::SftpError::None;
            const bool selected_survives =
                std::any_of(remote_entries_.begin(), remote_entries_.end(),
                            [this](const conn::SftpEntry &entry) -> bool {
                                return entry.name == selected_remote_;
                            });
            if (!selected_survives) {
                selected_remote_.clear();
            }
        } else {
            last_error_ = msg.error;
            // 掉线类（Network / NotConnected）⇒ 这条会话其实已经死了，本件自己爬一回（D7④）；
            // 本地侧那一支不在此列：本地列目录失败只会回 LocalIo，不属掉线类。
            if (redial_once(msg.error)) {
                return false;  // `connect_now()` 已经重建过浮层，本帧不再重复一次
            }
        }
        return true;
    case InboxMessage::Kind::LocalListing:
        if (msg.ok) {
            local_path_ = msg.path;
            local_entries_ = msg.local_entries;
            last_error_ = conn::SftpError::None;
            const bool selected_survives =
                std::any_of(local_entries_.begin(), local_entries_.end(),
                            [this](const LocalEntry &entry) -> bool {
                                return entry.name == selected_local_;
                            });
            if (!selected_survives) {
                selected_local_.clear();
            }
        } else {
            last_error_ = msg.error;
        }
        return true;
    case InboxMessage::Kind::OpDone:
        last_error_ = msg.ok ? conn::SftpError::None : msg.error;
        {
            // create-file 这类内嵌 upload 的路径没有 TransferDone，就地清进度。
            std::lock_guard<std::mutex> lock{inbox_mutex_};
            progress_active_ = false;
            progress_ = conn::SftpProgress{};
        }
        if (redial_once(msg.error)) {
            return false;
        }
        return true;
    case InboxMessage::Kind::TransferBegin:
        transfer_label_ = msg.path;
        transfer_is_download_ = msg.is_download;
        return true;
    case InboxMessage::Kind::TransferDone:
        ++transfers_done_;
        transfer_label_.clear();
        last_error_ = msg.ok ? conn::SftpError::None : msg.error;
        {
            std::lock_guard<std::mutex> lock{inbox_mutex_};
            progress_active_ = false;
            progress_ = conn::SftpProgress{};
        }
        // 断线时那一单按失败收尾（细则 (v)）：进度已清、计数已加，重拨只负责把下一条操作接上，
        // **不做续传**——半成品由既有的取消清理腿负责。
        if (redial_once(msg.error)) {
            return false;
        }
        return true;
    case InboxMessage::Kind::TransferAsk:
        // 批次首败问人（稿 §2）：框挂浮层宿主、答话投闸；卡结构没变，本帧不重建。
        show_batch_ask(msg.path, msg.error);
        return false;
    case InboxMessage::Kind::BatchDone:
        // 批次对 transfers_posted_ 恒贡献 1（F3），收摊与单文件 TransferDone 同位清账。
        ++transfers_done_;
        transfer_label_.clear();
        {
            std::lock_guard<std::mutex> lock{inbox_mutex_};
            progress_active_ = false;
            progress_ = conn::SftpProgress{};
        }
        batch_trace_ = BatchTrace{
            .shown = true,
            .rejected = msg.batch_reject != conn::BatchReject::None,
            .reject = msg.batch_reject,
            .ok = msg.batch_ok,
            .skipped = msg.batch_skip,
            .failed = msg.batch_fail,
        };
        // 守卫线拒绝不是「这一手操作失败」，不占错误态也不重拨；其余失败走既有掉线腿（F5）。
        last_error_ = msg.ok || msg.batch_reject != conn::BatchReject::None ? conn::SftpError::None
                                                                            : msg.error;
        if (redial_once(msg.error)) {
            return false;
        }
        return true;
    }
    return false;
}

auto SftpPanel::redial_once(conn::SftpError error) -> bool {
    // 三道闸各自守一件事：错误不属掉线类（权限 / 不存在 / 取消 / 本地 IO）就绝不是会话死了；
    // 旗标已花完就是「一回掉线只重拨一回」；状态不是 Connected 就是重拨已在途，再投一条 Connect
    // 只会让同一份档案在队列里排两遍。
    if (!conn::sftp_error_drops_link(error) || !redial_armed_ || state_ != ConnState::Connected) {
        return false;
    }
    redial_armed_ = false;
    connect_now();  // 同档案、`resolve_secret()` 现取：凭据面零新增驻留（§1 第 9 条）。
    return true;
}

auto SftpPanel::refresh_progress_in_place() -> void {
    conn::SftpProgress snapshot;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock{inbox_mutex_};
        snapshot = progress_;
        active = progress_active_;
    }
    if (!active || progress_bar_ == nullptr) {
        return;
    }
    const auto percent = snapshot.progress_percent();
    progress_bar_->set_value(percent.has_value() ? static_cast<double>(*percent) / 100.0 : 0.0);
    if (transfer_text_ != nullptr) {
        std::string text =
            settings_label(transfer_is_download_ ? "sftp.transfer.downloading" : "sftp.transfer.uploading",
                           {{"name", transfer_label_}});
        if (percent.has_value()) {
            text += " " + std::to_string(*percent) + "%";
        }
        transfer_text_->set_content(std::move(text));
    }
}

auto SftpPanel::rebuild_overlay() -> void {
    if (!open_ || !overlay_index_.has_value()) {
        return;
    }
    host_->remove_overlay(*overlay_index_);
    overlay_index_ = host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(build_root())});
}

// ============================================================
// UI 侧：树组装
// ============================================================

auto SftpPanel::build_root() -> std::shared_ptr<aurora::Stack> {
    const auto chrome = settings_chrome();
    auto mask = std::make_shared<aurora::Canvas>(
        [bg = chrome.window_bg](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, bg);
        });
    mask->modifier.set(aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_card();
    return std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(mask))},
                                  aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))}},
        aurora::Alignment::CenterRight);
}

auto SftpPanel::build_card() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    std::vector<aurora::Node> children;

    // 标题行：面板名 + 关闭。
    auto title = make_text(settings_label("sftp.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    auto close_button =
        make_button(settings_label("sftp.action.close"), 48.0F, [this]() -> void { close(); });
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(title)));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(close_button)));

    // 状态行：连接态（+ 错误词条），失败档给重试。
    auto status = make_text(status_display_text(), chrome.text_dim);
    status->modifier.set(aurora::Modifier{}.fill_max_width());
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(status)));
    if (state_ == ConnState::Failed) {
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(
            make_button(settings_label("sftp.action.retry"), 48.0F,
                        [this]() -> void { connect_now(); })));
    }

    // 双栏 + 栏间对传钮。
    auto local_pane = build_pane(false);
    auto remote_pane = build_pane(true);

    const bool connected = state_ == ConnState::Connected;
    const conn::SftpEntry *remote_pick = find_remote(selected_remote_);
    // 目录条目从置灰改可点（人 2026-10-25 已拍口径①）：File 走单文件腿，Directory 走批次腿。
    const bool can_download = connected && remote_pick != nullptr &&
                              (remote_pick->kind == conn::SftpEntryKind::File ||
                               remote_pick->kind == conn::SftpEntryKind::Directory);
    const LocalEntry *local_pick = find_local(selected_local_);
    const bool can_upload =
        connected && local_pick != nullptr && !local_pick->is_symlink;
    const auto chrome2 = chrome;
    auto upload_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.action.upload"),
        .color = chrome2.control_bg,
        .on_color = chrome2.text,
        .enabled = can_upload,
        .border_color = chrome2.card_line,
        .border_width = 1.0F,
        .disabled_color = chrome2.control_bg,
        .disabled_text_color = chrome2.text_dim,
        .min_width = 40.0F,
    });
    upload_button->set_on_click([this]() -> void { start_transfer(false); });
    auto download_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.action.download"),
        .color = chrome2.control_bg,
        .on_color = chrome2.text,
        .enabled = can_download,
        .border_color = chrome2.card_line,
        .border_width = 1.0F,
        .disabled_color = chrome2.control_bg,
        .disabled_text_color = chrome2.text_dim,
        .min_width = 40.0F,
    });
    download_button->set_on_click([this]() -> void { start_transfer(true); });
    auto transfer_column = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(upload_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(download_button))}},
        .gap = 4.0F});
    transfer_column->modifier.set(aurora::Modifier{}.width(kTransferColumnWidth));

    auto panes_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(local_pane))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(transfer_column))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(remote_pane))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Stretch},
        .gap = 4.0F});
    panes_row->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(panes_row)));

    // 传输条：仅在已连接且有传输在跑 / 在排队时出现。
    const int queued = transfers_posted_ - transfers_done_;
    if (state_ == ConnState::Connected && (!transfer_label_.empty() || queued > 0)) {
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(build_transfer_bar()));
    }

    // 批次留痕行（稿 §1 末步）：收摊后常驻到下次发起/关板；拒绝档给可行动的文案。
    if (batch_trace_.shown) {
        std::string trace;
        if (batch_trace_.rejected) {
            const conn::BatchLimits limits{};
            switch (batch_trace_.reject) {
            case conn::BatchReject::OverDepth:
                trace = settings_label("sftp.batch.reject.depth",
                                       {{"limit", static_cast<std::int64_t>(limits.max_depth)}});
                break;
            case conn::BatchReject::OverCount:
                trace = settings_label("sftp.batch.reject.count",
                                       {{"limit", static_cast<std::int64_t>(limits.max_files)}});
                break;
            case conn::BatchReject::ListFailed:
            case conn::BatchReject::OverBytes:
            case conn::BatchReject::None:
                // OverBytes 本期未启用（模型头口径）；None 是拒绝档的不应到形态——同归枚举失败文案。
                trace = settings_label("sftp.batch.reject.list");
                break;
            }
        } else {
            trace = settings_label("sftp.batch.summary",
                                   {{"ok", static_cast<std::int64_t>(batch_trace_.ok)},
                                    {"skip", static_cast<std::int64_t>(batch_trace_.skipped)},
                                    {"fail", static_cast<std::int64_t>(batch_trace_.failed)}});
        }
        auto trace_note = make_text(std::move(trace), chrome.text_dim);
        trace_note->modifier.set(aurora::Modifier{}.fill_max_width());
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(trace_note)));
    }

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children), .gap = 8.0F});
    card->modifier.set(aurora::Modifier{}
                           .width(kCardWidth)
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto SftpPanel::status_display_text() const -> std::string {
    std::string text;
    switch (state_) {
    case ConnState::Connecting:
        text = settings_label("sftp.status.connecting");
        break;
    case ConnState::Connected:
        text = settings_label("sftp.status.connected");
        break;
    case ConnState::Failed:
        text = settings_label("sftp.status.failed");
        break;
    case ConnState::NotSsh:
        text = settings_label("sftp.status.not_ssh");
        break;
    }
    if (last_error_ != conn::SftpError::None) {
        text += ": " + std::string{sftp_error_key(last_error_).empty()
                                       ? std::string{}
                                       : settings_label(sftp_error_key(last_error_))};
    }
    return text;
}

auto SftpPanel::build_pane(bool remote) -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    std::vector<aurora::Node> children;

    auto caption = make_text(settings_label(remote ? "sftp.column.remote" : "sftp.column.local"), chrome.text);
    caption->modifier.set(aurora::Modifier{}.fill_max_width());
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(caption)));

    // 面包屑行：向上 / 刷新 / 当前路径。
    auto up_button = make_button(settings_label("sftp.action.up"), 40.0F,
                                 [this, remote]() -> void { up_one(remote); });
    auto refresh_button = make_button(settings_label("sftp.action.refresh"), 48.0F,
                                      [this, remote]() -> void { refresh_pane(remote); });
    const std::string &path = remote ? remote_path_ : local_path_;
    auto path_text = make_text(path.empty() ? std::string{"..."} : path, chrome.text_dim);
    path_text->modifier.set(aurora::Modifier{}.fill_max_width());
    auto crumb_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(up_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(refresh_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(path_text))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 4.0F});
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(crumb_row)));

    // 列头行（远端多一列权限）。
    std::vector<aurora::Node> header;
    const auto header_text = [&chrome](const char *key, float width) -> aurora::Node {
        auto cell = make_text(settings_label(key), chrome.text_dim);
        cell->modifier.set(aurora::Modifier{}.width(width));
        return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cell))};
    };
    header.push_back(header_text("sftp.column.name", kNameWidth));
    header.push_back(header_text("sftp.column.size", kSizeWidth));
    header.push_back(header_text("sftp.column.mtime", kMtimeWidth));
    if (remote) {
        header.push_back(header_text("sftp.column.permissions", kPermWidth));
    }
    auto header_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(header), .gap = 4.0F});
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(header_row)));

    // 行区：整表已在 worker 侧排序（目录置顶），这里只渲染。
    auto rows = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 2.0F});
    if (remote) {
        for (const conn::SftpEntry &entry : remote_entries_) {
            rows->add(build_remote_row(entry));
        }
    } else {
        for (const LocalEntry &entry : local_entries_) {
            rows->add(build_local_row(entry));
        }
    }
    auto scroll = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows))},
        .step = 32.0F});
    scroll->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(scroll)));

    // 条目操作行：远端栏专属（连接后才有可操作对象）。
    if (remote && state_ == ConnState::Connected) {
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(build_remote_actions()));
    }

    auto pane = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children), .gap = 6.0F});
    pane->modifier.set(aurora::Modifier{}.width(kPaneWidth).fill_max_height());
    return pane;
}

auto SftpPanel::build_remote_row(const conn::SftpEntry &entry) -> aurora::Node {
    const auto chrome = settings_chrome();
    const bool dim = entry.kind == conn::SftpEntryKind::Symlink || entry.kind == conn::SftpEntryKind::Other;
    const bool selected = selected_remote_ == entry.name;
    auto name_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = entry.kind == conn::SftpEntryKind::Directory ? entry.name + "/" : entry.name,
        .color = chrome.control_bg,
        .on_color = selected ? chrome.accent : (dim ? chrome.text_dim : chrome.text),
        .border_color = selected ? chrome.accent : chrome.control_bg,
        .border_width = 1.0F,
        .min_width = kNameWidth,
    });
    name_button->set_on_click(
        [this, name = entry.name, kind = entry.kind]() -> void { on_remote_clicked(name, kind); });

    auto size_cell = make_text(
        entry.kind == conn::SftpEntryKind::File ? human_size(entry.size_bytes) : std::string{},
        chrome.text_dim);
    size_cell->modifier.set(aurora::Modifier{}.width(kSizeWidth));
    auto mtime_cell = make_text(format_mtime_local(entry.mtime), chrome.text_dim);
    mtime_cell->modifier.set(aurora::Modifier{}.width(kMtimeWidth));
    auto perm_cell = make_text(format_permissions(entry.permissions, entry.kind), chrome.text_dim);
    // D5①：八进制 tooltip（含 setuid 一类特殊位，掩 07777）。
    perm_cell->modifier.set(
        aurora::Modifier{}.width(kPermWidth).tooltip(format_octal(entry.permissions)));

    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(name_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(size_cell))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(mtime_cell))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(perm_cell))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 4.0F});
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
}

auto SftpPanel::build_local_row(const LocalEntry &entry) -> aurora::Node {
    const auto chrome = settings_chrome();
    const bool selected = selected_local_ == entry.name;
    auto name_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = entry.is_directory ? entry.name + "/" : entry.name,
        .color = chrome.control_bg,
        .on_color = selected ? chrome.accent : (entry.is_symlink ? chrome.text_dim : chrome.text),
        .border_color = selected ? chrome.accent : chrome.control_bg,
        .border_width = 1.0F,
        .min_width = kNameWidth,
    });
    name_button->set_on_click([this, name = entry.name, directory = entry.is_directory]() -> void {
        on_local_clicked(name, directory);
    });

    auto size_cell = make_text(
        (!entry.is_directory && !entry.is_symlink) ? human_size(entry.size_bytes) : std::string{},
        chrome.text_dim);
    size_cell->modifier.set(aurora::Modifier{}.width(kSizeWidth));
    auto mtime_cell = make_text(format_mtime_local(entry.mtime), chrome.text_dim);
    mtime_cell->modifier.set(aurora::Modifier{}.width(kMtimeWidth));

    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(name_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(size_cell))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(mtime_cell))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 4.0F});
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
}

auto SftpPanel::build_remote_actions() -> std::shared_ptr<aurora::Row> {
    std::vector<aurora::Node> children;
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(
        make_button(settings_label("sftp.action.new_folder"), 40.0F,
                    [this]() -> void { request_create(true); })));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(
        make_button(settings_label("sftp.action.new_file"), 40.0F,
                    [this]() -> void { request_create(false); })));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(
        make_button(settings_label("sftp.action.rename"), 40.0F,
                    [this]() -> void { request_rename(); })));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(
        make_button(settings_label("sftp.action.remove"), 40.0F,
                    [this]() -> void { request_delete(); })));
    return std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(children), .gap = 4.0F});
}

auto SftpPanel::build_transfer_bar() -> std::shared_ptr<aurora::Row> {
    const auto chrome = settings_chrome();
    transfer_text_ = make_text(
        settings_label(transfer_is_download_ ? "sftp.transfer.downloading" : "sftp.transfer.uploading",
                       {{"name", transfer_label_}}),
        chrome.text);
    transfer_text_->modifier.set(aurora::Modifier{}.fill_max_width());

    progress_bar_ = std::make_shared<aurora::ProgressIndicator>();
    progress_bar_->set_color(chrome.accent);
    progress_bar_->set_thickness(6.0F);
    progress_bar_->modifier.set(aurora::Modifier{}.width(140.0F));

    // 取消＝整批止（稿 §1）：旗标掐当前件与后续件，闸上若有未答的首败问也即刻按「中止」自答。
    auto cancel_button = make_button(settings_label("sftp.action.cancel"), 48.0F,
                                     [this]() -> void {
                                         cancel_flag_ = true;
                                         batch_latch_.abort();
                                     });

    std::vector<aurora::Node> children;
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(transfer_text_)));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(progress_bar_)));
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(cancel_button)));
    const int queued = transfers_posted_ - transfers_done_;
    if (queued > 0) {
        auto queue_cell = make_text(
            settings_label("sftp.transfer.queued", {{"count", static_cast<std::int64_t>(queued)}}),
            chrome.text_dim);
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(queue_cell)));
    }
    return std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F});
}

// ============================================================
// UI 侧：交互
// ============================================================

auto SftpPanel::on_remote_clicked(const std::string &name, conn::SftpEntryKind kind) -> void {
    const auto now = std::chrono::steady_clock::now();
    const bool second_click = last_click_pane_ == "remote" && last_click_name_ == name &&
                              now - last_click_time_ < kDoubleClickWindow;
    last_click_pane_ = "remote";
    last_click_name_ = name;
    last_click_time_ = now;
    if (second_click && kind == conn::SftpEntryKind::Directory) {
        // D3①：双击进入。软链不跟随（模型口径），Other 不可操作。
        remote_path_ = conn::sftp_path_join(remote_path_, name);
        selected_remote_.clear();
        Request req{.kind = Request::Kind::ListRemote, .a = remote_path_};
        post(std::move(req));
        rebuild_overlay();  // 面包屑先走，行区等新列表。
        return;
    }
    selected_remote_ = kind == conn::SftpEntryKind::Other ? std::string{} : name;
    rebuild_overlay();
}

auto SftpPanel::on_local_clicked(const std::string &name, bool is_directory) -> void {
    const auto now = std::chrono::steady_clock::now();
    const bool second_click = last_click_pane_ == "local" && last_click_name_ == name &&
                              now - last_click_time_ < kDoubleClickWindow;
    last_click_pane_ = "local";
    last_click_name_ = name;
    last_click_time_ = now;
    if (second_click && is_directory) {
        const std::filesystem::path next =
            std::filesystem::path{local_path_} / std::filesystem::path{name};
        local_path_ = next.string();
        selected_local_.clear();
        Request req{.kind = Request::Kind::ListLocal, .a = local_path_};
        post(std::move(req));
        rebuild_overlay();
        return;
    }
    selected_local_ = name;
    rebuild_overlay();
}

auto SftpPanel::up_one(bool remote) -> void {
    if (remote) {
        // sftp_parent_of 对根回根：已在顶即原地重列（模型口径，无「/..」路径外泄）。
        remote_path_ = conn::sftp_parent_of(remote_path_);
        Request req{.kind = Request::Kind::ListRemote, .a = remote_path_};
        post(std::move(req));
    } else {
        if (local_path_.empty()) {
            return;
        }
        const std::filesystem::path parent = std::filesystem::path{local_path_}.parent_path();
        if (parent.empty()) {
            return;
        }
        local_path_ = parent.string();
        Request req{.kind = Request::Kind::ListLocal, .a = local_path_};
        post(std::move(req));
    }
    rebuild_overlay();
}

auto SftpPanel::refresh_pane(bool remote) -> void {
    if (remote) {
        Request req{.kind = Request::Kind::ListRemote, .a = remote_path_};
        post(std::move(req));
    } else if (!local_path_.empty()) {
        Request req{.kind = Request::Kind::ListLocal, .a = local_path_};
        post(std::move(req));
    }
}

auto SftpPanel::connect_now() -> void {
    if (hooks_.current_ssh_profile == nullptr) {
        return;
    }
    std::optional<conn::SshProfile> profile = hooks_.current_ssh_profile();
    if (!profile.has_value()) {
        state_ = ConnState::NotSsh;
        rebuild_overlay();
        return;
    }
    state_ = ConnState::Connecting;
    last_error_ = conn::SftpError::None;
    std::optional<std::string> secret;
    if (hooks_.resolve_secret != nullptr) {
        secret = hooks_.resolve_secret();
    }
    conn::SshAlgorithmOverrides algorithms;
    if (hooks_.algorithm_overrides != nullptr) {
        algorithms = hooks_.algorithm_overrides();
    }
    Request req{.kind = Request::Kind::Connect,
                .profile = std::move(*profile),
                .secret = std::move(secret),
                .algorithms = std::move(algorithms)};
    post(std::move(req));
    rebuild_overlay();
}

auto SftpPanel::start_transfer(bool download) -> void {
    if (state_ != ConnState::Connected) {
        return;
    }
    cancel_flag_ = false;  // 新传输从干净旗标起跑（上一单的取消不连坐）。
    if (download) {
        const conn::SftpEntry *entry = find_remote(selected_remote_);
        if (entry == nullptr) {
            return;
        }
        if (entry->kind == conn::SftpEntryKind::Directory) {
            // 目录走批次腿（稿 §1）：先 D5① 确认（覆盖警示在前），确认回调里才投递。
            show_batch_confirm(true, entry->name);
            return;
        }
        if (entry->kind != conn::SftpEntryKind::File) {
            return;  // 软链与其他类型不可传（D1① 的条目卡口径）。
        }
        Request req{.kind = Request::Kind::Download,
                    .a = conn::sftp_path_join(remote_path_, entry->name),
                    .local = std::filesystem::path{local_path_}};
        ++transfers_posted_;
        post(std::move(req));
    } else {
        const LocalEntry *entry = find_local(selected_local_);
        if (entry == nullptr || entry->is_symlink) {
            return;
        }
        if (entry->is_directory) {
            show_batch_confirm(false, entry->name);
            return;
        }
        Request req{.kind = Request::Kind::Upload,
                    .a = remote_path_,
                    .local = std::filesystem::path{local_path_} / std::filesystem::path{entry->name}};
        ++transfers_posted_;
        post(std::move(req));
    }
    rebuild_overlay();  // 排队计数变化即时呈现。
}

auto SftpPanel::request_create(bool is_folder) -> void {
    if (state_ != ConnState::Connected) {
        return;
    }
    show_name_prompt(is_folder ? "sftp.input.new_folder" : "sftp.input.new_file", {},
                     [this, is_folder](const std::string &name) -> void {
                         if (is_folder) {
                             Request req{.kind = Request::Kind::Mkdir,
                                         .a = conn::sftp_path_join(remote_path_, name),
                                         .b = remote_path_};
                             post(std::move(req));
                         } else {
                             Request req{.kind = Request::Kind::CreateFile,
                                         .a = conn::sftp_path_join(remote_path_, name),
                                         .b = remote_path_};
                             post(std::move(req));
                         }
                     });
}

auto SftpPanel::request_rename() -> void {
    if (state_ != ConnState::Connected || selected_remote_.empty()) {
        return;
    }
    const conn::SftpEntry *entry = find_remote(selected_remote_);
    if (entry == nullptr || entry->kind == conn::SftpEntryKind::Other) {
        return;  // Other 不可操作（模型口径）。
    }
    const std::string old_name = entry->name;
    show_name_prompt("sftp.input.rename", old_name,
                     [this, old_name](const std::string &new_name) -> void {
                         Request req{.kind = Request::Kind::RenameRemote,
                                     .a = conn::sftp_path_join(remote_path_, old_name),
                                     .b = conn::sftp_path_join(remote_path_, new_name)};
                         post(std::move(req));
                     });
}

auto SftpPanel::request_delete() -> void {
    if (state_ != ConnState::Connected || selected_remote_.empty()) {
        return;
    }
    const conn::SftpEntry *entry = find_remote(selected_remote_);
    if (entry == nullptr || entry->kind == conn::SftpEntryKind::Other) {
        return;
    }
    const bool is_directory = entry->kind == conn::SftpEntryKind::Directory;
    // D6①：一律二次确认；确认回调里才拼路径（改名期间路径可能已变）。
    show_delete_confirm(entry->name, is_directory);
}

auto SftpPanel::find_remote(const std::string &name) const -> const conn::SftpEntry * {
    for (const conn::SftpEntry &entry : remote_entries_) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

auto SftpPanel::find_local(const std::string &name) const -> const LocalEntry * {
    for (const LocalEntry &entry : local_entries_) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// ============================================================
// 模态对话框（删除确认 / 改名与新建输入）
// ============================================================

auto SftpPanel::close_dialog() -> void {
    if (dialog_ != nullptr) {
        dialog_->close();
        dialog_.reset();
    }
    if (dialog_index_.has_value()) {
        host_->remove_overlay(*dialog_index_);
        dialog_index_.reset();
    }
    prompt_input_.reset();
    prompt_text_.clear();
}

auto SftpPanel::show_name_prompt(const std::string &title_key, const std::string &initial,
                                 const std::function<void(const std::string &)> &on_confirm) -> void {
    const auto chrome = settings_chrome();
    close_dialog();

    prompt_input_ = std::make_shared<aurora::TextInput>();
    prompt_input_->set_value(initial);
    prompt_text_ = initial;
    prompt_input_->set_on_changed([this](const std::string &value) -> void { prompt_text_ = value; });
    prompt_input_->set_background(chrome.control_bg);
    prompt_input_->set_border_color(chrome.card_line);
    prompt_input_->set_focused_border_color(chrome.accent);
    prompt_input_->set_text_color(chrome.text);
    prompt_input_->set_placeholder(settings_label("sftp.input.name"));
    prompt_input_->modifier.set(aurora::Modifier{}.width(240.0F));

    auto title = make_text(settings_label(title_key), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());

    auto confirm = [this, on_confirm]() -> void {
        const std::string name = prompt_text_;
        close_dialog();
        if (!name.empty()) {
            on_confirm(name);
        }
    };
    auto confirm_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.action.confirm"),
        .color = chrome.accent,
        .on_color = chrome.window_bg,
        .min_width = 64.0F,
    });
    confirm_button->set_on_click(confirm);
    auto cancel_button = make_button(settings_label("sftp.action.cancel"), 64.0F,
                                     [this]() -> void { close_dialog(); });
    prompt_input_->set_on_submit([confirm](const std::string &) -> void { confirm(); });

    auto buttons = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(confirm_button))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cancel_button))}},
        .gap = 8.0F});
    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(prompt_input_))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(buttons))}},
        .gap = 8.0F});

    dialog_ = std::make_shared<aurora::Dialog>();
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(content))});
    dialog_index_ = host_->add_overlay(aurora::Node{dialog_});
    dialog_->show();
}

auto SftpPanel::show_delete_confirm(const std::string &name, bool is_directory) -> void {
    const auto chrome = settings_chrome();
    close_dialog();

    auto title = make_text(settings_label("sftp.remove.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    auto body = make_text(settings_label("sftp.remove.body", {{"name", name}}),
                          chrome.text);
    body->modifier.set(aurora::Modifier{}.fill_max_width());

    auto confirm = [this, name, is_directory]() -> void {
        close_dialog();
        if (state_ != ConnState::Connected) {
            return;
        }
        Request req{.kind = Request::Kind::RemoveRemote,
                    .a = conn::sftp_path_join(remote_path_, name),
                    .b = remote_path_,
                    .is_directory = is_directory};
        post(std::move(req));
    };
    // D6①：确认键危险红（固定暗红，不随主题 chrome 变——危险语义不属主题表达）。
    auto confirm_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.remove.confirm"),
        .color = aurora::colors::AURORA_RED,
        .on_color = chrome.window_bg,
        .min_width = 64.0F,
    });
    confirm_button->set_on_click(confirm);
    auto cancel_button = make_button(settings_label("sftp.action.cancel"), 64.0F,
                                     [this]() -> void { close_dialog(); });

    std::vector<aurora::Node> children{
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))}};
    if (is_directory) {
        auto note = make_text(settings_label("sftp.remove.dir_note"), chrome.text_dim);
        note->modifier.set(aurora::Modifier{}.fill_max_width());
        children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::move(note)));
    }
    children.emplace_back(std::static_pointer_cast<aurora::Widget>(std::make_shared<aurora::Row>(
        aurora::RowProps{
            .children = std::vector<aurora::Node>{
                aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(confirm_button))},
                aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cancel_button))}},
            .gap = 8.0F})));

    auto content = std::make_shared<aurora::Column>(
        aurora::ColumnProps{.children = std::move(children), .gap = 8.0F});
    dialog_ = std::make_shared<aurora::Dialog>();
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(content))});
    dialog_index_ = host_->add_overlay(aurora::Node{dialog_});
    dialog_->show();
}

auto SftpPanel::show_batch_confirm(bool download, const std::string &name) -> void {
    const auto chrome = settings_chrome();
    close_dialog();

    auto title = make_text(settings_label("sftp.batch.confirm.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    // D5①：覆盖警示（D2/D3① 的后果面）必须先于动作可见，文案点名目录名与「同名覆盖」。
    auto body = make_text(settings_label("sftp.batch.confirm.body", {{"name", name}}), chrome.text);
    body->modifier.set(aurora::Modifier{}.fill_max_width());

    auto confirm = [this, download, name]() -> void {
        close_dialog();
        if (state_ != ConnState::Connected) {
            return;
        }
        cancel_flag_ = false;  // 批次接旗：上一单的取消不得先掐批次的第一件。
        batch_trace_ = BatchTrace{};
        Request req;
        if (download) {
            req = Request{.kind = Request::Kind::BatchDownload,
                          .a = conn::sftp_path_join(remote_path_, name),
                          .local = std::filesystem::path{local_path_}};
        } else {
            req = Request{.kind = Request::Kind::BatchUpload,
                          .a = remote_path_,
                          .local = std::filesystem::path{local_path_} /
                                   std::filesystem::path{name}};
        }
        ++transfers_posted_;  // 批次恒贡献 1（F3），文件数不进 chip。
        post(std::move(req));
        rebuild_overlay();
    };
    auto confirm_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.action.confirm"),
        .color = chrome.accent,
        .on_color = chrome.window_bg,
        .min_width = 64.0F,
    });
    confirm_button->set_on_click(confirm);
    auto cancel_button = make_button(settings_label("sftp.action.cancel"), 64.0F,
                                     [this]() -> void { close_dialog(); });

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(
                std::make_shared<aurora::Row>(aurora::RowProps{
                    .children = std::vector<aurora::Node>{
                        aurora::Node{std::static_pointer_cast<aurora::Widget>(
                            std::move(confirm_button))},
                        aurora::Node{std::static_pointer_cast<aurora::Widget>(
                            std::move(cancel_button))}},
                    .gap = 8.0F}))}},
        .gap = 8.0F});
    dialog_ = std::make_shared<aurora::Dialog>();
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(content))});
    dialog_index_ = host_->add_overlay(aurora::Node{dialog_});
    dialog_->show();
}

auto SftpPanel::show_batch_ask(const std::string &name, conn::SftpError error) -> void {
    const auto chrome = settings_chrome();
    close_dialog();

    auto title = make_text(settings_label("sftp.batch.ask.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    std::string error_text;
    const std::string_view error_key = sftp_error_key(error);
    if (!error_key.empty()) {
        error_text = settings_label(error_key);
    }
    auto body = make_text(
        settings_label("sftp.batch.ask.body",
                       {{"name", name}, {"error", std::move(error_text)}}),
        chrome.text);
    body->modifier.set(aurora::Modifier{}.fill_max_width());

    auto skip_button = make_button(settings_label("sftp.batch.ask.skip"), 64.0F,
                                   [this]() -> void {
                                       close_dialog();
                                       batch_latch_.post_decision(BatchDecision::Skip);
                                   });
    // D7①：缺省落「中止」——红键居首与删除确认同族；超时与收摊档在闸上同值，不在此处。
    auto abort_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("sftp.batch.ask.abort"),
        .color = aurora::colors::AURORA_RED,
        .on_color = chrome.window_bg,
        .min_width = 64.0F,
    });
    abort_button->set_on_click([this]() -> void {
        close_dialog();
        batch_latch_.post_decision(BatchDecision::Abort);
    });

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(
                std::make_shared<aurora::Row>(aurora::RowProps{
                    .children = std::vector<aurora::Node>{
                        aurora::Node{std::static_pointer_cast<aurora::Widget>(
                            std::move(abort_button))},
                        aurora::Node{std::static_pointer_cast<aurora::Widget>(
                            std::move(skip_button))}},
                    .gap = 8.0F}))}},
        .gap = 8.0F});
    dialog_ = std::make_shared<aurora::Dialog>();
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(content))});
    dialog_index_ = host_->add_overlay(aurora::Node{dialog_});
    dialog_->show();
}

}  // namespace borealis::ui
