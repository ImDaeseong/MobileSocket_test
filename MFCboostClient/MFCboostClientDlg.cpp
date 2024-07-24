#include "pch.h"
#include "framework.h"
#include "MFCboostClient.h"
#include "MFCboostClientDlg.h"
#include "afxdialogex.h"

#include <boost/asio/ip/tcp.hpp>
#include <json/json.h>
#include <iphlpapi.h>
#include <chrono>
#include <thread>
#include <string>

#pragma comment(lib, "iphlpapi.lib")


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


CMFCboostClientDlg::CMFCboostClientDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_MFCBOOSTCLIENT_DIALOG, pParent)
{
}

void CMFCboostClientDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_ctrlIP);
	DDX_Control(pDX, IDC_EDIT2, m_ctrlMessage);
	DDX_Control(pDX, IDC_EDIT3, m_ctrlLog);
	DDX_Control(pDX, IDC_BUTTON1, m_ctrlConnect);
	DDX_Control(pDX, IDC_BUTTON2, m_ctrlSend);
	DDX_Control(pDX, IDC_BUTTON3, m_ctrlFile);
}

BEGIN_MESSAGE_MAP(CMFCboostClientDlg, CDialogEx)
	ON_WM_PAINT()
	ON_BN_CLICKED(IDC_BUTTON1, &CMFCboostClientDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CMFCboostClientDlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &CMFCboostClientDlg::OnBnClickedButton3)
END_MESSAGE_MAP()

BOOL CMFCboostClientDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

    socket_manager_ = SocketManager::create(io_context_);

    file_manager_ = std::make_unique<FileManager>();

    setupSocketListeners();

    io_thread_ = std::thread([this]() {

        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard(io_context_.get_executor());
        io_context_.run();        
        });

    updateButtonState(false);

	return TRUE;  
}

void CMFCboostClientDlg::OnPaint()
{
	CPaintDC dc(this);
}

//취소
void CMFCboostClientDlg::OnCancel()
{
    should_monitor_network_ = false;
    if (socket_manager_->isConnected()) {
        socket_manager_->disconnect();
    }
    io_context_.stop();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

	CDialogEx::OnCancel();
}

// 접속 
void CMFCboostClientDlg::OnBnClickedButton1()
{
    CString serverIP;
    m_ctrlIP.GetWindowText(serverIP);
    std::string ip = CT2A(serverIP);
    int port = 11011;

    log(_T("서버 연결 시도 중..."));
    socket_manager_->connect(ip, port);
}

// 메시지 전송
void CMFCboostClientDlg::OnBnClickedButton2()
{
    if (!socket_manager_->isConnected()) {
        log(_T("서버에 연결되어 있지 않습니다."));
        return;
    }

    CString sMsg;
    m_ctrlMessage.GetWindowText(sMsg);

    if (!sMsg.IsEmpty()) {

        Json::Value json_message;
        json_message["type"] = "chat";

        // UTF-8 인코딩 보장
        CW2A pszMessage(sMsg, CP_UTF8);
        json_message["content"] = std::string(pszMessage);

        socket_manager_->send(json_message);

        log(_T("메시지 전달: ") + sMsg);
        
        m_ctrlMessage.SetWindowText(_T(""));
    }
}

// 파일 요청
void CMFCboostClientDlg::OnBnClickedButton3()
{
    if (!socket_manager_->isConnected()) {
        log(_T("서버에 연결되어 있지 않습니다."));
        return;
    }

    Json::Value json_message;
    json_message["type"] = "filerequest";
    json_message["content"] = "all";
    socket_manager_->send(json_message);
    log(_T("파일 요청"));
}


