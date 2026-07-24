#include "test_helpers.h"
#include "config_loader.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "someip_fota_provider.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using namespace cgw_fota::test;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(IntegrationTest, FullWorkflow) {
    // Load configuration
    ConfigLoader config;
    // For testing, we'll use default values

    // Create SOME/IP clients
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    // Connect to services (uses mock connect, no real TCP)
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort(), 0x6101, 0x0001));

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(config.getThrottleIntervalMs());
    assembler->setMaxEcuCount(config.getMaxEcuCount());

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(config.getMaxRetryCount(), config.getRetryIntervalMs());
    reporter->setDedupWindowSize(config.getDedupWindowSize());

    // Report inventory
    bool result = reporter->reportInventory();

    EXPECT_TRUE(result);

    // Disconnect
    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, MultipleReports) {
    // Load configuration
    ConfigLoader config;

    // Create SOME/IP clients
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    // Connect to services (uses mock connect, no real TCP)
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort(), 0x6101, 0x0001));

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(0); // Disable throttling for test

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    // Report inventory multiple times
    for (int i = 0; i < 3; ++i) {
        bool result = reporter->reportInventory();
        EXPECT_TRUE(result);
    }

    // Disconnect
    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, ErrorHandling) {
    // Test with real clients - should fail to connect because no services are running
    ConfigLoader config;

    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // These should fail to connect (no services running)
    EXPECT_FALSE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));

    // Create components anyway to test the workflow with disconnected clients
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    // This should fail because we're not connected
    bool result = reporter->reportInventory();
    EXPECT_FALSE(result);
}

TEST_F(IntegrationTest, EndToEndActiveRequest) {
    // 模拟完整的主动请求上报流程
    
    // 1. 创建组件
    ConfigLoader config;
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();
    
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort(), 0x6101, 0x0001));
    
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    
    // 2. 启动 Provider
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    bool started = provider->start("127.0.0.1", 51120);
    ASSERT_TRUE(started);
    
    // 3. 模拟 TBOX-TSP 发送请求
    AsyncReportResult result = provider->handleRequest("e2e-test-001", "cloud_query");
    
    // 4. 验证请求被接受
    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);
    
    // 5. 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 6. 停止 Provider
    provider->stop();
    
    // 7. 断开连接
    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, ConcurrentRequestMerging) {
    // 测试并发请求合并
    
    ConfigLoader config;
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();
    
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort(), 0x6101, 0x0001));
    
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    provider->start("127.0.0.1", 51121);
    
    // 快速发送多个请求
    auto result1 = provider->handleRequest("concurrent-001", "cloud_query");
    
    // 等待一小段时间让异步任务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto result2 = provider->handleRequest("concurrent-002", "manual_retry");
    auto result3 = provider->handleRequest("concurrent-003", "integration_test");
    
    // 所有请求都应该被接受
    EXPECT_TRUE(result1.accepted);
    EXPECT_TRUE(result2.accepted);
    EXPECT_TRUE(result3.accepted);
    
    // 验证并发合并行为
    // 注意：由于第一个请求可能在第二个请求之前完成，
    // 所以第二个请求可能获得新的 reportId
    // 但第三个请求应该合并到第二个（如果第二个还在进行中）
    
    // 如果 result2 和 result1 不同，说明第一个已完成
    // 那么 result3 应该与 result2 相同（合并到在途任务）
    if (result2.report_id != result1.report_id) {
        EXPECT_EQ(result2.report_id, result3.report_id);
    } else {
        // 如果 result2 与 result1 相同，说明三者都合并了
        EXPECT_EQ(result1.report_id, result3.report_id);
    }
    
    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    provider->stop();
    diag_client->disconnect();
    tbox_client->disconnect();
}
