#include "test_helpers.h"
#include "constants.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "someip_fota_provider.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using namespace cgw_fota::test;

// CGW-FOTA-DSN-CR-004: 寻址与调参来自 constants.h（过渡 SSOT），不再经 ConfigLoader。

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(IntegrationTest, FullWorkflow) {
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    ASSERT_TRUE(diag_client->connect(DEFAULT_DIAG_IP_ADDRESS, DEFAULT_DIAG_PORT));
    ASSERT_TRUE(tbox_client->connect(DEFAULT_TBOX_IP_ADDRESS, DEFAULT_TBOX_PORT,
                                     DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID));

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(DEFAULT_THROTTLE_INTERVAL_MS);
    assembler->setMaxEcuCount(DEFAULT_MAX_ECU_COUNT);

    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(DEFAULT_MAX_RETRY_COUNT, DEFAULT_RETRY_INTERVAL_MS);
    reporter->setDedupWindowSize(DEFAULT_DEDUP_WINDOW_SIZE);

    bool result = reporter->reportInventory();
    EXPECT_TRUE(result);

    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, MultipleReports) {
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    ASSERT_TRUE(diag_client->connect(DEFAULT_DIAG_IP_ADDRESS, DEFAULT_DIAG_PORT));
    ASSERT_TRUE(tbox_client->connect(DEFAULT_TBOX_IP_ADDRESS, DEFAULT_TBOX_PORT,
                                     DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID));

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(0); // Disable throttling for test

    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    for (int i = 0; i < 3; ++i) {
        bool result = reporter->reportInventory();
        EXPECT_TRUE(result);
    }

    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, ErrorHandling) {
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // 无真实服务运行，连接应失败
    EXPECT_FALSE(diag_client->connect(DEFAULT_DIAG_IP_ADDRESS, DEFAULT_DIAG_PORT));

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    bool result = reporter->reportInventory();
    EXPECT_FALSE(result);
}

TEST_F(IntegrationTest, EndToEndActiveRequest) {
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    ASSERT_TRUE(diag_client->connect(DEFAULT_DIAG_IP_ADDRESS, DEFAULT_DIAG_PORT));
    ASSERT_TRUE(tbox_client->connect(DEFAULT_TBOX_IP_ADDRESS, DEFAULT_TBOX_PORT,
                                     DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID));

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    bool started = provider->start("127.0.0.1", 51120);
    ASSERT_TRUE(started);

    AsyncReportResult result = provider->handleRequest("e2e-test-001", "cloud_query");

    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    provider->stop();

    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, ConcurrentRequestMerging) {
    auto diag_client = std::make_shared<TestableSomeIpFotaClient>();
    diag_client->setTestVin("12345678901234567");
    auto tbox_client = std::make_shared<TestableSomeIpTboxClient>();

    ASSERT_TRUE(diag_client->connect(DEFAULT_DIAG_IP_ADDRESS, DEFAULT_DIAG_PORT));
    ASSERT_TRUE(tbox_client->connect(DEFAULT_TBOX_IP_ADDRESS, DEFAULT_TBOX_PORT,
                                     DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID));

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    provider->start("127.0.0.1", 51121);

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
    provider->stop();
    diag_client->disconnect();
    tbox_client->disconnect();
}