// 소켓 리스너
void CMFCboostClientDlg::setupSocketListeners() {

    socket_manager_->setOnReceiveListener([this](const Json::Value& message) {

        std::string type = message["type"].asString();
        
        if (type == "heartbeat_ack") {

            log(_T("라이브 메시지"));
        }
        else if (type == "chat") {

            log(_T("받은 내용: ") + CString(message["content"].asCString()));
        }
        else if (type == "file_start") {

            Json::Value content = message["content"];
            std::string fileName = content["filename"].asString();
            size_t fileSize = content["filesize"].asUInt64();

            file_manager_->startFileDownload(fileName, fileSize);

            log(_T("파일 다운로드 시작: ") + CString(fileName.c_str()));
        }
        else if (type == "file_chunk") {

            std::string fileChunk = message["content"].asString();

            file_manager_->appendFileChunk(fileChunk);
        }
        else if (type == "file_end") {

            Json::Value content = message["content"];
            std::string fileName = content["filename"].asString();

            file_manager_->finishFileDownload();

            log(_T("파일 다운로드 완료: ") + CString(fileName.c_str()));
        }
        else {

            log(_T("알 수 없는 메시지 타입: ") + CString(type.c_str()));
        }
        
        });

    // 접속
    socket_manager_->setOnConnectListener([this]() {

        log(_T("서버 접속"));

        updateButtonState(true);

        should_monitor_network_ = true;
        startNetworkQualityMonitoring();

        });

    // 접속 해제
    socket_manager_->setOnDisconnectListener([this]() {

        log(_T("서버 접속 끊김"));

        updateButtonState(false);
        should_monitor_network_ = false;

        });

    // 전송 완료
    socket_manager_->setOnSendCompleteListener([this](size_t bytesSent) {

        CString message;
        message.Format(_T("보낸 bytes: %zu"), bytesSent);
        log(message);

        });

}

// 로그 메시지
void CMFCboostClientDlg::log(const CString& message) {

    CString sMsg;
    sMsg.Format(_T("%s: %s\r\n"), CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S")), message);

    int lineCount = m_ctrlLog.GetLineCount();
    if (lineCount >= 50) {
        m_ctrlLog.SetWindowText(_T(""));
    }

    int textLength = m_ctrlLog.GetWindowTextLength();
    m_ctrlLog.SetSel(textLength, textLength);
    m_ctrlLog.ReplaceSel(sMsg);
}

// 버튼 상태 업데이트
void CMFCboostClientDlg::updateButtonState(bool isConnected) {

    m_ctrlConnect.EnableWindow(!isConnected);
    m_ctrlSend.EnableWindow(isConnected);
    m_ctrlFile.EnableWindow(isConnected);
}

// 네트워크 품질 모니터
void CMFCboostClientDlg::startNetworkQualityMonitoring() {

    std::thread([this]() {

        while (should_monitor_network_) {

            MIB_IF_ROW2 row;
            ZeroMemory(&row, sizeof(row));
            row.InterfaceLuid.Value = 0;
            row.InterfaceIndex = 0;

            DWORD result = GetIfEntry2(&row);

            if (result == NO_ERROR) {
            
                ULONG64 inBandwidth = row.InOctets;
                ULONG64 outBandwidth = row.OutOctets;

                std::this_thread::sleep_for(std::chrono::seconds(1));

                result = GetIfEntry2(&row);
                if (result == NO_ERROR) {

                    ULONG64 inBandwidthNew = row.InOctets;
                    ULONG64 outBandwidthNew = row.OutOctets;

                    int downstreamBandwidthKbps = static_cast<int>((inBandwidthNew - inBandwidth) * 8 / 1000);
                    int upstreamBandwidthKbps = static_cast<int>((outBandwidthNew - outBandwidth) * 8 / 1000);

                    float quality = calculateNetworkQuality(downstreamBandwidthKbps, upstreamBandwidthKbps);
                    sendNetworkQualityToServer(quality);
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        
        }).detach();
}

// 네트워크 품질 계산
float CMFCboostClientDlg::calculateNetworkQuality(int downstreamBandwidthKbps, int upstreamBandwidthKbps) {

    int totalBandwidth = downstreamBandwidthKbps + upstreamBandwidthKbps;    
    if (totalBandwidth > 10000) return 1.0f;
    else if (totalBandwidth > 5000) return 0.75f;
    else if (totalBandwidth > 2000) return 0.5f;
    else if (totalBandwidth > 1000) return 0.25f;
    else return 0.1f;
}

// 서버에 네트워크 품질 전송
void CMFCboostClientDlg::sendNetworkQualityToServer(float quality) {

    if (socket_manager_->isConnected()) {
    
        Json::Value json_message;
        json_message["type"] = "network_quality";
        json_message["content"] = quality;
        socket_manager_->send(json_message);

        CString sMsg;
        sMsg.Format(_T("네트워크 품질 정보 전송: %.2f"), quality);
        log(sMsg);
    }
}