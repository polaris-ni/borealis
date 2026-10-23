/// 测试类型: unit
/// 目标单元: src/platform/crash_handler.h + src/platform/posix/crash_handler.cpp
/// 测试说明: POSIX 崩溃处理腿的端到端证人（SPEC.NF.RELI.01，裁决 7.142）——子进程装上钩子后
///           对自己抛 SIGSEGV，父进程按子进程号找到 `crash-<pid>.txt` 并逐项核对发布过的
///           结构计数与每格身份；子进程关掉 core dump，不在工作区留核。另两案断安装参数缺失
///           时拒绝（不注册、不建目录）。Win32 腿不在此套件内（未被本机编译验证）。

#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "borealis/conn/profile.h"
#include "borealis/diag/crash_report.h"
#include "framework/aurora_test.h"
#include "platform/crash_handler.h"

namespace borealis::test_cases::utest_posix_crash_handler {

namespace {

using borealis::diag::CrashFactsSnapshot;
using borealis::diag::CrashFactsStore;
using borealis::diag::CrashSessionFact;

[[nodiscard]] auto read_text(const std::filesystem::path &file) -> std::string {
    std::ifstream stream{file};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

}  // namespace

AURORA_TEST_CASE(fatal_signal_writes_the_published_facts_to_crash_file) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("borealis-crash-test-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
         std::to_string(static_cast<long long>(std::time(nullptr))));
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    CrashFactsStore facts;
    CrashFactsSnapshot snapshot;
    snapshot.windows = 3;
    snapshot.tabs = 4;
    snapshot.panes = 5;
    snapshot.last_publish_epoch = 777;
    snapshot.session_count = 1;
    snapshot.sessions[0] = CrashSessionFact{
        .tab = 12, .pane = 2, .kind = borealis::conn::ConnectionType::Ssh, .alive = true};
    facts.publish(snapshot);

    const pid_t child = ::fork();
    AURORA_TEST_REQUIRE_GE(child, 0);
    if (child == 0) {
        // 钩子装在子进程里而不是父进程里：父进程（runner）不背着一个指向栈上事实的处理器，
        // 后续用例出崩也不会误写本用例的临时目录。core dump 关掉，故意打崩不留核。
        struct rlimit limit {};
        limit.rlim_cur = 0;
        limit.rlim_max = 0;
        static_cast<void>(::setrlimit(RLIMIT_CORE, &limit));
        if (!borealis::platform::install_crash_handler({directory, &facts})) {
            ::_exit(99);
        }
        static_cast<void>(::raise(SIGSEGV));
        ::_exit(98);  // 处理器没接住才会走到这里
    }

    int status = 0;
    AURORA_TEST_REQUIRE_EQ(::waitpid(child, &status, 0), child);
    AURORA_TEST_CHECK(WIFSIGNALED(status));

    const std::filesystem::path file =
        directory / ("crash-" + std::to_string(static_cast<long long>(child)) + ".txt");
    AURORA_TEST_REQUIRE(std::filesystem::exists(file));
    const std::string text = read_text(file);
    AURORA_TEST_CHECK_NE(text.find("trigger=SIGSEGV\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("windows=3\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("tabs=4\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("panes=5\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("sessions=1\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("last_publish_epoch=777\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(text.find("session tab=12 pane=2 kind=ssh alive=1\n"),
                         std::string::npos);

    std::filesystem::remove_all(directory, error);
}

AURORA_TEST_CASE(install_refuses_without_facts_or_directory) {
    CrashFactsStore facts;
    AURORA_TEST_CHECK_FALSE(
        borealis::platform::install_crash_handler({std::filesystem::path{}, &facts}));
    AURORA_TEST_CHECK_FALSE(borealis::platform::install_crash_handler(
        {std::filesystem::temp_directory_path(), nullptr}));
}

}  // namespace borealis::test_cases::utest_posix_crash_handler
