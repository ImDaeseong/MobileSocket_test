#pragma once
#include <boost/asio.hpp>
#include <json/json.h>
#include <functional>
#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include <iostream>

class SocketManager : public std::enable_shared_from_this<SocketManager> {
public:
    // ������
    static std::shared_ptr<SocketManager> create(boost::asio::io_context& io_context);
    ~SocketManager();

    // ������ ����
    void connect(const std::string& host, int port);

    // ������ ���� ����
    void disconnect();

    // �޽��� ����
    void send(const Json::Value& message);

    // ���� ���� ���� Ȯ��
    bool isConnected() const;

    // ���� �̺�Ʈ ������ ����
    void setOnReceiveListener(std::function<void(const Json::Value&)> listener);

    // ���� ���� �̺�Ʈ ������ ����
    void setOnConnectListener(std::function<void()> listener);

    // ���� ���� �̺�Ʈ ������ ����
    void setOnDisconnectListener(std::function<void()> listener);

    // ���� �Ϸ� �̺�Ʈ ������ ����
    void setOnSendCompleteListener(std::function<void(size_t)> listener);

private:
    // ������ (private)
    SocketManager(boost::asio::io_context& io_context);

    // �񵿱� ���� ó��
    void doConnect(const boost::asio::ip::tcp::resolver::results_type& endpoints);

    // ���� ó��
    void handleConnect(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint);

    // �޽��� ���� ó��
    void doRead();

    // �޽��� ���� ó��
    void doWrite();

    // ���ŵ� �޽��� ó��
    void handleMessage();

    // ��Ʈ��Ʈ ����
    void startHeartbeat();

    // �翬�� ó��
    void handleReconnect();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer heartbeat_timer_;
    boost::asio::steady_timer reconnect_timer_;
    std::queue<std::string> write_queue_;
    std::mutex write_mutex_;
    std::function<void(const Json::Value&)> on_receive_;
    std::function<void()> on_connect_;
    std::function<void()> on_disconnect_;
    std::function<void(size_t)> on_send_complete_;
    std::atomic<bool> connected_;
    int reconnect_attempts_;
    uint32_t message_length_;
    std::vector<char> message_buffer_;
    std::string current_host_;
    int current_port_;

    static const int MAX_RECONNECT_ATTEMPTS = 5;
    static const int RECONNECT_DELAY_MS = 5000;
    static const int HEARTBEAT_INTERVAL_MS = 10000;
    static const size_t MAX_MESSAGE_SIZE = 100 * 1024 * 1024;
};
