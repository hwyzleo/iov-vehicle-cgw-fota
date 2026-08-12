#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/package_downloader.hpp
// CGW-FOTA 包下载器端口 (CGW-FOTA-DSN-CR-009 §下载, US-013)
// =============================================================================
// 为每个 package 持久化下载上下文（由 orchestrator 管理），再申请短期凭证。
// 下载器负责数据面 I/O + 校验，返回各阶段结果（DOWNLOAD/VERIFY_HASH/...）。
// MockDownloader 不访问真实 CDN，按 size 分块生成确定字节流，模拟 Range、断网、
// ETag 变化、摘要/签名/解密结果。下载与校验终态以 stageResultId+stageResultDigest
// 独立上报，不得用安装事件重复推进准备状态。
// =============================================================================

#include "vehicle/ota/v1/package.pb.h"
#include "vehicle/ota/v1/task.pb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cgw_fota {
namespace ota {

// 单包下载+校验结果。
struct DownloadOutcome {
    bool allStagesSucceeded = false;
    std::vector<::vehicle::ota::v1::StageResultReport> stageResults;
    std::int64_t bytesDownloaded = 0;
    std::int64_t finalOffset = 0;       // STORED_OBJECT 字节偏移（续传）
    std::string errorCode;
    std::string errorDetail;
};

class PackageDownloader {
public:
    virtual ~PackageDownloader() = default;

    // 下载并校验单个包，从 fromOffset 续传。grant 提供凭证/URL/etag/digest/signature。
    // 实现负责按 size 分块、Range 续传、摘要/签名/证书/解密校验，并产出阶段结果。
    virtual DownloadOutcome
    downloadAndVerify(const ::vehicle::ota::v1::PackageInfo& pkg,
                      const ::vehicle::ota::v1::DownloadGrantResponse& grant,
                      std::int64_t fromOffset) = 0;
};

} // namespace ota
} // namespace cgw_fota
