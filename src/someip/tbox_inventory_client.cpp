// =============================================================================
// src/someip/tbox_inventory_client.cpp
// CGW-FOTA TBOX Inventory Client 适配器实现 (CGW-FOTA-DSN-CR-007 §Client 设计)
// =============================================================================

#include "cgw/fota/someip/tbox_inventory_client.hpp"
#include "cgw/fota/someip/error_mapper.hpp"
#include "constants.h"
#include "error_codes.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace cgw_fota {
namespace someip {

using cgw::fw::someip::Availability;
using cgw::fw::someip::CallOptions;
using cgw::fw::someip::CallResult;
using cgw::fw::someip::Payload;
using cgw::fw::someip::PayloadView;
using cgw::fw::someip::RetryMode;

namespace {

// 序列化快照为提交 payload（与既有 TBOX wire 兼容：vin|seq|count|ecu,ver...）
Payload serializeSnapshot(const VehicleSoftwareSnapshot& snapshot) {
    std::ostringstream oss;
    oss << snapshot.vin << "|"
        << snapshot.snapshot_seq << "|"
        << snapshot.ecu_list.size();
    for (const auto& ecu : snapshot.ecu_list) {
        oss << "|" << ecu.ecu_id << "," << ecu.sw_version.value_or("unknown");
    }
    const std::string& s = oss.str();
    return Payload(s.begin(), s.end());
}

} // namespace

TboxInventoryClient::TboxInventoryClient(cgw::fw::someip::Client client,
                                         std::chrono::milliseconds submitTimeout)
    : client_(std::move(client))
    , submitTimeout_(submitTimeout)
    , hasClient_(true) {}

TboxInventoryClient::TboxInventoryClient()
    : submitTimeout_(std::chrono::milliseconds(10000))
    , hasClient_(false) {}

TboxInventoryClient::~TboxInventoryClient() = default;

void TboxInventoryClient::requestService() { client_.requestService(); }
void TboxInventoryClient::releaseService() { client_.releaseService(); }
Availability TboxInventoryClient::availability() const { return client_.availability(); }

cgw::fw::someip::Subscription
TboxInventoryClient::onAvailability(cgw::fw::someip::AvailabilityCallback callback) {
    return client_.onAvailability(std::move(callback));
}

bool TboxInventoryClient::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    if (!hasClient_) return false;

    // Available/VersionMismatch gating：不可用时不调用 (CR-007)
    if (client_.availability() != Availability::Available) {
        MappedError me;
        me.code = ErrorCode::CGW_FOTA_1005;
        me.cause = "CGW-FW-0304";
        me.message = "TBOX service unavailable";
        FotaLogAdapter::inventory_reporter().error(
            fota_events::TBOX_SUBMIT_FAILED,
            "TBOX submission failed - service unavailable",
            {flog::f_str("report_id", current_report_id()),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_str("someip_service_id", hex_id(DEFAULT_TBOX_SERVICE_ID)),
             flog::f_str("someip_method_id", hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)),
             flog::f_str("error_code", "CGW-FOTA-1005"),
             flog::f_str("framework_error", "CGW-FW-0304")});
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();

    // 创建 TBOX 提交子作用域 (Service 0x6101)
    auto tbox_scope = make_someip_child_scope(
        hex_id(DEFAULT_TBOX_SERVICE_ID),
        hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY));

    Payload req = serializeSnapshot(snapshot);

    CallOptions opts;
    opts.timeout = submitTimeout_;
    opts.retry = RetryMode::None;  // framework 不自动重试 (CR-007)

    CallResult result = client_.call(
        TBOX_METHOD_REPORT_SOFTWARE_INVENTORY,
        PayloadView{req.data(), req.size()}, opts);

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    if (!result.success) {
        // timeout/断线/late response 不得推断成功 (CR-007)
        MappedError me = mapTboxCallError(result);
        FotaLogAdapter::inventory_reporter().error(
            fota_events::TBOX_SUBMIT_FAILED,
            "TBOX submission failed",
            {flog::f_str("report_id", current_report_id()),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_str("someip_service_id", hex_id(DEFAULT_TBOX_SERVICE_ID)),
             flog::f_str("someip_method_id", hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)),
             flog::f_int("duration_ms", duration_ms),
             flog::f_str("error_code", "CGW-FOTA-" + std::to_string(
                 static_cast<int>(me.code) == 1105 ? 1005 :
                 static_cast<int>(me.code) == 1104 ? 1004 :
                 static_cast<int>(me.code) == 1106 ? 1006 :
                 static_cast<int>(me.code) == 1103 ? 1003 : 1004)),
             flog::f_str("framework_error", result.code)});
        return false;
    }

    FotaLogAdapter::inventory_reporter().info(
        fota_events::TBOX_SUBMIT_SUCCEEDED,
        "TBOX-SOMEIP accepted software inventory snapshot",
        {flog::f_str("report_id", current_report_id()),
         flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
         flog::f_int("duration_ms", duration_ms)});
    return true;
}

} // namespace someip
} // namespace cgw_fota
