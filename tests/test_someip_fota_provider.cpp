#include "someip_fota_provider.h"
#include "inventory_reporter.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class MockSomeIpTboxClient : public SomeIpTboxClient {
public:
    MOCK_METHOD(bool, reportSoftwareInventory, (const VehicleSoftwareSnapshot& snapshot));
    MOCK_METHOD(bool, reportSoftwareInventoryWithRetry, (const VehicleSoftwareSnapshot& snapshot,
                                                       uint32_t max_retries,
                                                       uint32_t retry_interval_ms));
};

class MockSnapshotAssembler : public SnapshotAssembler {
public:
    MockSnapshotAssembler() : SnapshotAssembler(nullptr) {}
    MOCK_METHOD(bool, assembleSnapshot, (VehicleSoftwareSnapshot& snapshot));
};

class SomeIpFotaProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock_tbox = std::make_shared<MockSomeIpTboxClient>();
        auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

        // 模拟耗时操作，以便测试合并逻辑
        EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
            .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
                s.vin = "12345678901234567";
                s.snapshot_seq = 1;
                // 模拟采集耗时
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return true;
            }));

        EXPECT_CALL(*mock_tbox, reportSoftwareInventory(_))
            .WillRepeatedly(Return(true));

        reporter_ = std::make_shared<InventoryReporter>(mock_tbox, mock_assembler);
    }

    std::shared_ptr<InventoryReporter> reporter_;
};

TEST_F(SomeIpFotaProviderTest, StartAndStop) {
    SomeIpFotaProvider provider(reporter_);

    bool started = provider.start("127.0.0.1", 51120);
    EXPECT_TRUE(started);
    EXPECT_TRUE(provider.isRunning());

    // 等待一小段时间让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool stopped = provider.stop();
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(provider.isRunning());
}

TEST_F(SomeIpFotaProviderTest, HandleRequestReturnsAccepted) {
    SomeIpFotaProvider provider(reporter_);

    // 直接测试 handleRequest 方法
    AsyncReportResult result = provider.handleRequest("test-001", "cloud_query");

    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);
}

TEST_F(SomeIpFotaProviderTest, ConcurrentRequests) {
    SomeIpFotaProvider provider(reporter_);

    // 快速连续发送两个请求
    auto result1 = provider.handleRequest("req-001", "cloud_query");
    
    // 等待一小段时间让异步任务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto result2 = provider.handleRequest("req-002", "manual_retry");

    // 两个请求都应该被接受
    EXPECT_TRUE(result1.accepted);
    EXPECT_TRUE(result2.accepted);

    // 第二个请求应该合并到第一个（因为第一个还在进行中）
    EXPECT_EQ(result1.report_id, result2.report_id);

    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(SomeIpFotaProviderTest, MultipleSequentialRequests) {
    SomeIpFotaProvider provider(reporter_);

    // 发送多个顺序请求（等待前一个完成）
    auto result1 = provider.handleRequest("req-001", "cloud_query");
    EXPECT_TRUE(result1.accepted);
    EXPECT_EQ(result1.report_id, 1);

    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto result2 = provider.handleRequest("req-002", "manual_retry");
    EXPECT_TRUE(result2.accepted);
    EXPECT_EQ(result2.report_id, 2);

    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto result3 = provider.handleRequest("req-003", "integration_test");
    EXPECT_TRUE(result3.accepted);
    EXPECT_EQ(result3.report_id, 3);

    // 等待所有异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}
