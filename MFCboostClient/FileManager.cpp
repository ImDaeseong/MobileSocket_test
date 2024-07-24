#include "pch.h"
#include "FileManager.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>

// ����� �޽���
void OutputDebugStringIfNeeded(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

// ������
FileManager::FileManager() : total_file_size_(0), received_size_(0) {}

// ���� �ٿ�ε� ����
void FileManager::startFileDownload(const std::string& fileName, size_t fileSize) {

    current_file_name_ = fileName;
    total_file_size_ = fileSize;
    received_size_ = 0;
    file_buffer_.clear();
    file_buffer_.reserve(fileSize);
}

// Base64 ���ڵ�
std::string FileManager::base64Decode(const std::string& base64) {

    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string decoded;
    decoded.reserve(base64.size() * 3 / 4);

    int val = 0, valb = -8;
    for (unsigned char c : base64) {
        if (c >= 'A' && c <= 'Z') c = c - 'A';
        else if (c >= 'a' && c <= 'z') c = c - 'a' + 26;
        else if (c >= '0' && c <= '9') c = c - '0' + 52;
        else if (c == '+') c = 62;
        else if (c == '/') c = 63;
        else if (c == '=') continue; // �е� ó��

        val = (val << 6) + c;
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return decoded;
}

// ���� ûũ �߰�
void FileManager::appendFileChunk(const std::string& base64Chunk) {

    std::string decoded_chunk = base64Decode(base64Chunk);
    file_buffer_.insert(file_buffer_.end(), decoded_chunk.begin(), decoded_chunk.end());
    received_size_ += decoded_chunk.size();
}

// ���� �ٿ�ε� �Ϸ�
void FileManager::finishFileDownload() {

    if (received_size_ == total_file_size_) 
    {
        saveFile();
    }
    else
    {
        OutputDebugStringIfNeeded("���� ũ�� ����ġ: ���ŵ� ũ�� " + std::to_string(received_size_) + ", ���� ũ�� " + std::to_string(total_file_size_) + "\n");
    }
}

// ���� ����
void FileManager::saveFile() const {

    // ���� ���� ������ ��� ���
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {

        OutputDebugStringIfNeeded("���� ���� ��θ� �������� �� �����߽��ϴ�.\n");
        return;
    }

    // ���� ���� ��ο��� ���丮 ����
    boost::filesystem::path exe_dir = boost::filesystem::path(exePath).parent_path();

    // 'download' ���� ��� ����
    boost::filesystem::path download_dir = exe_dir / "download";

    // ������ �������� ������ ����
    if (!boost::filesystem::exists(download_dir)) {

        if (!boost::filesystem::create_directory(download_dir))
        {
            OutputDebugStringIfNeeded("�ٿ�ε� ���� ������ �����߽��ϴ�.\n");
            return;
        }
    }

    // 'download' ������ ������ ���� ��� ����
    boost::filesystem::path file_path = download_dir / current_file_name_;

    // ���� ����
    std::ofstream file(file_path.string(), std::ios::binary);
    if (file.is_open()) 
    {
        file.write(file_buffer_.data(), file_buffer_.size());
        file.close();

        OutputDebugStringIfNeeded("������ ����Ǿ����ϴ�: " + file_path.string() + "\n");
    }
    else 
    {

        OutputDebugStringIfNeeded("������ �� �� �����ϴ�: " + file_path.string() + "\n");
    }
}
