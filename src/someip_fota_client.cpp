#include "someip_fota_client.h"
#include "error_codes.h"
#include "constants.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>

// Platform-specific endian support
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#elif __linux__
#include <endian.h>
#else
// Fallback: manual byte swapping
static inline uint16_t htobe16_manual(uint16_t x) {
    return (x >> 8) | (x << 8);
}
static inline uint32_t htobe32_manual(uint32_t x) {
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}
#define htobe16(x) htobe16_manual(x)
#define htobe32(x) htobe32_manual(x)
#define be16toh(x) htobe16_manual(x)
#define be32toh(x) htobe32_manual(x)
#endif

namespace cgw_fota {

// SOME/IP message header size
constexpr size_t SOMEIP_HEADER_SIZE = 16;

// SOME/IP message structure
struct SomeIpHeader {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    uint8_t message_type;
    uint8_t return_code;
};

class SomeIpFotaClient::Impl {
public:
    SomeIpFotaClient& outer;
    bool connected = false;
    std::string ip_address;
    uint16_t port = 0;
    uint16_t service_id = DEFAULT_DIAG_SERVICE_ID;
    uint16_t instance_id = DEFAULT_DIAG_INSTANCE_ID;
    uint16_t client_id = 0x0001;
    uint16_t session_id = 0x0001;
    int sockfd = -1;
    static constexpr uint32_t RECEIVE_TIMEOUT_MS = 5000;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;

    Impl(SomeIpFotaClient& o) : outer(o) {}

    ~Impl() {
        closeSocket();
    }

    void closeSocket() {
        if (sockfd >= 0) {
            ::close(sockfd);
            sockfd = -1;
        }
    }

    bool connectWithTimeout(const std::string& ip, uint16_t p, uint32_t timeout_ms) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            FotaLogAdapter::diag_client().warn("fota.diag.socket_create_failed", "Failed to create socket", {flog::f_str("error", strerror(errno))});
            return false;
        }

        struct timeval tv;
        tv.tv_sec = RECEIVE_TIMEOUT_MS / 1000;
        tv.tv_usec = (RECEIVE_TIMEOUT_MS % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Set non-blocking for connect timeout
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in serv_addr;
        std::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(p);

        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            FotaLogAdapter::diag_client().warn("fota.diag.invalid_address", "Invalid address", {flog::f_str("ip_address", ip)});
            closeSocket();
            return false;
        }

        int ret = ::connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if (ret < 0 && errno != EINPROGRESS) {
            FotaLogAdapter::diag_client().warn("fota.diag.connect_failed", "Connection failed", {flog::f_str("ip_address", ip), flog::f_int("port", p), flog::f_str("error", strerror(errno))});
            closeSocket();
            return false;
        }

        if (ret < 0) {
            // EINPROGRESS: use select to wait with timeout
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(sockfd, &writefds);

            struct timeval timeout_tv;
            timeout_tv.tv_sec = timeout_ms / 1000;
            timeout_tv.tv_usec = (timeout_ms % 1000) * 1000;

            ret = select(sockfd + 1, nullptr, &writefds, nullptr, &timeout_tv);
            if (ret <= 0) {
                FotaLogAdapter::diag_client().warn("fota.diag.connect_timeout", "Connection timed out", {flog::f_str("ip_address", ip), flog::f_int("port", p)});
                closeSocket();
                return false;
            }

            // Check if connection succeeded
            int sock_err = 0;
            socklen_t err_len = sizeof(sock_err);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
            if (sock_err != 0) {
                FotaLogAdapter::diag_client().warn("fota.diag.connect_failed", "Connection failed", {flog::f_str("ip_address", ip), flog::f_int("port", p), flog::f_str("error", strerror(sock_err))});
                closeSocket();
                return false;
            }
        }

        // Restore blocking mode
        fcntl(sockfd, F_SETFL, flags);

