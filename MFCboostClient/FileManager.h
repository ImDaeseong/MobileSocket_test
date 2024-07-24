#pragma once
#include <string>
#include <vector>

class FileManager {
public:
    FileManager();

    // ���� �ٿ�ε� ����
    void startFileDownload(const std::string& fileName, size_t fileSize);

    // ���� ûũ �߰�
    void appendFileChunk(const std::string& base64Chunk);

    // ���� �ٿ�ε� �Ϸ�
    void finishFileDownload();

private:
    // ���� ����
    void saveFile() const;

    // Base64 ���ڵ�
    static std::string base64Decode(const std::string& base64);

    std::string current_file_name_; // ���� ���� �̸�
    std::vector<char> file_buffer_; // ���� �����͸� ������ ����
    size_t total_file_size_; // ������ �� ũ��
    size_t received_size_; // ���ŵ� �������� ũ��
};
