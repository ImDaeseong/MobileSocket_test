#include "pch.h"
#include "FileManager.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>

// 디버깅 메시지
void OutputDebugStringIfNeeded(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

// 생성자
FileManager::FileManager() : total_file_size_(0), received_size_(0) {}

// 파일 다운로드 시작
void FileManager::startFileDownload(const std::string& fileName, size_t fileSize) {

    current_file_name_ = fileName;
    total_file_size_ = fileSize;
    received_size_ = 0;
    file_buffer_.clear();
    file_buffer_.reserve(fileSize);
}

// Base64 디코딩
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
        else if (c == '=') continue; // 패딩 처리

        val = (val << 6) + c;
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return decoded;
}

// 파일 청크 추가
void FileManager::appendFileChunk(const std::string& base64Chunk) {

    std::string decoded_chunk = base64Decode(base64Chunk);
    file_buffer_.insert(file_buffer_.end(), decoded_chunk.begin(), decoded_chunk.end());
    received_size_ += decoded_chunk.size();
}

// 파일 다운로드 완료
void FileManager::finishFileDownload() {

    if (received_size_ == total_file_size_) 
    {
        saveFile();
    }
    else
    {
        OutputDebugStringIfNeeded("파일 크기 불일치: 수신된 크기 " + std::to_string(received_size_) + ", 예상 크기 " + std::to_string(total_file_size_) + "\n");
    }
}

// 파일 저장
void FileManager::saveFile() const {

    // 현재 실행 파일의 경로 얻기
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {

        OutputDebugStringIfNeeded("실행 파일 경로를 가져오는 데 실패했습니다.\n");
        return;
    }

    // 실행 파일 경로에서 디렉토리 추출
    boost::filesystem::path exe_dir = boost::filesystem::path(exePath).parent_path();

    // 'download' 폴더 경로 생성
    boost::filesystem::path download_dir = exe_dir / "download";

    // 폴더가 존재하지 않으면 생성
    if (!boost::filesystem::exists(download_dir)) {

        if (!boost::filesystem::create_directory(download_dir))
        {
            OutputDebugStringIfNeeded("다운로드 폴더 생성에 실패했습니다.\n");
            return;
        }
    }

    // 'download' 폴더에 저장할 파일 경로 생성
    boost::filesystem::path file_path = download_dir / current_file_name_;

    // 파일 저장
    std::ofstream file(file_path.string(), std::ios::binary);
    if (file.is_open()) 
    {
        file.write(file_buffer_.data(), file_buffer_.size());
        file.close();

        OutputDebugStringIfNeeded("파일이 저장되었습니다: " + file_path.string() + "\n");
    }
    else 
    {

        OutputDebugStringIfNeeded("파일을 열 수 없습니다: " + file_path.string() + "\n");
    }
}