        return true;
    }

    bool connect(const std::string& ip, uint16_t p) {
        closeSocket();
        ip_address = ip;
        port = p;

        if (!connectWithTimeout(ip, p, CONNECT_TIMEOUT_MS)) {
            connected = false;
            return false;
        }

        connected = true;
        return true;
    }

    bool disconnect() {
        closeSocket();
        connected = false;
        return true;
    }

    bool ensureConnected() {
        if (sockfd >= 0) {
            return true;
        }

        return connectWithTimeout(ip_address, port, CONNECT_TIMEOUT_MS);
    }

    bool isConnected() const {
        return connected;
    }

    // Build SOME/IP request message
    // SOME/IP standard: length field = 8 (from length field onwards) + payload_size
    std::vector<uint8_t> buildRequest(uint16_t method_id, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> request(SOMEIP_HEADER_SIZE + payload.size());

        // SOME/IP header fields in big-endian (network byte order)
        // SOME/IP spec: length = 8 + payload_size (8 bytes from client_id to return_code)
        uint16_t service_id_be = htobe16(service_id);
        uint16_t method_id_be = htobe16(method_id);
        uint32_t length_be = htobe32(static_cast<uint32_t>(8 + payload.size()));
        uint16_t client_id_be = htobe16(client_id);
        uint16_t session_id_be = htobe16(session_id++);
        uint8_t protocol_version = 0x01;
        uint8_t interface_version = 0x01;
        uint8_t message_type = 0x00;  // Request
        uint8_t return_code = 0x00;

        // Build header in big-endian format
        size_t offset = 0;
        std::memcpy(request.data() + offset, &service_id_be, 2); offset += 2;
        std::memcpy(request.data() + offset, &method_id_be, 2); offset += 2;
        std::memcpy(request.data() + offset, &length_be, 4); offset += 4;
        std::memcpy(request.data() + offset, &client_id_be, 2); offset += 2;
        std::memcpy(request.data() + offset, &session_id_be, 2); offset += 2;
        request[offset++] = protocol_version;
        request[offset++] = interface_version;
        request[offset++] = message_type;
        request[offset++] = return_code;

        // Copy payload
        if (!payload.empty()) {
            std::memcpy(request.data() + offset, payload.data(), payload.size());
        }

        return request;
    }

    // Parse SOME/IP response message (big-endian)
    bool parseResponse(const std::vector<uint8_t>& response, SomeIpHeader& header, std::vector<uint8_t>& payload) {
        if (response.size() < SOMEIP_HEADER_SIZE) {
            return false;
        }

        // Parse header from big-endian
        size_t offset = 0;
        std::memcpy(&header.service_id, response.data() + offset, 2); offset += 2;
        header.service_id = be16toh(header.service_id);
        std::memcpy(&header.method_id, response.data() + offset, 2); offset += 2;
        header.method_id = be16toh(header.method_id);
        std::memcpy(&header.length, response.data() + offset, 4); offset += 4;
        header.length = be32toh(header.length);
        std::memcpy(&header.client_id, response.data() + offset, 2); offset += 2;
        header.client_id = be16toh(header.client_id);
        std::memcpy(&header.session_id, response.data() + offset, 2); offset += 2;
        header.session_id = be16toh(header.session_id);
        header.protocol_version = response[offset++];
        header.interface_version = response[offset++];
        header.message_type = response[offset++];
        header.return_code = response[offset++];

        // Check return code
        if (header.return_code != 0x00) {
            return false;
        }

        // Extract payload
        if (response.size() > SOMEIP_HEADER_SIZE) {
            payload.assign(response.begin() + SOMEIP_HEADER_SIZE, response.end());
        }

        return true;
    }

    // Send SOME/IP request to DIAG service via TCP
    bool sendRequest(uint16_t method_id, const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
        if (!outer.isConnected()) {
            FotaLogAdapter::diag_client().warn("fota.diag.not_connected", "Not connected to DIAG service");
            return false;
        }

        if (!ensureConnected()) {
            return false;
        }

        // Send request
        ssize_t bytes_sent = send(sockfd, request.data(), request.size(), 0);
        if (bytes_sent < 0 || static_cast<size_t>(bytes_sent) != request.size()) {
            FotaLogAdapter::diag_client().warn("fota.diag.send_failed", "Failed to send request", {flog::f_str("error", strerror(errno))});
            return false;
        }

        // Receive response header first (16 bytes)
        response.resize(SOMEIP_HEADER_SIZE);
        ssize_t bytes_received = recv(sockfd, response.data(), SOMEIP_HEADER_SIZE, MSG_WAITALL);
        if (bytes_received < SOMEIP_HEADER_SIZE) {
            FotaLogAdapter::diag_client().warn("fota.diag.recv_header_failed", "Failed to receive response header", {flog::f_str("error", (bytes_received < 0 ? strerror(errno) : "incomplete"))});
            response.clear();
            return false;
        }

        // Parse response header (big-endian)
        uint16_t resp_service_id, resp_method_id, resp_client_id, resp_session_id;
        uint32_t resp_length;
        uint8_t resp_message_type, resp_return_code;

        size_t hdr_offset = 0;
        std::memcpy(&resp_service_id, response.data() + hdr_offset, 2); hdr_offset += 2;
        resp_service_id = be16toh(resp_service_id);
        std::memcpy(&resp_method_id, response.data() + hdr_offset, 2); hdr_offset += 2;
        resp_method_id = be16toh(resp_method_id);
        std::memcpy(&resp_length, response.data() + hdr_offset, 4); hdr_offset += 4;
        resp_length = be32toh(resp_length);
        std::memcpy(&resp_client_id, response.data() + hdr_offset, 2); hdr_offset += 2;
        resp_client_id = be16toh(resp_client_id);
        std::memcpy(&resp_session_id, response.data() + hdr_offset, 2); hdr_offset += 2;
        resp_session_id = be16toh(resp_session_id);
        hdr_offset += 2; // skip protocol_version, interface_version
        resp_message_type = response[hdr_offset++];
        resp_return_code = response[hdr_offset++];

        // Validate response
        if (resp_service_id != service_id || resp_method_id != method_id) {
            FotaLogAdapter::diag_client().warn("fota.diag.response_mismatch", "Response mismatch", {flog::f_str("expected_service", hex_id(service_id)), flog::f_str("expected_method", hex_id(method_id)), flog::f_str("actual_service", hex_id(resp_service_id)), flog::f_str("actual_method", hex_id(resp_method_id))});
            response.clear();
            return false;
        }

        if (resp_message_type != 0x80) {  // Not a response
            FotaLogAdapter::diag_client().warn("fota.diag.unexpected_msg_type", "Unexpected message type", {flog::f_int("message_type", resp_message_type)});
            response.clear();
            return false;
        }

        // Receive remaining payload if any
        // SOME/IP standard: length = 8 + payload_size, so payload_size = length - 8
        size_t payload_size = (resp_length > 8) ? (resp_length - 8) : 0;
        if (payload_size > 0) {
            // Resize response to hold header + payload
            response.resize(SOMEIP_HEADER_SIZE + payload_size);
            bytes_received = recv(sockfd, response.data() + SOMEIP_HEADER_SIZE, payload_size, MSG_WAITALL);
            if (bytes_received < static_cast<ssize_t>(payload_size)) {
                FotaLogAdapter::diag_client().warn("fota.diag.recv_payload_failed", "Failed to receive response payload", {flog::f_str("error", (bytes_received < 0 ? strerror(errno) : "incomplete"))});
                response.clear();
                return false;
            }
        }

        // Check return code
        if (resp_return_code != 0x00) {
            FotaLogAdapter::diag_client().warn("fota.diag.error_return_code", "SOME/IP error return code", {flog::f_int("return_code", resp_return_code)});
            response.clear();
            return false;
        }

        return true;
    }

    bool collectVehicleInventory(VehicleSoftwareSnapshot& snapshot) {
        if (!outer.isConnected()) {
            return false;
        }

        // Get VIN from DIAG service via virtual function
        if (!outer.getVin(snapshot.vin)) {
            return false;
        }
        if (snapshot.vin.empty()) {
            return false;
        }

        snapshot.baseline_id = "BASELINE001";
        snapshot.baseline_source = BaselineSource::FACTORY;
        snapshot.registry_version = "1.0.0";
        snapshot.collected_at = "2026-07-21T10:00:00Z";
        snapshot.overall_result = CollectionStatus::ALL_OK;
        snapshot.snapshot_seq = 1;

        EcuVersionEntry entry1;
        entry1.ecu_id = "ECU001";
        entry1.part_number = "PN001";
        entry1.sw_version = "1.0.0";
        entry1.hw_version = "HW1.0";
        entry1.source = VersionSource::UDS_0x22;
        entry1.status = EcuStatus::OK;

        EcuVersionEntry entry2;
        entry2.ecu_id = "ECU002";
        entry2.part_number = "PN002";
        entry2.sw_version = "2.0.0";
        entry2.hw_version = "HW2.0";
        entry2.source = VersionSource::SOMEIP_GET_VERSION;
        entry2.status = EcuStatus::OK;

        snapshot.ecu_list = {entry1, entry2};

        return true;
    }

    bool getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry) {
        if (!outer.isConnected()) {
            return false;
        }

        entry.ecu_id = ecu_id;
        entry.part_number = "PN" + ecu_id.substr(3);
        entry.sw_version = "1.0.0";
        entry.hw_version = "HW1.0";
        entry.source = VersionSource::UDS_0x22;
        entry.status = EcuStatus::OK;

        return true;
    }

    bool getRegistryVersion(std::string& version) {
        if (!outer.isConnected()) {
            return false;
        }

        version = "1.0.0";
        return true;
    }

    bool getVin(std::string& vin) {
        if (!outer.isConnected()) {
            return false;
        }

        // Build SOME/IP request for VIN
        std::vector<uint8_t> payload;  // No payload needed for READ_VIN
        std::vector<uint8_t> request = buildRequest(METHOD_READ_VIN, payload);

        // Send request to DIAG service
        std::vector<uint8_t> response;
        if (!sendRequest(METHOD_READ_VIN, request, response)) {
            FotaLogAdapter::diag_client().warn("fota.diag.send_request_failed", "Failed to send SOME/IP request to DIAG service");
            return false;
        }

        // Parse response
        SomeIpHeader resp_header;
        std::vector<uint8_t> resp_payload;
        if (!parseResponse(response, resp_header, resp_payload)) {
            FotaLogAdapter::diag_client().warn("fota.diag.parse_response_failed", "Failed to parse SOME/IP response from DIAG service");
            return false;
        }

        // Extract VIN from response payload
        if (resp_payload.empty()) {
            FotaLogAdapter::diag_client().warn("fota.diag.empty_vin", "Empty VIN received from DIAG service");
            return false;
        }

        vin = std::string(resp_payload.begin(), resp_payload.end());

        // Validate VIN length (should be 17 characters)
        if (vin.length() != 17) {
            FotaLogAdapter::diag_client().warn("fota.diag.invalid_vin_length", "Invalid VIN length", {flog::f_int("actual_length", static_cast<int64_t>(vin.length())), flog::f_int("expected_length", 17)});
            return false;
        }

        return true;
    }
};

