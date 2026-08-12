#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/inventory_provider.hpp
// CGW-FOTA 清单提供者端口 (CGW-FOTA-DSN-CR-009 §任务检测, US-011)
// =============================================================================
// InventoryProvider 产生 FULL 清单，规范化后计算 inventoryRevision + ecuListDigest
// + algorithm。后续可发送 DIGEST。Mock/真实实现共享同一端口，不分叉状态机或 payload。
// =============================================================================

#include "vehicle/ota/v1/enums.pb.h"
#include "vehicle/ota/v1/inventory.pb.h"

namespace cgw_fota {
namespace ota {

class InventoryProvider {
public:
    virtual ~InventoryProvider() = default;

    // 产生 ECU 清单。FULL 模式返回完整 ecu_list + ecu_list_digest；DIGEST 模式
    // 仅返回 ecu_digest_list + ecu_list_digest。规范化、digest 由实现负责。
    virtual ::vehicle::ota::v1::InventoryInfo
    collectInventory(::vehicle::ota::v1::InventoryMode mode) = 0;
};

} // namespace ota
} // namespace cgw_fota
