#include "someip_tbox_client.h"
#include "error_codes.h"
#include "constants.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

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
struct SomeIpTboxHeader {
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

class SomeIpTboxClient::Impl {
public:
    bool connected = false;
    std::string ip_address;
    uint16_t port = 0;
    uint16_t service_id = DEFAULT_TBOX_SERVICE_ID;
    uint16_t instance_id = DEFAULT_TBOX_INSTANCE_ID;
    uint16_t client_id = 0x0002;
    uint16_t session_id = 0x0001;
    int sockfd = -1;
    static constexpr uint32_t RECEIVE_TIMEOUT_MS = 5000;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;

    void closeSocket() {
        if (sockfd >= 0) {
            ::close(sockfd);
            sockfd = -1;
        }
    }

    bool connectWithTimeout(const std::string& ip, uint16_t p, uint32_t timeout_ms) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.socket_create_failed", "Failed to create socket", {flog::f_str("error", strerror(errno))});
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
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.invalid_address", "Invalid address", {flog::f_str("ip_address", ip)});
            closeSocket();
            return false;
        }

        int ret = ::connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if (ret < 0 && errno != EINPROGRESS) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.connect_failed", "Connection failed", {flog::f_str("ip_address", ip), flog::f_int("port", p), flog::f_str("error", strerror(errno))});
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
                FotaLogAdapter::inventory_reporter().warn("fota.tbox.connect_timeout", "Connection timed out", {flog::f_str("ip_address", ip), flog::f_int("port", p)});
                closeSocket();
                return false;
            }

            // Check if connection succeeded
            int sock_err = 0;
            socklen_t err_len = sizeof(sock_err);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
            if (sock_err != 0) {
                FotaLogAdapter::inventory_reporter().warn("fota.tbox.connect_failed", "Connection failed", {flog::f_str("ip_address", ip), flog::f_int("port", p), flog::f_str("error", strerror(sock_err))});
                closeSocket();
                return false;
            }
        }

        // Restore blocking mode
        fcntl(sockfd, F_SETFL, flags);

        FotaLogAdapter::inventory_reporter().info("fota.tbox.connected", "TCP connection established", {flog::f_str("ip_address", ip), flog::f_int("port", p)});
        return true;
    }

    bool connect(const std::string& ip, uint16_t p, uint16_t sid, uint16_t iid) {
        closeSocket();
        ip_address = ip;
        port = p;
        service_id = sid;
        instance_id = iid;

        if (!connectWithTimeout(ip, p, CONNECT_TIMEOUT_MS)) {
            connected = false;
            return false;
        }

        connected = true;
        return true;
    }



    bool ensureConnected() {
        if (sockfd >= 0) {
            return true;
        }

        FotaLogAdapter::inventory_reporter().info("fota.tbox.reconnecting", "Reconnecting", {flog::f_str("ip_address", ip_address), flog::f_int("port", port)});
        return connectWithTimeout(ip_address, port, CONNECT_TIMEOUT_MS);
    }

    bool disconnect() {
        closeSocket();
        connected = false;
        return true;
    }

    bool isConnected() const {
        return connected;
    }

    // Build SOME/IP request message
    std::vector<uint8_t> buildRequest(uint16_t method_id, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> request(SOMEIP_HEADER_SIZE + payload.size());

        uint16_t service_id_be = htobe16(service_id);
        uint16_t method_id_be = htobe16(method_id);
        uint32_t length_be = htobe32(static_cast<uint32_t>(8 + payload.size()));
        uint16_t client_id_be = htobe16(client_id);
        uint16_t session_id_be = htobe16(session_id++);
        uint8_t protocol_version = 0x01;
        uint8_t interface_version = 0x01;
        uint8_t message_type = 0x00;  // Request
        uint8_t return_code = 0x00;

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

        if (!payload.empty()) {
            std::memcpy(request.data() + offset, payload.data(), payload.size());
        }

        return request;
    }

    // Parse SOME/IP response message
    bool parseResponse(const std::vector<uint8_t>& response, SomeIpTboxHeader& header, std::vector<uint8_t>& payload) {
        if (response.size() < SOMEIP_HEADER_SIZE) {
            return false;
        }

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

        if (header.return_code != 0x00) {
            return false;
        }

        if (response.size() > SOMEIP_HEADER_SIZE) {
            payload.assign(response.begin() + SOMEIP_HEADER_SIZE, response.end());
        }

        return true;
    }

    // Send SOME/IP request via TCP and receive response
    bool sendRequest(uint16_t method_id, const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
        if (!connected) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.not_connected", "Not connected");
            return false;
        }

        if (!ensureConnected()) {
            return false;
        }

        ssize_t bytes_sent = send(sockfd, request.data(), request.size(), 0);
        if (bytes_sent < 0 || static_cast<size_t>(bytes_sent) != request.size()) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.send_failed", "Failed to send request", {flog::f_str("error", strerror(errno))});
            closeSocket();
            return false;
        }

        // Receive response header
        response.resize(SOMEIP_HEADER_SIZE);
        ssize_t bytes_received = recv(sockfd, response.data(), SOMEIP_HEADER_SIZE, MSG_WAITALL);
        if (bytes_received < SOMEIP_HEADER_SIZE) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.recv_header_failed", "Failed to receive response header", {flog::f_str("error", (bytes_received < 0 ? strerror(errno) : "incomplete"))});
            response.clear();
            closeSocket();
            return false;
        }

        // Parse response header
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
        hdr_offset += 2;
        resp_message_type = response[hdr_offset++];
        resp_return_code = response[hdr_offset++];

        if (resp_service_id != service_id || resp_method_id != method_id) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.response_mismatch", "Response mismatch", {flog::f_str("expected_service", hex_id(service_id)), flog::f_str("expected_method", hex_id(method_id)), flog::f_str("actual_service", hex_id(resp_service_id)), flog::f_str("actual_method", hex_id(resp_method_id))});
            response.clear();
            closeSocket();
            return false;
        }

        if (resp_message_type != 0x80) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.unexpected_msg_type", "Unexpected message type", {flog::f_int("message_type", resp_message_type)});
            response.clear();
            closeSocket();
            return false;
        }

        // Receive remaining payload
        size_t payload_size = (resp_length > 8) ? (resp_length - 8) : 0;
        if (payload_size > 0) {
            response.resize(SOMEIP_HEADER_SIZE + payload_size);
            bytes_received = recv(sockfd, response.data() + SOMEIP_HEADER_SIZE, payload_size, MSG_WAITALL);
            if (bytes_received < static_cast<ssize_t>(payload_size)) {
                FotaLogAdapter::inventory_reporter().warn("fota.tbox.recv_payload_failed", "Failed to receive response payload", {flog::f_str("error", (bytes_received < 0 ? strerror(errno) : "incomplete"))});
                response.clear();
                closeSocket();
                return false;
            }
        }

        if (resp_return_code != 0x00) {
            FotaLogAdapter::inventory_reporter().warn("fota.tbox.error_return_code", "SOME/IP error return code", {flog::f_int("return_code", resp_return_code)});
            response.clear();
            closeSocket();
            return false;
        }

        return true;
    }

    // Serialize snapshot to payload bytes
    std::vector<uint8_t> serializeSnapshot(const VehicleSoftwareSnapshot& snapshot) {
        std::ostringstream oss;
        oss << snapshot.vin << "|"
            << snapshot.snapshot_seq << "|"
            << snapshot.ecu_list.size();
        for (const auto& ecu : snapshot.ecu_list) {
            oss << "|" << ecu.ecu_id << "," << ecu.sw_version.value_or("unknown");
        }
        const std::string& s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot, uint32_t attempt = 0) {
        if (!connected) {
            return false;
        }

        auto start_time = std::chrono::steady_clock::now();

        std::vector<uint8_t> payload = serializeSnapshot(snapshot);
        std::vector<uint8_t> request = buildRequest(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY, payload);

        std::vector<uint8_t> response;
        if (!sendRequest(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY, request, response)) {
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            FotaLogAdapter::inventory_reporter().error(
                fota_events::TBOX_SUBMIT_FAILED,
                "TBOX submission failed - send request failed",
                {flog::f_str("report_id", current_report_id()),
                 flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
                 flog::f_str("someip_service_id", hex_id(service_id)),
                 flog::f_str("someip_method_id", hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)),
                 flog::f_int("attempt", static_cast<int64_t>(attempt)),
                 flog::f_int("duration_ms", duration_ms),
                 flog::f_str("error_code", "CGW-FOTA-1005")}
            );
            return false;
        }

        SomeIpTboxHeader resp_header;
        std::vector<uint8_t> resp_payload;
        if (!parseResponse(response, resp_header, resp_payload)) {
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            FotaLogAdapter::inventory_reporter().error(
                fota_events::TBOX_SUBMIT_FAILED,
                "TBOX submission failed - parse response failed",
                {flog::f_str("report_id", current_report_id()),
                 flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
                 flog::f_str("someip_service_id", hex_id(service_id)),
                 flog::f_str("someip_method_id", hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)),
                 flog::f_int("attempt", static_cast<int64_t>(attempt)),
                 flog::f_int("duration_ms", duration_ms),
                 flog::f_str("error_code", "CGW-FOTA-1004")}
            );
            return false;
        }

        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        FotaLogAdapter::inventory_reporter().info(
            fota_events::TBOX_SUBMIT_SUCCEEDED,
            "TBOX-SOMEIP accepted software inventory snapshot",
            {flog::f_str("report_id", current_report_id()),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_int("duration_ms", duration_ms)}
        );

        return true;
    }

    bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                         uint32_t max_retries,
                                         uint32_t retry_interval_ms) {
        if (!connected) {
            return false;
        }

        for (uint32_t attempt = 0; attempt <= max_retries; ++attempt) {
            if (reportSoftwareInventory(snapshot, attempt)) {
                return true;
            }

            if (attempt < max_retries) {
                FotaLogAdapter::inventory_reporter().warn(
                    "fota.tbox.retry",
                    "Retrying TBOX submission",
                    {flog::f_str("report_id", current_report_id()),
                     flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
                     flog::f_int("attempt", static_cast<int64_t>(attempt + 2)),
                     flog::f_int("max_attempts", static_cast<int64_t>(max_retries + 1)),
                     flog::f_int("retry_interval_ms", static_cast<int64_t>(retry_interval_ms))}
                );
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
            }
        }

        // All retries exhausted - final failure already logged in reportSoftwareInventory
        return false;
    }
};

SomeIpTboxClient::SomeIpTboxClient() : pimpl_(std::make_unique<Impl>()) {}

SomeIpTboxClient::~SomeIpTboxClient() = default;

bool SomeIpTboxClient::connect(const std::string& ip_address, uint16_t port,
                              uint16_t service_id, uint16_t instance_id) {
    return pimpl_->connect(ip_address, port, service_id, instance_id);
}

bool SomeIpTboxClient::disconnect() {
    return pimpl_->disconnect();
}

bool SomeIpTboxClient::isConnected() const {
    return pimpl_->isConnected();
}

bool SomeIpTboxClient::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    return pimpl_->reportSoftwareInventory(snapshot);
}

bool SomeIpTboxClient::reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                                       uint32_t max_retries,
                                                       uint32_t retry_interval_ms) {
    return pimpl_->reportSoftwareInventoryWithRetry(snapshot, max_retries, retry_interval_ms);
}

} // namespace cgw_fota
