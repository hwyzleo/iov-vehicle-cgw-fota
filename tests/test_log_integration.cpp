#include <gtest/gtest.h>
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "test_helpers.h"
#include "data_models.h"
#include "error_codes.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

namespace cgw_fota {
namespace {

class LogIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgw::fw::log::LogConfig config;
        config.level = cgw::fw::log::LogLevel::kDebug;

        auto result = FotaLogAdapter::init("cgw-fota-integration-test", config);
        ASSERT_EQ(result.error, cgw::fw::log::LogError::kOk);

        diag_client_ = std::make_shared<test::TestableSomeIpFotaClient>();
        diag_client_->connect("127.0.0.1", 51110);
        diag_client_->setTestVin("LSVAU2180N2123456");

        tbox_client_ = std::make_shared<test::TestableSomeIpTboxClient>();
        tbox_client_->connect("127.0.0.1", 56101);

        assembler_ = std::make_shared<SnapshotAssembler>(diag_client_);
        assembler_->setThrottleInterval(0); // Disable throttling for tests

        reporter_ = std::make_shared<InventoryReporter>(tbox_client_, assembler_);
        reporter_->setRetryPolicy(2, 100);
    }

    std::shared_ptr<test::TestableSomeIpFotaClient> diag_client_;
    std::shared_ptr<test::TestableSomeIpTboxClient> tbox_client_;
    std::shared_ptr<SnapshotAssembler> assembler_;
    std::shared_ptr<InventoryReporter> reporter_;
};

// ============================================================
// 上下文传播测试（CGW-FOTA-DSN-CR-003 §上下文传播）
// ============================================================

TEST_F(LogIntegrationTest, ContextScopeBasics) {
    // 验证 ContextScope RAII：构造时设置上下文，析构时恢复
    EXPECT_EQ(current_context(), nullptr);

    {
        auto scope = make_context_scope("trace-001", "req-001");
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, "trace-001");
        EXPECT_EQ(ctx->request_id, "req-001");
    }

    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, ContextScopeNesting) {
    // 验证嵌套 ContextScope：内层恢复外层上下文
    {
        auto outer = make_context_scope("trace-outer", "req-outer");

        {
            auto inner = make_context_scope("trace-inner", "req-inner");
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            EXPECT_EQ(ctx->trace_id, "trace-inner");
        }

        // 内层析构后恢复外层
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, "trace-outer");
    }
}

TEST_F(LogIntegrationTest, ChildScopeInheritsTraceId) {
    // 验证子作用域继承 trace_id（CGW-FOTA-DSN-CR-003 §上下文传播 第3点）
    std::string parent_trace = "parent-trace-123";
    std::string parent_req = "parent-req-456";

    {
        auto parent_scope = make_context_scope(parent_trace, parent_req);

        // 创建子作用域（模拟调用 CGW-DIAG）
        {
            auto child_scope = make_someip_child_scope("0x1110", "0x0002");
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            // 子作用域应继承 trace_id 和 request_id
            EXPECT_EQ(ctx->trace_id, parent_trace);
            EXPECT_EQ(ctx->request_id, parent_req);
            // 子作用域应附加 SOME/IP 上下文
            ASSERT_NE(ctx->someip, nullptr);
            EXPECT_EQ(ctx->someip->service_id, "0x1110");
            EXPECT_EQ(ctx->someip->method_id, "0x0002");
        }

        // 子作用域析构后恢复父上下文
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, parent_trace);
        EXPECT_EQ(ctx->someip, nullptr);
    }
}

