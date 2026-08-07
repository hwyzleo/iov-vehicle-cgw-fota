// =============================================================================
// tests/someip/error_mapper_test.cpp
// CGW-FOTA-DSN-CR-007 §测试设计 - 错误映射单元测试
// 覆盖 CGW-FW-03xx -> FOTA 1003/1004/1005/1006 映射、cause 链保留、
// 启动阻断/资源上限判定。
// =============================================================================
#include "cgw/fota/someip/error_mapper.hpp"
#include <gtest/gtest.h>

using namespace cgw_fota;
using namespace cgw_fota::someip;
using cgw::fw::someip::CallResult;

namespace {

CallResult fwFail(const std::string& code, const std::string& msg = "") {
    return CallResult::fail(code, msg);
}

} // namespace

TEST(ErrorMapperTest, DiagUnavailableMapsToCollectFailure) {
    auto r = mapDiagCallError(fwFail("CGW-FW-0304", "unavailable"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1006);
    EXPECT_EQ(r.cause, "CGW-FW-0304");
}

TEST(ErrorMapperTest, DiagTimeoutMapsToCollectTimeout) {
    auto r = mapDiagCallError(fwFail("CGW-FW-0305", "timeout"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1006);
    EXPECT_EQ(r.cause, "CGW-FW-0305");
}

TEST(ErrorMapperTest, DiagCodecErrorMapsToParseFailure) {
    auto r = mapDiagCallError(fwFail("CGW-FW-0306", "payload illegal"));
    EXPECT_EQ(r.code, ErrorCode::CGW_DIAG_1005);
    EXPECT_EQ(r.cause, "CGW-FW-0306");
}

TEST(ErrorMapperTest, TboxUnavailableMapsTo1005) {
    auto r = mapTboxCallError(fwFail("CGW-FW-0304", "version mismatch"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1005);
    EXPECT_EQ(r.cause, "CGW-FW-0304");
}

TEST(ErrorMapperTest, TboxTransportFailureMapsTo1004) {
    auto r = mapTboxCallError(fwFail("CGW-FW-0303", "send failed"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1004);
    EXPECT_EQ(r.cause, "CGW-FW-0303");
}

TEST(ErrorMapperTest, TboxTimeoutMapsTo1006) {
    auto r = mapTboxCallError(fwFail("CGW-FW-0305", "timeout"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1006);
    EXPECT_EQ(r.cause, "CGW-FW-0305");
}

TEST(ErrorMapperTest, TboxCodecErrorMapsTo1003) {
    auto r = mapTboxCallError(fwFail("CGW-FW-0306", "snapshot encode failed"));
    EXPECT_EQ(r.code, ErrorCode::CGW_FOTA_1003);
    EXPECT_EQ(r.cause, "CGW-FW-0306");
}

TEST(ErrorMapperTest, StartupBlockingFlags) {
    EXPECT_TRUE(isStartupBlocking("CGW-FW-0301"));
    EXPECT_TRUE(isStartupBlocking("CGW-FW-0302"));
    EXPECT_FALSE(isStartupBlocking("CGW-FW-0303"));
    EXPECT_FALSE(isStartupBlocking("CGW-FW-0304"));
}

TEST(ErrorMapperTest, ResourceLimitFlag) {
    EXPECT_TRUE(isResourceLimit("CGW-FW-0309"));
    EXPECT_FALSE(isResourceLimit("CGW-FW-0305"));
}

TEST(ErrorMapperTest, FrameworkErrorNumberNormalizes) {
    EXPECT_EQ(frameworkErrorNumber("CGW-FW-0305"), "0305");
    EXPECT_EQ(frameworkErrorNumber("CGW-FW-0301"), "0301");
}

TEST(ErrorMapperTest, CauseChainPreservedForUnknown) {
    // 未知 framework 错误仍保留 cause，映射到通用失败码
    auto rd = mapDiagCallError(fwFail("CGW-FW-0999", "unknown"));
    EXPECT_EQ(rd.cause, "CGW-FW-0999");
    EXPECT_EQ(rd.code, ErrorCode::CGW_DIAG_1006);

    auto rt = mapTboxCallError(fwFail("CGW-FW-0999", "unknown"));
    EXPECT_EQ(rt.cause, "CGW-FW-0999");
    EXPECT_EQ(rt.code, ErrorCode::CGW_FOTA_1004);
}
