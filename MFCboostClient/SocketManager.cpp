#include "pch.h"
#include "SocketManager.h"
#include <boost/bind/bind.hpp>
#include <boost/endian/conversion.hpp>

// ������
std::shared_ptr<SocketManager> SocketManager::create(boost::asio::io_context& io_context) {
    return std::shared_ptr<SocketManager>(new SocketManager(io_context));
}

// ������
SocketManager::SocketManager(boost::asio::io_context& io_context) : io_context_(io_context),
    socket_(io_context),
    heartbeat_timer_(io_context),
    reconnect_timer_(io_context),
    connected_(false),
    reconnect_attempts_(0) {}

// �Ҹ���
SocketManager::~SocketManager() {
    disconnect();
}

// ������ ����
void SocketManager::connect(const std::string& host, int port) {

    if (connected_) {
        std::cout << "�̹� ����Ǿ� �ֽ��ϴ�. ���� ������ �����մϴ�." << std::endl;
        disconnect();
    }

    current_host_ = host;
    current_port_ = port;

    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    std::cout << "������ ���� �õ�: " << host << ":" << port << std::endl;

    doConnect(endpoints);
}

// �񵿱� ���� ó��
void SocketManager::doConnect(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
    boost::asio::async_connect(socket_, endpoints,
        boost::bind(&SocketManager::handleConnect, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::endpoint));
}

// ���� ó��
void SocketManager::handleConnect(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint) {
    if (!error) {

        connected_ = true;
        reconnect_attempts_ = 0;
        if (on_connect_) on_connect_();
        
        std::cout << "������ ����Ǿ����ϴ�." << std::endl;

        doRead();
        startHeartbeat();
    }
    else {

        std::cerr << "���� ����: " << error.message() << std::endl;
        handleReconnect();
    }
}

// �翬�� ó��
void SocketManager::handleReconnect() {
    if (reconnect_attempts_ < MAX_RECONNECT_ATTEMPTS) {
        reconnect_attempts_++;
        std::cout << "�翬�� �õ� " << reconnect_attempts_ << "/" << MAX_RECONNECT_ATTEMPTS << std::endl;
        reconnect_timer_.expires_after(boost::asio::chrono::milliseconds(RECONNECT_DELAY_MS));
        reconnect_timer_.async_wait([this](const boost::system::error_code& ec) {

            if (!ec) {

                connect(current_host_, current_port_);
            }
            else {

                std::cerr << "�翬�� Ÿ�̸� ����: " << ec.message() << std::endl;
            }
            });
    }
    else {

        std::cout << "�ִ� �翬�� �õ� Ƚ�� �ʰ�. ������ �����մϴ�." << std::endl;

        if (on_disconnect_) 
            on_disconnect_();
    }
}

// ������ ���� ����
void SocketManager::disconnect() {

    if (connected_) {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
        connected_ = false;

        std::cout << "�������� ������ ����Ǿ����ϴ�." << std::endl;

        if (on_disconnect_) 
            on_disconnect_();
    }
}

// �޽��� ����
void SocketManager::send(const Json::Value& message) {

    if (!connected_) {
        std::cerr << "����Ǿ� ���� �ʾ� �޽����� ������ �� �����ϴ�." << std::endl;
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

// ���� ���� ���� Ȯ��
bool SocketManager::isConnected() const {
    return connected_;
}

// ���� �̺�Ʈ ������ ����
void SocketManager::setOnReceiveListener(std::function<void(const Json::Value&)> listener) {
    on_receive_ = listener;
}

// ���� ���� �̺�Ʈ ������ ����
void SocketManager::setOnConnectListener(std::function<void()> listener) {
    on_connect_ = listener;
}

// ���� ���� �̺�Ʈ ������ ����
void SocketManager::setOnDisconnectListener(std::function<void()> listener) {
    on_disconnect_ = listener;
}

// ���� �Ϸ� �̺�Ʈ ������ ����
void SocketManager::setOnSendCompleteListener(std::function<void(size_t)> listener) {
    on_send_complete_ = listener;
}

// �񵿱� �޽��� ���� ó��
void SocketManager::doRead() {

    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(&message_length_, sizeof(uint32_t)),
        [this, self](boost::system::error_code ec, std::size_t) {

            if (!ec) {

                message_length_ = boost::endian::big_to_native(message_length_);
                if (message_length_ > MAX_MESSAGE_SIZE) {
                    std::cerr << "�޽��� ũ�Ⱑ �ʹ� Ů�ϴ�. ������ �����մϴ�." << std::endl;
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
                            std::cerr << "���� ����: " << ec.message() << std::endl;
                            disconnect();
                        }
                    });
            }
            else {

                std::cerr << "���� ����: " << ec.message() << std::endl;
                disconnect();
            }
        });
}

// �񵿱� �޽��� ���� ó��
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

                std::cerr << "���� ����: " << ec.message() << std::endl;
                disconnect();
            }
        });
}

// ���ŵ� �޽��� ó��
void SocketManager::handleMessage() {

    Json::Value json_message;
    Json::Reader reader;
    if (reader.parse(message_buffer_.data(), message_buffer_.data() + message_buffer_.size(), json_message)) {

        if (on_receive_)
            on_receive_(json_message);
    }
    else {
        std::cerr << "�޽��� �Ľ� ����." << std::endl;
    }
}

// ��Ʈ��Ʈ ����
void SocketManager::startHeartbeat() {

    heartbeat_timer_.expires_after(boost::asio::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
    heartbeat_timer_.async_wait([this](const boost::system::error_code& error) {

        if (!error && connected_) {
            Json::Value heartbeat;
            heartbeat["type"] = "heartbeat";
            send(heartbeat);
            startHeartbeat(); // ���� ��Ʈ��Ʈ�� ����
        }
        else if (error) {
            std::cerr << "��Ʈ��Ʈ Ÿ�̸� ����: " << error.message() << std::endl;
        }
        });
}
