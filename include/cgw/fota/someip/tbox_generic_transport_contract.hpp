#pragma once

// =============================================================================
// include/cgw/fota/someip/tbox_generic_transport_contract.hpp
// CGW-FOTA 契约 B TBOX 通用消息服务寻址契约 (CGW-FOTA-DSN-CR-010 / CR-011)
// =============================================================================
// 契约 B 的 SomeIpVehicleMessageTransport 通过 cgw-framework-someip 调用
// TBOX-SOMEIP 聚合服务（service 0x6101 / instance 0x0001 / TCP 56101，已由
// Registry 分配，见 constants.h / CGW-FOTA-DSN-CR-002）。generic Method/Event/
// Eventgroup ID 必须来自整车 SOME/IP Service Registry / 权威 IDL / 运行配置注入，
// 禁止在本 CR 代码中猜测或硬编码尚未分配的 ID。
//
// fail-closed 规则：
//   * method/event/eventgroup 全 0（未分配）-> 契约 B 不可启用（resolve 返回 false）；
//   * 部分声明、越界、保留值、与本地已分配 ID 冲突或接口版本不一致 -> 抛
//     FotaConfigException（启动阻止）；绝不回退 Fake 或旧私有协议。
// =============================================================================

#include "cgw/fota/config/fota_config.hpp"
#include "cgw/fw/someip/types.hpp"
#include "constants.h"

#include <cstdint>

namespace cgw_fota {
namespace someip {

// ---------------------------------------------------------------------------
// TboxGenericTransportAddress - 校验通过的 TBOX 通用消息服务寻址。
// ---------------------------------------------------------------------------
struct TboxGenericTransportAddress {
    cgw::fw::someip::ServiceId service{DEFAULT_TBOX_SERVICE_ID};     // 0x6101
    cgw::fw::someip::InstanceId instance{DEFAULT_TBOX_INSTANCE_ID};  // 0x0001
    cgw::fw::someip::MethodId method{0};      // Registry 分配的 generic exchange/publish
    cgw::fw::someip::EventId event{0};        // Registry 分配的 generic 下行 Event
    cgw::fw::someip::EventgroupId eventgroup{0};  // Registry 分配的 Eventgroup
    cgw::fw::someip::InterfaceVersion interfaceVersion{1, 0};

    bool fullyAllocated() const {
        return method != 0 && event != 0 && eventgroup != 0;
    }
};

// ---------------------------------------------------------------------------
// resolveTboxGenericTransport - 解析并校验契约 B 寻址（fail-closed）。
//   * 未分配（method/event/eventgroup 全 0）-> out 保持默认，返回 false；
//   * 已声明但非法/冲突/版本不一致 -> 抛 FotaConfigException（阻止服务开放）。
// 返回 true 时 out 为可用寻址（fullyAllocated）。
// ---------------------------------------------------------------------------
bool resolveTboxGenericTransport(const FotaConfig& cfg,
                                 TboxGenericTransportAddress& out);

} // namespace someip
} // namespace cgw_fota
