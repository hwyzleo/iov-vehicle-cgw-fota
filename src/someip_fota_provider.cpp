#include "someip_fota_provider.h"
#include "constants.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace cgw_fota {

// SOME/IP 消息头结构
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

class SomeIpFotaProvider::Impl {
public:
    Impl(SomeIpFotaProvider* parent) : parent_(parent), running_(false), server_fd_(-1) {}

    ~Impl() {
        stop();
    }

    bool start(const std::string& ip_address, uint16_t port) {
        if (running_.load()) {
            std::cerr << "Provider already running" << std::endl;
            return false;
        }

        // 创建 socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        // 设置 socket 选项
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        // 绑定地址
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(ip_address.c_str());
        address.sin_port = htons(port);

        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to " << ip_address << ":" << port << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        // 开始监听
        if (listen(server_fd_, 3) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        running_.store(true);
        
        // 启动接受连接的线程
        accept_thread_ = std::thread([this]() {
            acceptConnections();
        });

        std::cout << "FOTA Provider started on " << ip_address << ":" << port << std::endl;
        return true;
    }

    bool stop() {
        if (!running_.load()) {
            return true;
        }

        running_.store(false);

        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }

        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::cout << "FOTA Provider stopped" << std::endl;
        return true;
    }

    bool isRunning() const {
        return running_.load();
    }

private:
    SomeIpFotaProvider* parent_;
    std::atomic<bool> running_;
    int server_fd_;
    std::thread accept_thread_;

    void acceptConnections() {
        while (running_.load()) {
            struct sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);

            int client_fd = accept(server_fd_, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                if (running_.load()) {
                    std::cerr << "Failed to accept connection" << std::endl;
                }
                continue;
            }

            // 为每个连接启动处理线程
            std::thread([this, client_fd]() {
                handleConnection(client_fd);
            }).detach();
        }
    }

    void handleConnection(int client_fd) {
        // 读取 SOME/IP 消息头
        SomeIpHeader header;
        ssize_t bytes_read = read(client_fd, &header, sizeof(header));
        
        if (bytes_read != sizeof(header)) {
            std::cerr << "Failed to read SOME/IP header" << std::endl;
            close(client_fd);
            return;
        }

        // 验证 Service ID
        if (ntohs(header.service_id) != FOTA_PROVIDER_SERVICE_ID) {
            std::cerr << "Invalid service ID: 0x" << std::hex << ntohs(header.service_id) << std::endl;
            close(client_fd);
            return;
        }

        // 路由到对应的处理方法
        uint16_t method_id = ntohs(header.method_id);
        
        if (method_id == METHOD_REQUEST_SOFTWARE_INVENTORY) {
            handleRequestSoftwareInventory(client_fd, header);
        } else {
            std::cerr << "Unknown method ID: 0x" << std::hex << method_id << std::endl;
            close(client_fd);
        }
    }

    void handleRequestSoftwareInventory(int client_fd, const SomeIpHeader& request_header) {
        // 读取请求负载
        uint32_t payload_length = ntohl(request_header.length) - sizeof(SomeIpHeader);
        std::vector<uint8_t> payload(payload_length);
        
        ssize_t bytes_read = read(client_fd, payload.data(), payload_length);
        if (bytes_read != payload_length) {
            std::cerr << "Failed to read request payload" << std::endl;
            close(client_fd);
            return;
        }

        // 解析 requestId 和 reason（简化实现）
        // 实际实现应使用 Protobuf
        std::string request_id = "req-" + std::to_string(request_header.session_id);
        std::string reason = "cloud_query";

        // 调用 parent 的 handleRequest
        AsyncReportResult result = parent_->handleRequest(request_id, reason);

        // 构建响应
        SomeIpHeader response_header;
        response_header.service_id = request_header.service_id;
        response_header.method_id = request_header.method_id;
        response_header.client_id = request_header.client_id;
        response_header.session_id = request_header.session_id;
        response_header.protocol_version = request_header.protocol_version;
        response_header.interface_version = request_header.interface_version;
        response_header.message_type = 0x80;  // Response
        response_header.return_code = 0x00;   // OK

        // 响应负载: accepted (1 byte) + reportId (8 bytes)
        uint8_t accepted = result.accepted ? 1 : 0;
        // 转换为网络字节序
        uint64_t report_id_network = 0;
        uint64_t report_id_host = result.report_id;
        for (int i = 0; i < 8; i++) {
            ((uint8_t*)&report_id_network)[i] = (report_id_host >> (56 - i * 8)) & 0xFF;
        }

        std::vector<uint8_t> response_payload;
        response_payload.push_back(accepted);
        for (int i = 0; i < 8; i++) {
            response_payload.push_back(((uint8_t*)&report_id_network)[i]);
        }

        response_header.length = htonl(sizeof(SomeIpHeader) + response_payload.size());

        // 发送响应
        write(client_fd, &response_header, sizeof(response_header));
        write(client_fd, response_payload.data(), response_payload.size());

        close(client_fd);
    }
};

SomeIpFotaProvider::SomeIpFotaProvider(std::shared_ptr<InventoryReporter> reporter)
    : pimpl_(std::make_unique<Impl>(this))
    , reporter_(reporter)
{
}

SomeIpFotaProvider::~SomeIpFotaProvider() = default;

bool SomeIpFotaProvider::start(const std::string& ip_address, uint16_t port) {
    return pimpl_->start(ip_address, port);
}

bool SomeIpFotaProvider::stop() {
    return pimpl_->stop();
}

bool SomeIpFotaProvider::isRunning() const {
    return pimpl_->isRunning();
}

AsyncReportResult SomeIpFotaProvider::handleRequest(const std::string& request_id, 
                                                    const std::string& reason) {
    return reporter_->reportInventoryAsync(request_id, reason);
}

} // namespace cgw_fota