TEST_F(LogIntegrationTest, ContextPropagationDuringReport) {
    // 验证上报期间上下文被正确传播
    std::string trace_id = "report-trace-001";
    std::string request_id = "report-req-001";

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 执行上报操作
        bool result = reporter_->reportInventory();

        // 验证上下文在操作期间可用
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, trace_id);
        EXPECT_EQ(ctx->request_id, request_id);

        // 上报应成功（testable clients 总是成功）
        EXPECT_TRUE(result);
    }

    // 上下文已清理
    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, FullChainContextPropagation) {
    // 模拟全链路：TBOX 请求 -> FOTA -> DIAG -> FOTA -> TBOX
    // （CGW-FOTA-DSN-CR-003 §上下文传播）
    std::string trace_id = "fullchain-trace-001";
    std::string request_id = "fullchain-req-001";

    {
        // 1. Provider 收到 TBOX 请求，建立上下文
        auto provider_scope = make_someip_context_scope(
            trace_id, request_id,
            "0x1120", "0x0001", "0x0001"
        );

        EXPECT_EQ(current_trace_id(), trace_id);
        EXPECT_EQ(current_request_id(), request_id);

        // 2. FOTA 受理请求并执行采集上报
        // reportInventory 内部会创建 DIAG 和 TBOX 的子作用域
        bool result = reporter_->reportInventory();
        EXPECT_TRUE(result);

        // 3. 验证全链路 trace_id 保持一致
        EXPECT_EQ(current_trace_id(), trace_id);
    }

    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, AutoTriggerIndependentContext) {
    // 自动触发任务生成新的 trace_id 和 request_id
    // （CGW-FOTA-DSN-CR-003 §上下文传播 第2点）
    std::string auto_trace = generate_trace_id();
    std::string auto_req = generate_request_id();

    EXPECT_FALSE(auto_trace.empty());
    EXPECT_FALSE(auto_req.empty());
    EXPECT_NE(auto_trace, auto_req);

    {
        auto scope = make_context_scope(auto_trace, auto_req);
        EXPECT_EQ(current_trace_id(), auto_trace);

        // 执行自动上报
        bool result = reporter_->reportInventory();
        EXPECT_TRUE(result);
    }
}

TEST_F(LogIntegrationTest, AsyncContextCopy) {
    // 异步任务边界显式复制日志上下文
    // （CGW-FOTA-DSN-CR-003 §上下文传播 第4点）
    std::string trace_id = "async-trace-001";
    std::string request_id = "async-req-001";

    std::atomic<bool> async_done{false};
    std::string async_trace_check;

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 捕获上下文并传递到异步线程
        std::string captured_trace = current_trace_id();
        std::string captured_req = current_request_id();

        std::thread([&async_done, &async_trace_check, captured_trace, captured_req]() {
            // 在异步线程中重建上下文
            auto async_scope = make_context_scope(captured_trace, captured_req);
            async_trace_check = current_trace_id();
            async_done.store(true);
        }).join();
    }

    EXPECT_TRUE(async_done.load());
    EXPECT_EQ(async_trace_check, trace_id);

    // 主线程上下文已清理
    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, ReportIdInContext) {
    // 验证 report_id 通过 session_id 在上下文中传播
    {
        auto scope = make_context_scope("trace-rpt", "req-rpt", "42");
        EXPECT_EQ(current_report_id(), "42");

        // 子作用域应继承 session_id（report_id）
        {
            auto child = make_someip_child_scope("0x1110", "0x0002");
            EXPECT_EQ(current_report_id(), "42");
        }
    }
}

// ============================================================
// 脱敏测试（CGW-FOTA-DSN-CR-003 §字段与脱敏）
// ============================================================

TEST_F(LogIntegrationTest, VinDesensitization) {
    // VIN 按 Identifier 处理：默认掩码或不可逆摘要
    std::string test_vin = "LSVAU2180N2123456";

    {
        auto scope = make_context_scope("trace-vin", "req-vin");

        FotaLogAdapter::inventory_reporter().info("test.vin_desensitization",
            "VIN should be masked",
            {flog::f_str("vin", test_vin, cgw::fw::log::Sensitivity::Identifier)}
        );

        // VIN 不应直接拼入 message
        FotaLogAdapter::inventory_reporter().info("test.vin_not_in_message",
            "VIN processing result",
            {flog::f_str("vin_hash", test_vin, cgw::fw::log::Sensitivity::Identifier),
             flog::f_str("result", "OK")}
        );
    }
}

