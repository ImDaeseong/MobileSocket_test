using System;
using System.Threading.Tasks;
using System.Windows.Forms;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace dotnetMobileClient
{
    public partial class Form1 : Form
    {
        private readonly SocketManager socketManager;
        private readonly FileManager fileManager;

        public Form1()
        {
            InitializeComponent();

            socketManager = new SocketManager();
            fileManager = new FileManager();

            // �ݹ� ����
            socketManager.SetOnReceiveListener(OnReceive);
            socketManager.SetOnConnectListener(OnConnect);
            socketManager.SetOnDisconnectListener(OnDisconnect);
            socketManager.SetOnSendCompleteListener(OnSendComplete);
        }

        private async Task OnReceive(string message)
        {
            try
            {
                var jsonMessage = JsonConvert.DeserializeObject<JObject>(message);
                var msgType = jsonMessage["type"].ToString();

                switch (msgType)
                {
                    case "heartbeat_ack":
                        Log("��Ʈ��Ʈ ���� ����");
                        break;

                    case "chat":
                        Log($"ä�� �޽��� ����: {jsonMessage["content"]}");
                        break;

                    case "file_start":
                        Log("���� ���� ����");
                        await fileManager.HandleFileStart(message);
                        break;

                    case "file_chunk":
                        await fileManager.HandleFileChunk(message);
                        break;

                    case "file_end":
                        string fileName = await fileManager.HandleFileEnd(message);
                        if (!string.IsNullOrEmpty(fileName))
                        {
                            Log($"���� ���� �Ϸ�: {fileName}");
                        }
                        break;

                    default:
                        Log($"�� �� ���� �޽��� Ÿ��: {msgType}");
                        break;
                }
            }
            catch (Exception e)
            {
                Log($"�޽��� ó�� ����: {e.Message}");
            }
        }

        private Task OnConnect()
        {
            Log("������ �����");
            return Task.CompletedTask;
        }

        private Task OnDisconnect()
        {
            Log("�������� ���� �����");
            return Task.CompletedTask;
        }

        private Task OnSendComplete(int length)
        {
            Log($"���� �Ϸ�: {length} ����Ʈ");
            return Task.CompletedTask;
        }

        private void Log(string message)
        {
            Invoke((MethodInvoker)delegate
            {
                textBox3.AppendText(message + Environment.NewLine);
            });
        }

        //���� ����
        private async void button1_Click(object sender, EventArgs e)
        {
            string serverIp = textBox1.Text;
            int port = 51111;

            await socketManager.Connect(serverIp, port);
            _ = socketManager.Listen();
        }

        //�޽��� ����
        private async void button2_Click(object sender, EventArgs e)
        {
            string message = textBox2.Text;
            var jsonMessage = JsonConvert.SerializeObject(new { type = "chat", content = message });
            await socketManager.SendMessage(jsonMessage);
        }

        //���� �ٿ�ε�
        private async void button3_Click(object sender, EventArgs e)
        {
            var jsonMessage = JsonConvert.SerializeObject(new { type = "filerequest", content = "all" });
            await socketManager.SendMessage(jsonMessage);
        }

        //���� ����
        private async void button4_Click(object sender, EventArgs e)
        {
            await socketManager.Disconnect();
        }
    }
}
