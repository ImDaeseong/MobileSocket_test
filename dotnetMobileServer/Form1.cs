using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace dotnetMobileServer
{
    public partial class Form1 : Form
    {
        private Server _server; 
        private CancellationTokenSource _cts;
        private Task _monitoringTask;

        public Form1()
        {
            InitializeComponent();
        }

        private async void Form1_Load(object sender, EventArgs e)
        {
            try
            {
                _server = new Server(LogMessage);
                _cts = new CancellationTokenSource();

                // 서버 시작
                await StartServerAsync();
            }
            catch (Exception ex)
            {
                Application.Exit();
            }
        }

        private async void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            try
            {
                if (_cts != null)
                {
                    // 취소 요청
                    _cts.Cancel(); 

                    // 통계 모니터링 작업이 존재하는 경우 종료 기다림
                    if (_monitoringTask != null)
                    {
                        await Task.WhenAny(_monitoringTask, Task.Delay(Timeout.Infinite, _cts.Token));
                    }

                    // 서버 중지
                    await StopServerAsync(); 
                }
            }
            catch (Exception ex)
            {
                LogMessage($"폼 닫기 오류: {ex.Message}");
            }
            finally
            {
                _cts?.Dispose();
                _cts = null;
                _monitoringTask = null;
            }
        }

        // 서버 시작
        private async Task StartServerAsync()
        {
            try
            {
                if (_cts == null)
                {
                    LogMessage("취소 토큰 소스가 초기화되지 않았습니다.");
                    return;
                }

                // 서버 시작
                await Task.Run(() => _server.StartAsync(_cts.Token));
                LogMessage("서버가 시작되었습니다.");

                // 통계 모니터링 시작
                if (_cts != null)
                {
                    _monitoringTask = Task.Run(() => _server.MonitorStatsAsync(_cts.Token));
                }
            }
            catch (Exception ex)
            {
                LogMessage($"서버 시작 오류: {ex.Message}");
            }
        }

        // 서버 중지 
        private async Task StopServerAsync()
        {
            try
            {
                if (_server != null)
                {
                    // 서버 종료
                    await _server.StopAsync(); 
                    LogMessage("서버가 종료되었습니다.");
                }
            }
            catch (Exception ex)
            {
                LogMessage($"서버 종료 오류: {ex.Message}");
            }
            finally
            {
                _cts?.Dispose();
                _cts = null;
                _monitoringTask = null;
            }
        }

        // 로그 
        private void LogMessage(string message)
        {
            if (InvokeRequired)
            {
                Invoke(new Action<string>(LogMessage), message);
                return;
            }

            richTextBox1.AppendText($"{DateTime.Now}: {message}{Environment.NewLine}");
        }

        // 서버 시작 버튼 
        private async void button1_Click(object sender, EventArgs e)
        {
            // 서버가 이미 실행 중인지 확인
            if (_server != null && _cts != null && !_cts.IsCancellationRequested)
            {
                LogMessage("서버가 이미 실행 중입니다."); 
                return;
            }

            // 서버 및 취소 토큰 소스 초기화
            _server = new Server(LogMessage);
            _cts = new CancellationTokenSource();

            // 서버 시작
            await StartServerAsync(); 
        }

        // 서버 중지 버튼 
        private async void button2_Click(object sender, EventArgs e)
        {
            if (_cts != null)
            {
                // 취소 요청
                _cts.Cancel(); 

                // 통계 모니터링 작업이 존재하는 경우 종료 기다림
                if (_monitoringTask != null)
                {
                    await Task.WhenAny(_monitoringTask, Task.Delay(Timeout.Infinite, _cts.Token));
                }

                // 서버 중지
                await StopServerAsync(); 
            }
            else
            {
                LogMessage("서버가 실행되고 있지 않거나 취소 토큰 소스가 초기화되지 않았습니다.");
            }
        }
    }

}
