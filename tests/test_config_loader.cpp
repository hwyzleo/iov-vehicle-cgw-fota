#include "config_loader.h"
#include <gtest/gtest.h>
#include <fstream>

using namespace cgw_fota;

class ConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::ofstream outfile("test_config.yaml");
        outfile << R"(
fota:
  snapshot:
    max_ecu_count: 50
    snapshot_seq_initial: 1
    throttle_interval_ms: 3000
    dedup_window_size: 50
  someip:
    diag_service:
      service_id: 0x7725
      instance_id: 0x0001
      ip_address: "192.168.1.100"
      port: 30501
    tbox_service:
      service_id: 0x0002
      instance_id: 0x0001
      ip_address: "192.168.1.200"
      port: 30502
  reporting:
    initial_report_delay_ms: 2000
    max_retry_count: 5
    retry_interval_ms: 2000
  logging:
    level: "DEBUG"
    file: "/tmp/test_cgw_fota.log"
)";
        outfile.close();
    }

    void TearDown() override {
        std::remove("test_config.yaml");
    }
};

TEST_F(ConfigLoaderTest, LoadValidConfig) {
    ConfigLoader loader;
    bool result = loader.loadConfig("test_config.yaml");

    EXPECT_TRUE(result);
    EXPECT_EQ(loader.getMaxEcuCount(), 50);
    EXPECT_EQ(loader.getSnapshotSeqInitial(), 1);
    EXPECT_EQ(loader.getThrottleIntervalMs(), 3000);
    EXPECT_EQ(loader.getDedupWindowSize(), 50);
    EXPECT_EQ(loader.getDiagServiceId(), 0x7725);
    EXPECT_EQ(loader.getDiagInstanceId(), 0x0001);
    EXPECT_EQ(loader.getDiagIpAddress(), "192.168.1.100");
    EXPECT_EQ(loader.getDiagPort(), 30501);
    EXPECT_EQ(loader.getTboxServiceId(), 0x0002);
    EXPECT_EQ(loader.getTboxInstanceId(), 0x0001);
    EXPECT_EQ(loader.getTboxIpAddress(), "192.168.1.200");
    EXPECT_EQ(loader.getTboxPort(), 30502);
    EXPECT_EQ(loader.getInitialReportDelayMs(), 2000);
    EXPECT_EQ(loader.getMaxRetryCount(), 5);
    EXPECT_EQ(loader.getRetryIntervalMs(), 2000);
    EXPECT_EQ(loader.getLogLevel(), "DEBUG");
    EXPECT_EQ(loader.getLogFile(), "/tmp/test_cgw_fota.log");
}

TEST_F(ConfigLoaderTest, LoadNonexistentConfig) {
    ConfigLoader loader;
    bool result = loader.loadConfig("nonexistent.yaml");

    EXPECT_FALSE(result);
}

TEST_F(ConfigLoaderTest, DefaultValues) {
    ConfigLoader loader;

    EXPECT_EQ(loader.getMaxEcuCount(), 100);
    EXPECT_EQ(loader.getSnapshotSeqInitial(), 1);
    EXPECT_EQ(loader.getThrottleIntervalMs(), 5000);
    EXPECT_EQ(loader.getDedupWindowSize(), 100);
    EXPECT_EQ(loader.getDiagServiceId(), 0x7725);
    EXPECT_EQ(loader.getDiagInstanceId(), 0x0001);
    EXPECT_EQ(loader.getDiagIpAddress(), "127.0.0.1");
    EXPECT_EQ(loader.getDiagPort(), 30501);
    EXPECT_EQ(loader.getTboxServiceId(), 0x0002);
    EXPECT_EQ(loader.getTboxInstanceId(), 0x0001);
    EXPECT_EQ(loader.getTboxIpAddress(), "127.0.0.1");
    EXPECT_EQ(loader.getTboxPort(), 30502);
    EXPECT_EQ(loader.getInitialReportDelayMs(), 1000);
    EXPECT_EQ(loader.getMaxRetryCount(), 3);
    EXPECT_EQ(loader.getRetryIntervalMs(), 1000);
    EXPECT_EQ(loader.getLogLevel(), "INFO");
    EXPECT_EQ(loader.getLogFile(), "/var/log/cgw_fota.log");
}