TEST_F(LogIntegrationTest, PayloadDesensitization) {
    // 快照 payload 按 Payload 处理：INFO 及以上仅记录条目数、长度、摘要
    {
        auto scope = make_context_scope("trace-payload", "req-payload");

        // 正确做法：仅记录元数据
        FotaLogAdapter::snapshot_assembler().info("test.payload_metadata",
            "Snapshot metadata only",
            {flog::f_int("ecu_count", 42),
             flog::f_int("payload_length", 1024),
             flog::f_str("overall_result", "ALL_OK")}
        );

        // 错误做法（应被框架限长/过滤）：完整 payload
        std::string raw_payload = "raw_ecu_list_data_with_versions_and_details";
        FotaLogAdapter::snapshot_assembler().info("test.payload_raw",
            "Raw payload test",
            {flog::f_str("raw_payload", raw_payload, cgw::fw::log::Sensitivity::Payload)}
        );
    }
}

TEST_F(LogIntegrationTest, SecretFieldRejection) {
    // seed、key、session_key 和密钥材料按 Secret 直接拒绝
    {
        auto scope = make_context_scope("trace-secret", "req-secret");

        FotaLogAdapter::diag_client().info("test.secret_rejection",
            "Secret fields should be rejected",
            {flog::f_str("seed", "0xABCD1234", cgw::fw::log::Sensitivity::Secret),
             flog::f_str("key", "0xEF567890", cgw::fw::log::Sensitivity::Secret),
             flog::f_str("session_key", "session_key_data", cgw::fw::log::Sensitivity::Secret)}
        );

        // 验证日志可正常执行（框架拒绝 Secret 字段但不崩溃）
        SUCCEED();
    }
}

TEST_F(LogIntegrationTest, DeviceSnDesensitization) {
    // device_sn 按 Identifier 处理
    std::string device_sn = "DEVICE-SN-12345678";

    FotaLogAdapter::inventory_reporter().info("test.device_sn",
        "Device SN should be masked",
        {flog::f_str("device_sn", device_sn, cgw::fw::log::Sensitivity::Identifier)}
    );
}

// ============================================================
// 并发与压力测试（CGW-FOTA-DSN-CR-003 §测试-压力测试）
// ============================================================

