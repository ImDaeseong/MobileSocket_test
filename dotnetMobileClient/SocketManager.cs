using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace dotnetMobileClient
{
    public class SocketManager : IDisposable
    {
        // 서버에 연결할 때의 타임아웃 시간(ms)
        private const int ConnectionTimeoutMs = 5000;

        // TCP 클라이언트 객체
        private TcpClient client;

        // 네트워크 스트림 객체
        private NetworkStream stream;

        // 연결 상태
        private bool isConnected = false;

        // 연결 완료
        private Func<Task> onConnectListener;

        // 연결 종료
        private Func<Task> onDisconnectListener;

        // 메시지 수신
        private Func<string, Task> onReceiveListener;

        // 메시지 전송 완료
        private Func<int, Task> onSendCompleteListener;


        // 서버 연결
        public async Task Connect(string serverIp, int port)
        {
            try
            {
                client = new TcpClient();
                var connectTask = client.ConnectAsync(serverIp, port);

                // 타임아웃 내에 연결을 시도
                if (await Task.WhenAny(connectTask, Task.Delay(ConnectionTimeoutMs)) == connectTask)
                {
                    await connectTask;
                    isConnected = true;
                    stream = client.GetStream();
                    if (onConnectListener != null) await onConnectListener();

                    Console.WriteLine($"서버에 연결됨: {serverIp}:{port}");
                }
                else
                {
                    throw new TimeoutException("연결 타임아웃");
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"연결 오류: {e.Message}");
                isConnected = false;
            }
        }

        // 연결 종료
        public async Task Disconnect()
        {
            if (stream != null) await stream.DisposeAsync();
            if (client != null) client.Close();
            isConnected = false;
            if (onDisconnectListener != null) await onDisconnectListener();

            Console.WriteLine("서버와의 연결이 끊어졌습니다.");
        }

        // 메시지 수신
        public async Task Listen()
        {
            var lengthBuffer = new byte[4]; // 메시지 길이를 저장할 버퍼
            while (isConnected)
            {
                try
                {
                    Console.WriteLine("메시지 수신 대기 중...");

                    int bytesRead = await stream.ReadAsync(lengthBuffer, 0, lengthBuffer.Length);
                    if (bytesRead == 0)
                    {
                        Console.WriteLine("연결이 종료되었습니다.");
                        break;
                    }

                    int length = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(lengthBuffer, 0));

                    // 실제 메시지를 저장할 버퍼
                    var buffer = new byte[length]; 
                    bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length);
                    string message = Encoding.UTF8.GetString(buffer, 0, bytesRead);

                    if (onReceiveListener != null) await onReceiveListener(message);
                }
                catch (Exception e)
                {
                    Console.WriteLine($"수신 오류: {e.Message}");
                    break;
                }
            }
            await Disconnect();
        }

        // 메시지 전송
        public async Task SendMessage(string message)
        {
            if (!isConnected) throw new InvalidOperationException("서버에 연결되어 있지 않음");

            var messageBytes = Encoding.UTF8.GetBytes(message);
            var lengthPrefix = BitConverter.GetBytes(IPAddress.HostToNetworkOrder(messageBytes.Length));
            await stream.WriteAsync(lengthPrefix, 0, lengthPrefix.Length); // 메시지 길이 전송
            await stream.WriteAsync(messageBytes, 0, messageBytes.Length); // 실제 메시지 전송
            
            if (onSendCompleteListener != null) await onSendCompleteListener(lengthPrefix.Length + messageBytes.Length);
            
            Console.WriteLine($"메시지 전송 완료: {message}");
        }

        
        public void SetOnReceiveListener(Func<string, Task> listener) => onReceiveListener = listener;
        public void SetOnConnectListener(Func<Task> listener) => onConnectListener = listener;
        public void SetOnDisconnectListener(Func<Task> listener) => onDisconnectListener = listener;
        public void SetOnSendCompleteListener(Func<int, Task> listener) => onSendCompleteListener = listener;

        // 객체 해제 시 연결 종료
        public void Dispose()
        {
            Disconnect().GetAwaiter().GetResult();
        }
    }
}
