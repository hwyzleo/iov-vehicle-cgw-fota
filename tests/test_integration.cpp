#include "test_helpers.h"
#include "constants.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using namespace cgw_fota::test;

// CGW-FOTA-DSN-CR-007: 集成测试使用 framework-backed 适配器的测试子类，
// 不再经裸 socket connect/disconnect（framework 管理生命周期）。

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(IntegrationTest, FullWorkflow) {
    auto diag_client = std::make_shared<TestableDiagInventoryClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableTboxInventoryClient>();

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(DEFAULT_THROTTLE_INTERVAL_MS);
    assembler->setMaxEcuCount(DEFAULT_MAX_ECU_COUNT);

    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(DEFAULT_MAX_RETRY_COUNT, 1);
    reporter->setDedupWindowSize(DEFAULT_DEDUP_WINDOW_SIZE);

    bool result = reporter->reportInventory();
    EXPECT_TRUE(result);
}

TEST_F(IntegrationTest, MultipleReports) {
    auto diag_client = std::make_shared<TestableDiagInventoryClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableTboxInventoryClient>();

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(0); // Disable throttling for test

    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    for (int i = 0; i < 3; ++i) {
        bool result = reporter->reportInventory();
        EXPECT_TRUE(result);
    }
}

TEST_F(IntegrationTest, ErrorHandling) {
    // 采集失败：DIAG 返回空 VIN
    auto diag_client = std::make_shared<TestableDiagInventoryClient>();
    diag_client->setTestVin("");  // 空 VIN -> 采集失败
    auto tbox_client = std::make_shared<TestableTboxInventoryClient>();

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    bool result = reporter->reportInventory();
    EXPECT_FALSE(result);
}

TEST_F(IntegrationTest, EndToEndActiveRequest) {
    auto diag_client = std::make_shared<TestableDiagInventoryClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableTboxInventoryClient>();

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    auto provider = std::make_shared<TestableFotaProviderAdapter>(reporter);

    AsyncReportResult result = provider->handleRequest("e2e-test-001", "cloud_query");

    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST_F(IntegrationTest, ConcurrentRequestMerging) {
    auto diag_client = std::make_shared<TestableDiagInventoryClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableTboxInventoryClient>();

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    auto provider = std::make_shared<TestableFotaProviderAdapter>(reporter);

    auto result1 = provider->handleRequest("concurrent-001", "cloud_query");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto result2 = provider->handleRequest("concurrent-002", "manual_retry");
    auto result3 = provider->handleRequest("concurrent-003", "integration_test");

    EXPECT_TRUE(result1.accepted);
    EXPECT_TRUE(result2.accepted);
    EXPECT_TRUE(result3.accepted);

    if (result2.report_id != result1.report_id) {
        EXPECT_EQ(result2.report_id, result3.report_id);
    } else {
        EXPECT_EQ(result1.report_id, result3.report_id);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
