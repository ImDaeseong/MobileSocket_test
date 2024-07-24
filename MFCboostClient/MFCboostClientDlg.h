#pragma once

#include "SocketManager.h"
#include "FileManager.h"
#include <afxwin.h>
#include <memory>
#include <thread>
#include <atomic>
#include <string>


class CMFCboostClientDlg : public CDialogEx
{
public:
	CMFCboostClientDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MFCBOOSTCLIENT_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.

protected:
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	virtual void OnCancel();
	
	DECLARE_MESSAGE_MAP()

	afx_msg void OnBnClickedButton1();
	afx_msg void OnBnClickedButton2();
	afx_msg void OnBnClickedButton3();

private:
	CEdit m_ctrlIP;          
	CEdit m_ctrlMessage;     
	CEdit m_ctrlLog;         
	CButton m_ctrlConnect;   
	CButton m_ctrlSend;     
	CButton m_ctrlFile;     

private:

	void setupSocketListeners();   // 소켓 리스너 설정
	void log(const CString& message); // 로그 메시지 출력
	void updateButtonState(bool isConnected); // 버튼 상태 업데이트
	void startNetworkQualityMonitoring(); // 네트워크 품질 모니터링 시작
	float calculateNetworkQuality(int downstreamBandwidthKbps, int upstreamBandwidthKbps); // 네트워크 품질 계산
	void sendNetworkQualityToServer(float quality); // 네트워크 품질 정보 서버에 전송

	boost::asio::io_context io_context_; // Boost ASIO IO 컨텍스트
	std::shared_ptr<SocketManager> socket_manager_; // 소켓 매니저
	std::unique_ptr<FileManager> file_manager_; // 파일 매니저
	std::thread io_thread_; // IO 스레드
	std::atomic<bool> should_monitor_network_; // 네트워크 모니터링 여부
};
