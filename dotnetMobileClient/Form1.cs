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

            // 콜백 설정
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
                        Log("하트비트 응답 수신");
                        break;

                    case "chat":
                        Log($"채팅 메시지 수신: {jsonMessage["content"]}");
                        break;

                    case "file_start":
                        Log("파일 전송 시작");
                        await fileManager.HandleFileStart(message);
                        break;

                    case "file_chunk":
                        await fileManager.HandleFileChunk(message);
                        break;

                    case "file_end":
                        string fileName = await fileManager.HandleFileEnd(message);
                        if (!string.IsNullOrEmpty(fileName))
                        {
                            Log($"파일 전송 완료: {fileName}");
                        }
                        break;

                    default:
                        Log($"알 수 없는 메시지 타입: {msgType}");
                        break;
                }
            }
            catch (Exception e)
            {
                Log($"메시지 처리 오류: {e.Message}");
            }
        }

        private Task OnConnect()
        {
            Log("서버에 연결됨");
            return Task.CompletedTask;
        }

        private Task OnDisconnect()
        {
            Log("서버와의 연결 종료됨");
            return Task.CompletedTask;
        }

        private Task OnSendComplete(int length)
        {
            Log($"전송 완료: {length} 바이트");
            return Task.CompletedTask;
        }

        private void Log(string message)
        {
            Invoke((MethodInvoker)delegate
            {
                textBox3.AppendText(message + Environment.NewLine);
            });
        }

        //서버 접속
        private async void button1_Click(object sender, EventArgs e)
        {
            string serverIp = textBox1.Text;
            int port = 51111;

            await socketManager.Connect(serverIp, port);
            _ = socketManager.Listen();
        }

        //메시지 전달
        private async void button2_Click(object sender, EventArgs e)
        {
            string message = textBox2.Text;
            var jsonMessage = JsonConvert.SerializeObject(new { type = "chat", content = message });
            await socketManager.SendMessage(jsonMessage);
        }

        //파일 다운로드
        private async void button3_Click(object sender, EventArgs e)
        {
            var jsonMessage = JsonConvert.SerializeObject(new { type = "filerequest", content = "all" });
            await socketManager.SendMessage(jsonMessage);
        }

        //접속 해제
        private async void button4_Click(object sender, EventArgs e)
        {
            await socketManager.Disconnect();
        }
    }
}
