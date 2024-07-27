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

                // ���� ����
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
                    // ��� ��û
                    _cts.Cancel(); 

                    // ��� ����͸� �۾��� �����ϴ� ��� ���� ��ٸ�
                    if (_monitoringTask != null)
                    {
                        await Task.WhenAny(_monitoringTask, Task.Delay(Timeout.Infinite, _cts.Token));
                    }

                    // ���� ����
                    await StopServerAsync(); 
                }
            }
            catch (Exception ex)
            {
                LogMessage($"�� �ݱ� ����: {ex.Message}");
            }
            finally
            {
                _cts?.Dispose();
                _cts = null;
                _monitoringTask = null;
            }
        }

        // ���� ����
        private async Task StartServerAsync()
        {
            try
            {
                if (_cts == null)
                {
                    LogMessage("��� ��ū �ҽ��� �ʱ�ȭ���� �ʾҽ��ϴ�.");
                    return;
                }

                // ���� ����
                await Task.Run(() => _server.StartAsync(_cts.Token));
                LogMessage("������ ���۵Ǿ����ϴ�.");

                // ��� ����͸� ����
                if (_cts != null)
                {
                    _monitoringTask = Task.Run(() => _server.MonitorStatsAsync(_cts.Token));
                }
            }
            catch (Exception ex)
            {
                LogMessage($"���� ���� ����: {ex.Message}");
            }
        }

        // ���� ���� 
        private async Task StopServerAsync()
        {
            try
            {
                if (_server != null)
                {
                    // ���� ����
                    await _server.StopAsync(); 
                    LogMessage("������ ����Ǿ����ϴ�.");
                }
            }
            catch (Exception ex)
            {
                LogMessage($"���� ���� ����: {ex.Message}");
            }
            finally
            {
                _cts?.Dispose();
                _cts = null;
                _monitoringTask = null;
            }
        }

        // �α� 
        private void LogMessage(string message)
        {
            if (InvokeRequired)
            {
                Invoke(new Action<string>(LogMessage), message);
                return;
            }

            richTextBox1.AppendText($"{DateTime.Now}: {message}{Environment.NewLine}");
        }

        // ���� ���� ��ư 
        private async void button1_Click(object sender, EventArgs e)
        {
            // ������ �̹� ���� ������ Ȯ��
            if (_server != null && _cts != null && !_cts.IsCancellationRequested)
            {
                LogMessage("������ �̹� ���� ���Դϴ�."); 
                return;
            }

            // ���� �� ��� ��ū �ҽ� �ʱ�ȭ
            _server = new Server(LogMessage);
            _cts = new CancellationTokenSource();

            // ���� ����
            await StartServerAsync(); 
        }

        // ���� ���� ��ư 
        private async void button2_Click(object sender, EventArgs e)
        {
            if (_cts != null)
            {
                // ��� ��û
                _cts.Cancel(); 

                // ��� ����͸� �۾��� �����ϴ� ��� ���� ��ٸ�
                if (_monitoringTask != null)
                {
                    await Task.WhenAny(_monitoringTask, Task.Delay(Timeout.Infinite, _cts.Token));
                }

                // ���� ����
                await StopServerAsync(); 
            }
            else
            {
                LogMessage("������ ����ǰ� ���� �ʰų� ��� ��ū �ҽ��� �ʱ�ȭ���� �ʾҽ��ϴ�.");
            }
        }
    }

}