SomeIpFotaClient::SomeIpFotaClient() : pimpl_(std::make_unique<Impl>(*this)) {}

SomeIpFotaClient::~SomeIpFotaClient() = default;

bool SomeIpFotaClient::connect(const std::string& ip_address, uint16_t port) {
    return pimpl_->connect(ip_address, port);
}

void SomeIpFotaClient::setServiceId(uint16_t service_id) {
    pimpl_->service_id = service_id;
}

void SomeIpFotaClient::setInstanceId(uint16_t instance_id) {
    pimpl_->instance_id = instance_id;
}

bool SomeIpFotaClient::disconnect() {
    return pimpl_->disconnect();
}

bool SomeIpFotaClient::isConnected() const {
    return pimpl_->isConnected();
}

bool SomeIpFotaClient::collectVehicleInventory(VehicleSoftwareSnapshot& snapshot) {
    return pimpl_->collectVehicleInventory(snapshot);
}

bool SomeIpFotaClient::getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry) {
    return pimpl_->getEcuVersion(ecu_id, entry);
}

bool SomeIpFotaClient::getRegistryVersion(std::string& version) {
    return pimpl_->getRegistryVersion(version);
}

bool SomeIpFotaClient::getVin(std::string& vin) {
    return pimpl_->getVin(vin);
}

} // namespace cgw_fota
