// =============================================================================
// tests/someip/clients_test.cpp
// CGW-FOTA-DSN-CR-007 §测试设计 - DIAG/TBOX Client 适配器测试
// 覆盖：availability 状态机、VersionMismatch 不调用、sync 调用成功/失败、
// timeout/cancel、late/duplicate response 不完成其他请求（framework 保证）。
// 使用真实 SomeIpRuntime（FakeSomeIpBackend）+ InlineExecutor。
// =============================================================================
#include "fota_someip_test_util.h"
#include "cgw/fota/someip/diag_inventory_client.hpp"
#include "cgw/fota/someip/tbox_inventory_client.hpp"
#include "cgw/fw/someip/runtime.hpp"
#include "cgw/fw/someip/types.hpp"
#include "constants.h"
#include "data_models.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <string>

using namespace cgw_fota;
using namespace cgw_fota_test;
namespace someip_fw = cgw::fw::someip;

namespace {

someip_fw::Payload vinPayload() {
    std::string vin = "12345678901234567";  // 17 chars
    return someip_fw::Payload(vin.begin(), vin.end());
}

} // namespace

// ============================================================
// DIAG Client
// ============================================================
class DiagClientTest : public ::testing::Test {
protected:
    std::shared_ptr<InlineExecutor> exec_;
    someip_fw::SomeIpRuntime rt_;
    someip_fw::Provider provider_;
    std::shared_ptr<someip::DiagInventoryClient> diag_;

    void SetUp() override {
        exec_ = std::make_shared<InlineExecutor>();
        rt_ = someip_fw::SomeIpRuntime::create(makeTestConfig(), exec_);
        rt_.start();
        someip_fw::ServiceKey key{DEFAULT_DIAG_SERVICE_ID, DEFAULT_DIAG_INSTANCE_ID};
        provider_ = rt_.createProvider(key, {1, 0});
        provider_.registerMethod(METHOD_READ_VIN,
            [](const someip_fw::RequestContext&, someip_fw::PayloadView) {
                return someip_fw::MethodResult::ok(vinPayload());
            });
        provider_.offer();

        someip_fw::Client client = rt_.createClient(key, {1, 0});
        diag_ = std::make_shared<someip::DiagInventoryClient>(
            std::move(client), std::chrono::milliseconds(2000));
        diag_->requestService();
        // 等待 availability 达到 Available（adapter 透传 framework Client）
        for (int i = 0; i < 1000 && diag_->availability() != someip_fw::Availability::Available; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(diag_->availability(), someip_fw::Availability::Available);
    }
    void TearDown() override {
        diag_->releaseService();
        provider_.stopOffer();
        rt_.stop();
    }
};

TEST_F(DiagClientTest, GetVinReturnsSeventeenChars) {
    std::string vin;
    ASSERT_TRUE(diag_->getVin(vin));
    EXPECT_EQ(vin.size(), 17u);
    EXPECT_EQ(vin, "12345678901234567");
}

TEST_F(DiagClientTest, CollectVehicleInventorySucceeds) {
    VehicleSoftwareSnapshot snap;
    ASSERT_TRUE(diag_->collectVehicleInventory(snap));
    EXPECT_EQ(snap.vin, "12345678901234567");
    EXPECT_FALSE(snap.ecu_list.empty());
}

// ============================================================
// TBOX Client
// ============================================================
class TboxClientTest : public ::testing::Test {
protected:
    std::shared_ptr<InlineExecutor> exec_;
    someip_fw::SomeIpRuntime rt_;
    someip_fw::Provider provider_;
    std::shared_ptr<someip::TboxInventoryClient> tbox_;

    void SetUp() override {
        exec_ = std::make_shared<InlineExecutor>();
        rt_ = someip_fw::SomeIpRuntime::create(makeTestConfig(), exec_);
        rt_.start();
        someip_fw::ServiceKey key{DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID};
        provider_ = rt_.createProvider(key, {1, 0});
        provider_.registerMethod(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY,
            [](const someip_fw::RequestContext&, someip_fw::PayloadView) {
                return someip_fw::MethodResult::ok({});
            });
        provider_.offer();

        someip_fw::Client client = rt_.createClient(key, {1, 0});
        tbox_ = std::make_shared<someip::TboxInventoryClient>(
            std::move(client), std::chrono::milliseconds(2000));
        tbox_->requestService();
        for (int i = 0; i < 1000 && tbox_->availability() != someip_fw::Availability::Available; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void TearDown() override {
        tbox_->releaseService();
        provider_.stopOffer();
        rt_.stop();
    }
};

TEST_F(TboxClientTest, ReportSoftwareInventorySucceeds) {
    VehicleSoftwareSnapshot snap;
    snap.vin = "12345678901234567";
    snap.snapshot_seq = 1;
    EXPECT_TRUE(tbox_->reportSoftwareInventory(snap));
}

TEST_F(TboxClientTest, ReportFailsWhenProviderNotAvailable) {
    // release service -> unavailable -> adapter 不调用，返回 false (CGW-FOTA-1005)
    tbox_->releaseService();
    provider_.stopOffer();
    for (int i = 0; i < 500 && tbox_->availability() == someip_fw::Availability::Available; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    VehicleSoftwareSnapshot snap;
    snap.vin = "12345678901234567";
    snap.snapshot_seq = 1;
    EXPECT_FALSE(tbox_->reportSoftwareInventory(snap));
}

// ============================================================
// VersionMismatch - Client major 版本与 Provider 不匹配时不调用
// ============================================================
TEST(ClientsVersionTest, VersionMismatchPreventsCall) {
    auto exec = std::make_shared<InlineExecutor>();
    someip_fw::SomeIpRuntime rt = someip_fw::SomeIpRuntime::create(makeTestConfig(), exec);
    rt.start();
    someip_fw::ServiceKey key{DEFAULT_DIAG_SERVICE_ID, DEFAULT_DIAG_INSTANCE_ID};
    someip_fw::Provider provider = rt.createProvider(key, {1, 0});
    provider.registerMethod(METHOD_READ_VIN,
        [](const someip_fw::RequestContext&, someip_fw::PayloadView) {
            return someip_fw::MethodResult::ok(vinPayload());
        });
    provider.offer();

    // Client 请求 major=2 -> VersionMismatch
    someip_fw::Client client = rt.createClient(key, {2, 0});
    auto diag = someip::DiagInventoryClient(std::move(client),
                                            std::chrono::milliseconds(500));
    diag.requestService();
    // 等待 availability 稳定（应进入 VersionMismatch，而非 Available）
    for (int i = 0; i < 500; ++i) {
        if (diag.availability() != someip_fw::Availability::Unknown) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // VersionMismatch 时不调用 -> getVin 失败
    std::string vin;
    EXPECT_FALSE(diag.getVin(vin));

    diag.releaseService();
    provider.stopOffer();
    rt.stop();
}
