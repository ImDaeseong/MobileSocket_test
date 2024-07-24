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
    // 생성자
    static std::shared_ptr<SocketManager> create(boost::asio::io_context& io_context);
    ~SocketManager();

    // 서버에 연결
    void connect(const std::string& host, int port);

    // 서버와 연결 종료
    void disconnect();

    // 메시지 전송
    void send(const Json::Value& message);

    // 현재 연결 상태 확인
    bool isConnected() const;

    // 수신 이벤트 리스너 설정
    void setOnReceiveListener(std::function<void(const Json::Value&)> listener);

    // 연결 성공 이벤트 리스너 설정
    void setOnConnectListener(std::function<void()> listener);

    // 연결 종료 이벤트 리스너 설정
    void setOnDisconnectListener(std::function<void()> listener);

    // 전송 완료 이벤트 리스너 설정
    void setOnSendCompleteListener(std::function<void(size_t)> listener);

private:
    // 생성자 (private)
    SocketManager(boost::asio::io_context& io_context);

    // 비동기 연결 처리
    void doConnect(const boost::asio::ip::tcp::resolver::results_type& endpoints);

    // 연결 처리
    void handleConnect(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint);

    // 메시지 수신 처리
    void doRead();

    // 메시지 전송 처리
    void doWrite();

    // 수신된 메시지 처리
    void handleMessage();

    // 하트비트 시작
    void startHeartbeat();

    // 재연결 처리
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
