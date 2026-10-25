/// 测试类型: unit
/// 目标单元: src/ui/sftp_batch.h/cpp 的 BatchDecisionLatch（首败决策闸）
/// 测试说明: 守住递归目录传输「首败弹框」那条唯一新增的跨线程往返（稿
///           UI_SFTP_RECURSIVE.draft.md §2，裁决 7.151）：答话唤醒、超时自收、
///           收摊自答、无问之答丢弃、过期投递不串问。真实批次循环（worker
///           撞错才开问）须真 sshd，不在本通道（稿 D8① 的证人分层）。

#include "ui/sftp_batch.h"

#include <thread>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_sftp_batch {

namespace {

using ui::BatchDecision;
using ui::BatchDecisionLatch;

/// @brief 在调用方持有的 WorkerWait 上起一条「worker」线程等待答话。
///
/// 等待时限取 2000 ms：本组的红案都以答话/收摊唤醒，时限只是防挂死的兜底，
/// 不作为被测事实（超时档另有专例用 20 ms 判）。WorkerWait 建好后不得再移动
/// （线程捕获了它的地址），故由调用方给存储、这里只点火。
struct WorkerWait {
    std::thread worker;
    BatchDecision result{BatchDecision::Abort};

    WorkerWait() = default;
    WorkerWait(WorkerWait &&) = delete;
    auto operator=(WorkerWait &&) -> WorkerWait & = delete;

    auto join_and_read() -> BatchDecision {
        worker.join();
        return result;
    }
};

auto start_wait(BatchDecisionLatch &latch, WorkerWait &handle, int time_limit_ms = 2000) -> void {
    handle.worker = std::thread{[&latch, &handle, time_limit_ms] {
        handle.result = latch.wait_for_decision(time_limit_ms);
    }};
    // 让 worker 先把闸开起来：has_pending 是唯一可观测的开问完成位。
    while (!latch.has_pending()) {
        std::this_thread::yield();
    }
}

}  // namespace

AURORA_TEST_CASE(answer_from_ui_wakes_the_worker_with_skip) {
    BatchDecisionLatch latch;
    WorkerWait worker;
    start_wait(latch, worker);

    latch.post_decision(BatchDecision::Skip);
    AURORA_TEST_CHECK_TRUE(worker.join_and_read() == BatchDecision::Skip);
    AURORA_TEST_CHECK_TRUE(!latch.has_pending());
}

AURORA_TEST_CASE(timeout_answers_abort_and_swallows_the_late_reply) {
    BatchDecisionLatch latch;
    const auto decided = latch.wait_for_decision(20);
    AURORA_TEST_CHECK_TRUE(decided == BatchDecision::Abort);
    AURORA_TEST_CHECK_TRUE(!latch.has_pending());

    // 迟到的答话没有问可答：既不生效，也不给下一问垫底。
    latch.post_decision(BatchDecision::Skip);
    const auto next = latch.wait_for_decision(20);
    AURORA_TEST_CHECK_TRUE(next == BatchDecision::Abort);
}

AURORA_TEST_CASE(shutdown_leg_answers_the_pending_question_with_abort) {
    BatchDecisionLatch latch;
    WorkerWait worker;
    start_wait(latch, worker);

    latch.abort();
    AURORA_TEST_CHECK_TRUE(worker.join_and_read() == BatchDecision::Abort);
}

AURORA_TEST_CASE(answer_without_a_question_is_dropped) {
    BatchDecisionLatch latch;
    latch.post_decision(BatchDecision::Skip);
    AURORA_TEST_CHECK_TRUE(!latch.has_pending());

    // 下一问正常开、正常等——上一份被丢弃的答话不得让它不等就返回 Skip。
    // 断言吃的是「立刻有答」这个可观测量：变异把丢弃腿拆掉后这里当场绿转红。
    const auto decided = latch.wait_for_decision(2000);
    AURORA_TEST_CHECK_TRUE(decided != BatchDecision::Skip);
}

AURORA_TEST_CASE(stale_answer_lands_before_the_next_question_is_dropped) {
    BatchDecisionLatch latch;
    const auto first = latch.wait_for_decision(20);  // 超时收摊。
    AURORA_TEST_CHECK_TRUE(first == BatchDecision::Abort);

    // 此刻 UI 才把「对第一问」的答话投上来：闸上无问，丢弃。
    latch.post_decision(BatchDecision::Skip);
    WorkerWait worker;
    start_wait(latch, worker, 2000);
    latch.post_decision(BatchDecision::Abort);  // 真正答第二问。
    AURORA_TEST_CHECK_TRUE(worker.join_and_read() == BatchDecision::Abort);
}

AURORA_TEST_CASE(first_answer_wins_later_answers_do_not_overwrite) {
    BatchDecisionLatch latch;
    WorkerWait worker;
    start_wait(latch, worker);

    latch.post_decision(BatchDecision::Skip);
    latch.post_decision(BatchDecision::Abort);  // 双投递（防 UI 抖动手感）。
    AURORA_TEST_CHECK_TRUE(worker.join_and_read() == BatchDecision::Skip);
}

}  // namespace borealis::test_cases::utest_sftp_batch
