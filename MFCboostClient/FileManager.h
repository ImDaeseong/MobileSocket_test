#pragma once
#include <string>
#include <vector>

class FileManager {
public:
    FileManager();

    // 파일 다운로드 시작
    void startFileDownload(const std::string& fileName, size_t fileSize);

    // 파일 청크 추가
    void appendFileChunk(const std::string& base64Chunk);

    // 파일 다운로드 완료
    void finishFileDownload();

private:
    // 파일 저장
    void saveFile() const;

    // Base64 디코딩
    static std::string base64Decode(const std::string& base64);

    std::string current_file_name_; // 현재 파일 이름
    std::vector<char> file_buffer_; // 파일 데이터를 저장할 버퍼
    size_t total_file_size_; // 파일의 총 크기
    size_t received_size_; // 수신된 데이터의 크기
};