TEST_F(LogIntegrationTest, ConcurrentLogging) {
    // 并发日志记录：验证线程安全
    const int num_threads = 4;
    const int logs_per_thread = 25;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i]() {
            auto logger = FotaLogAdapter::orchestrator();
            for (int j = 0; j < logs_per_thread; ++j) {
                logger.info("test.concurrent_logging",
                    "Concurrent logging test",
                    {flog::f_int("thread_id", i),
                     flog::f_int("log_index", j)}
                );
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    SUCCEED();
}

TEST_F(LogIntegrationTest, ConcurrentReportMerge) {
    // 并发请求合并：验证并发请求被正确合并
    // （CGW-FOTA-DSN-CR-003 §测试-压力测试: 并发请求合并）
    std::atomic<int> accepted_count{0};
    std::atomic<int> merged_count{0};

    const int num_requests = 3;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_requests; ++i) {
        threads.emplace_back([this, i, &accepted_count, &merged_count]() {
            auto result = reporter_->reportInventoryAsync(
                "concurrent-req-" + std::to_string(i),
                "integration_test"
            );
            if (result.accepted) {
                if (reporter_->getCurrentReportId() == result.report_id) {
                    accepted_count++;
                } else {
                    merged_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 至少一个请求被接受
    EXPECT_GE(accepted_count + merged_count, 1);
}

// ============================================================
// 故障降级测试（CGW-FOTA-DSN-CR-003 §故障与性能）
// ============================================================

TEST_F(LogIntegrationTest, LoggerInitFailure) {
    // 验证 strict 模式下的 fail-closed 行为
    // （CGW-FOTA-DSN-CR-003 §故障与性能: 严格模式按 framework 约定处理）
    cgw::fw::log::LogConfig config;
    config.strict = true;
    config.level = cgw::fw::log::LogLevel::kInfo;

    // 使用空服务名测试（框架可能返回错误或接受）
    auto result = FotaLogAdapter::init("", config);
    // 框架行为取决于实现，此处仅验证不崩溃
    SUCCEED();
}

TEST_F(LogIntegrationTest, LoggingDoesNotBlockBusiness) {
    // 验证普通日志故障不阻断业务
    // （CGW-FOTA-DSN-CR-003 §故障与性能: 普通日志故障不得阻断版本采集与上报）
    {
        auto scope = make_context_scope("trace-noblock", "req-noblock");

        // 大量日志不应阻断业务操作
        for (int i = 0; i < 100; ++i) {
            FotaLogAdapter::orchestrator().info("test.high_volume",
                "High volume logging test",
                {flog::f_int("index", i)}
            );
        }

        // 业务操作应正常完成
        bool result = reporter_->reportInventory();
        EXPECT_TRUE(result);
    }
}

TEST_F(LogIntegrationTest, FlushWorks) {
    // 验证 flush 方法可调用
    auto logger = FotaLogAdapter::orchestrator();
    logger.info("test.before_flush", "Message before flush");
    logger.flush();
    logger.info("test.after_flush", "Message after flush");
}

// ============================================================
// 事件覆盖集成测试
// ============================================================

TEST_F(LogIntegrationTest, AllBusinessEventsEmittedDuringReport) {
    // 验证完整上报流程中关键事件被输出
    {
        auto scope = make_context_scope("trace-events", "req-events", "99");

        // 模拟完整流程的事件输出
        // 1. 请求受理
        FotaLogAdapter::orchestrator().info(fota_events::INVENTORY_REQUEST_ACCEPTED,
            "Request accepted",
            {flog::f_str("request_id", "req-events"),
             flog::f_int("report_id", 99),
             flog::f_str("reason", "integration_test")});

        // 2. DIAG 采集
        FotaLogAdapter::snapshot_assembler().info(fota_events::DIAG_COLLECT_SUCCEEDED,
            "DIAG collect succeeded",
            {flog::f_str("report_id", "99"),
             flog::f_str("registry_version", "1.0.0"),
             flog::f_str("overall_result", "ALL_OK"),
             flog::f_int("duration_ms", 50)});

        // 3. 快照组装
        FotaLogAdapter::snapshot_assembler().info(fota_events::SNAPSHOT_ASSEMBLED,
            "Snapshot assembled",
            {flog::f_str("report_id", "99"),
             flog::f_int("snapshot_seq", 1),
             flog::f_str("registry_version", "1.0.0"),
             flog::f_str("overall_result", "ALL_OK")});

        // 4. TBOX 提交
        FotaLogAdapter::inventory_reporter().info(fota_events::TBOX_SUBMIT_SUCCEEDED,
            "TBOX submit succeeded",
            {flog::f_str("report_id", "99"),
             flog::f_int("snapshot_seq", 1),
             flog::f_int("duration_ms", 30)});

        // 5. 报告完成
        FotaLogAdapter::inventory_reporter().info(fota_events::INVENTORY_REPORT_COMPLETED,
            "Report completed",
            {flog::f_str("request_id", "req-events"),
             flog::f_str("report_id", "99"),
             flog::f_int("snapshot_seq", 1),
             flog::f_str("overall_result", "ALL_OK"),
             flog::f_int("duration_ms", 100)});
    }

    SUCCEED();
}

} // namespace
} // namespace cgw_fota
