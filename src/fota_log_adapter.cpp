#include "fota_log_adapter.h"

namespace cgw_fota {

bool FotaLogAdapter::s_initialized = false;

cgw::fw::log::InitResult FotaLogAdapter::init(
    const std::string& service,
    const cgw::fw::log::LogConfig& config
) {
    auto result = cgw::fw::log::Logger::init(service, config);
    if (result.error == cgw::fw::log::LogError::kOk) {
        s_initialized = true;
    }
    return result;
}

cgw::fw::log::Logger FotaLogAdapter::orchestrator() {
    return cgw::fw::log::Logger::get("orchestrator");
}

cgw::fw::log::Logger FotaLogAdapter::snapshot_assembler() {
    return cgw::fw::log::Logger::get("snapshot_assembler");
}

cgw::fw::log::Logger FotaLogAdapter::diag_client() {
    return cgw::fw::log::Logger::get("diag_client");
}

cgw::fw::log::Logger FotaLogAdapter::inventory_reporter() {
    return cgw::fw::log::Logger::get("inventory_reporter");
}

cgw::fw::log::Logger FotaLogAdapter::store() {
    return cgw::fw::log::Logger::get("store");
}

bool FotaLogAdapter::isInitialized() {
    return s_initialized;
}

} // namespace cgw_fota
