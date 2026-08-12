#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/consent_provider.hpp
// CGW-FOTA 授权提供者端口 (CGW-FOTA-DSN-CR-009 §授权, US-012)
// =============================================================================
// ConsentProvider 输出用户选择及条款身份；上报使用稳定 idempotencyKey。
// 不得以响应 accepted 推断用户同意；必须使用 effectiveConsentStatus。
// =============================================================================

#include "vehicle/ota/v1/consent.pb.h"
#include "vehicle/ota/v1/enums.pb.h"

#include <string>

namespace cgw_fota {
namespace ota {

// 授权提供者返回的用户选择结果。
struct ConsentChoice {
    ::vehicle::ota::v1::ConsentStatus userChoice =
        ::vehicle::ota::v1::CONSENT_STATUS_PENDING;
    ::vehicle::ota::v1::ConsentTerms terms;
    bool termsAvailable = false;       // 是否取得有效条款身份
};

class ConsentProvider {
public:
    virtual ~ConsentProvider() = default;

    // 请求用户授权结果（HMI/TBOX 或 Mock）。terms 为云端下发的条款身份。
    virtual ConsentChoice
    requestConsent(const std::string& vehicleTaskId,
                   const ::vehicle::ota::v1::ConsentTerms& terms) = 0;
};

} // namespace ota
} // namespace cgw_fota
