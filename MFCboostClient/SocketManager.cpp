#include "pch.h"
#include "SocketManager.h"
#include <boost/bind/bind.hpp>
#include <boost/endian/conversion.hpp>

// 생성자
std::shared_ptr<SocketManager> SocketManager::create(boost::asio::io_context& io_context) {
    return std::shared_ptr<SocketManager>(new SocketManager(io_context));
}

// 생성자
SocketManager::SocketManager(boost::asio::io_context& io_context) : io_context_(io_context),
    socket_(io_context),
    heartbeat_timer_(io_context),
    reconnect_timer_(io_context),
    connected_(false),
    reconnect_attempts_(0) {}

// 소멸자
SocketManager::~SocketManager() {
    disconnect();
}

// 서버에 연결
void SocketManager::connect(const std::string& host, int port) {

    if (connected_) {
        std::cout << "이미 연결되어 있습니다. 먼저 연결을 종료합니다." << std::endl;
        disconnect();
    }

    current_host_ = host;
    current_port_ = port;

    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    std::cout << "서버에 연결 시도: " << host << ":" << port << std::endl;

    doConnect(endpoints);
}

// 비동기 연결 처리
void SocketManager::doConnect(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
    boost::asio::async_connect(socket_, endpoints,
        boost::bind(&SocketManager::handleConnect, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::endpoint));
}

// 연결 처리
void SocketManager::handleConnect(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint) {
    if (!error) {

        connected_ = true;
        reconnect_attempts_ = 0;
        if (on_connect_) on_connect_();
        
        std::cout << "서버에 연결되었습니다." << std::endl;

        doRead();
        startHeartbeat();
    }
    else {

        std::cerr << "연결 실패: " << error.message() << std::endl;
        handleReconnect();
    }
}

// 재연결 처리
void SocketManager::handleReconnect() {
    if (reconnect_attempts_ < MAX_RECONNECT_ATTEMPTS) {
        reconnect_attempts_++;
        std::cout << "재연결 시도 " << reconnect_attempts_ << "/" << MAX_RECONNECT_ATTEMPTS << std::endl;
        reconnect_timer_.expires_after(boost::asio::chrono::milliseconds(RECONNECT_DELAY_MS));
        reconnect_timer_.async_wait([this](const boost::system::error_code& ec) {

            if (!ec) {

                connect(current_host_, current_port_);
            }
            else {

                std::cerr << "재연결 타이머 오류: " << ec.message() << std::endl;
            }
            });
    }
    else {

        std::cout << "최대 재연결 시도 횟수 초과. 연결을 종료합니다." << std::endl;

        if (on_disconnect_) 
            on_disconnect_();
    }
}

// 서버와 연결 종료
void SocketManager::disconnect() {

    if (connected_) {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
        connected_ = false;

        std::cout << "서버와의 연결이 종료되었습니다." << std::endl;

        if (on_disconnect_) 
            on_disconnect_();
    }
}

// 메시지 전송
void SocketManager::send(const Json::Value& message) {

    if (!connected_) {
        std::cerr << "연결되어 있지 않아 메시지를 전송할 수 없습니다." << std::endl;
        return;
    }

    Json::FastWriter writer;
    std::string json_str = writer.write(message);

    std::lock_guard<std::mutex> lock(write_mutex_);
    bool write_in_progress = !write_queue_.empty();
    write_queue_.push(json_str);

    if (!write_in_progress) {
        doWrite();
    }
}

// 현재 연결 상태 확인
bool SocketManager::isConnected() const {
    return connected_;
}

// 수신 이벤트 리스너 설정
void SocketManager::setOnReceiveListener(std::function<void(const Json::Value&)> listener) {
    on_receive_ = listener;
}

// 연결 성공 이벤트 리스너 설정
void SocketManager::setOnConnectListener(std::function<void()> listener) {
    on_connect_ = listener;
}

// 연결 종료 이벤트 리스너 설정
void SocketManager::setOnDisconnectListener(std::function<void()> listener) {
    on_disconnect_ = listener;
}

// 전송 완료 이벤트 리스너 설정
void SocketManager::setOnSendCompleteListener(std::function<void(size_t)> listener) {
    on_send_complete_ = listener;
}

// 비동기 메시지 수신 처리
void SocketManager::doRead() {

    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(&message_length_, sizeof(uint32_t)),
        [this, self](boost::system::error_code ec, std::size_t) {

            if (!ec) {

                message_length_ = boost::endian::big_to_native(message_length_);
                if (message_length_ > MAX_MESSAGE_SIZE) {
                    std::cerr << "메시지 크기가 너무 큽니다. 연결을 종료합니다." << std::endl;
                    disconnect();
                    return;
                }

                message_buffer_.resize(message_length_);
                boost::asio::async_read(socket_,
                    boost::asio::buffer(message_buffer_),
                    [this, self](boost::system::error_code ec, std::size_t) {

                        if (!ec) {
                            handleMessage();
                            doRead();
                        }
                        else {
                            std::cerr << "수신 오류: " << ec.message() << std::endl;
                            disconnect();
                        }
                    });
            }
            else {

                std::cerr << "수신 오류: " << ec.message() << std::endl;
                disconnect();
            }
        });
}

// 비동기 메시지 전송 처리
void SocketManager::doWrite() {

    auto& message = write_queue_.front();
    uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(message.size()));

    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(boost::asio::buffer(&length, sizeof(uint32_t)));
    buffers.push_back(boost::asio::buffer(message));

    boost::asio::async_write(socket_, buffers,
        [this](boost::system::error_code ec, std::size_t bytes_transferred) {

            if (!ec) {

                std::lock_guard<std::mutex> lock(write_mutex_);
                write_queue_.pop();
                if (!write_queue_.empty()) {
                    doWrite();
                }
                if (on_send_complete_) on_send_complete_(bytes_transferred - sizeof(uint32_t));
            }
            else {

                std::cerr << "전송 오류: " << ec.message() << std::endl;
                disconnect();
            }
        });
}

// 수신된 메시지 처리
void SocketManager::handleMessage() {

    Json::Value json_message;
    Json::Reader reader;
    if (reader.parse(message_buffer_.data(), message_buffer_.data() + message_buffer_.size(), json_message)) {

        if (on_receive_)
            on_receive_(json_message);
    }
    else {
        std::cerr << "메시지 파싱 실패." << std::endl;
    }
}

// 하트비트 시작
void SocketManager::startHeartbeat() {

    heartbeat_timer_.expires_after(boost::asio::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
    heartbeat_timer_.async_wait([this](const boost::system::error_code& error) {

        if (!error && connected_) {
            Json::Value heartbeat;
            heartbeat["type"] = "heartbeat";
            send(heartbeat);
            startHeartbeat(); // 다음 하트비트를 예약
        }
        else if (error) {
            std::cerr << "하트비트 타이머 오류: " << error.message() << std::endl;
        }
        });
}
