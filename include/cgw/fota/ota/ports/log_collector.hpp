#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/log_collector.hpp
// CGW-FOTA 日志采集器端口 (CGW-FOTA-DSN-CR-009 §日志, US-016)
// =============================================================================
// MockLogCollector 生成无敏感数据的确定性诊断包，沿真实授权/对象上传/结果上报
// 状态机执行；对象上传数据面可由本地 Stub 模拟。文件数据面走对象存储，不进 Proto。
// =============================================================================

#include "vehicle/ota/v1/log.pb.h"

#include <cstdint>
#include <string>

namespace cgw_fota {
namespace ota {

// 生成的日志包元数据（不含文件内容）。
struct LogPackage {
    std::string objectKey;
    std::string digestHex;
    std::int64_t sizeBytes = 0;
    bool generated = false;
    std::string errorCode;
};

class LogCollector {
public:
    virtual ~LogCollector() = default;

    // 按采集范围、时间范围和脱敏规则采集日志包。Mock 生成确定性无敏感数据包。
    virtual LogPackage
    collect(const ::vehicle::ota::v1::LogCollectScope& scope,
            std::int64_t fromMs, std::int64_t toMs) = 0;
};

} // namespace ota
} // namespace cgw_fota
