using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;

namespace dotnetMobileServer
{
    // 서버 
    class Server
    {
        
        public const int DefaultChunkSize = 16 * 1024;
        public const int MinChunkSize = 4 * 1024;
        public const int MaxChunkSize = 64 * 1024;

        // 파일 저장 디렉터리 및 최대 파일 크기
        public const string FilesDir = "./files";
        public const long MaxFileSize = 100L * 1024 * 1024;

        private TcpListener _listener;
        private readonly Stats _stats = new();
        private readonly Action<string> _logAction;

       
        public Server(Action<string> logAction)
        {
            _logAction = logAction ?? throw new ArgumentNullException(nameof(logAction));
        }


        // 로그 기록 
        private void Log(string message) => _logAction(message);


        // 서버 시작
        public async Task StartAsync(CancellationToken cancellationToken)
        {
            _listener = new TcpListener(IPAddress.Any, 11011);
            _listener.Start();
            Log("서버 시작: 0.0.0.0:11011");

            try
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    var tcpClient = await _listener.AcceptTcpClientAsync(cancellationToken);
                    _ = Task.Run(() => HandleClientAsync(tcpClient, cancellationToken), cancellationToken);
                }
            }
            catch (OperationCanceledException)
            {
            }
            catch (Exception ex)
            {
                Log($"클라이언트 연결 처리 중 오류 발생: {ex.Message}");
            }
        }


        // 서버 종료 
        public Task StopAsync()
        {
            _listener.Stop();
            Log("서버 종료 완료");
            return Task.CompletedTask;
        }


        // 클라이언트 연결 처리
        private async Task HandleClientAsync(TcpClient tcpClient, CancellationToken cancellationToken)
        {
            var client = new Client(tcpClient);
            var clientId = ((IPEndPoint)tcpClient.Client.RemoteEndPoint)?.ToString() ?? "unknown";
            Log($"클라이언트 연결: {clientId}");
            await _stats.IncrementActiveConnections();

            try
            {
                using var stream = tcpClient.GetStream();
                using var reader = new BinaryReader(stream, Encoding.UTF8, true);
                using var writer = new BinaryWriter(stream, Encoding.UTF8, true);

                while (!cancellationToken.IsCancellationRequested)
                {
                    //바이트 데이터를 json 으로 변경
                    var message = await ReadMessageAsync(reader, cancellationToken);
                    if (message == null) break;

                    //클라이언트 요청에 대한 메시지 처리
                    await ProcessMessageAsync(client, clientId, message, writer, cancellationToken);
                    client.LastSeen = DateTime.Now;
                }
            }
            catch (Exception ex)
            {
                Log($"클라이언트 처리 중 오류 발생: {ex.Message}");
            }
            finally
            {
                tcpClient.Close();
                await _stats.DecrementActiveConnections();
                Log($"클라이언트 연결 종료: {clientId}");
            }
        }


        // 메시지 처리
        private async Task ProcessMessageAsync(Client client, string clientId, Dictionary<string, object> message, BinaryWriter writer, CancellationToken cancellationToken)
        {
            var type = message["type"].ToString();
            switch (type)
            {
                case "heartbeat":
                    Log($"클라이언트 {clientId}로부터 heartbeat 신호 수신");
                    await SendMessageAsync(writer, new { type = "heartbeat_ack" });
                    break;

                case "chat":
                    Log($"클라이언트 {clientId}로부터 메시지 받음: {message["content"]}");
                    break;

                case "filerequest":
                    Log($"클라이언트 {clientId}로부터 파일 요청 수신");
                    await SendFilesToClientAsync(client, writer, cancellationToken);
                    break;

                case "network_quality":
                    client.NetworkQuality = Convert.ToDouble(message["content"]);
                    Log($"클라이언트 {clientId}의 네트워크 품질 업데이트: {client.NetworkQuality}");
                    break;

                default:
                    Log($"알 수 없는 메시지 타입: {type}");
                    break;
            }
        }


        // 메시지 읽기
        private async Task<Dictionary<string, object>?> ReadMessageAsync(BinaryReader reader, CancellationToken cancellationToken)
        {
            try
            {
                var length = IPAddress.NetworkToHostOrder(reader.ReadInt32());
                var messageData = reader.ReadBytes(length);
                var messageJson = Encoding.UTF8.GetString(messageData);
                return JsonConvert.DeserializeObject<Dictionary<string, object>>(messageJson);
            }
            catch (EndOfStreamException)
            {
                return null;
            }
            catch (Exception ex)
            {
                Log($"메시지 읽기 오류: {ex.Message}");
                return null;
            }
        }


        // 메시지 전달
        private async Task SendMessageAsync(BinaryWriter writer, object message)
        {
            try
            {
                var messageJson = JsonConvert.SerializeObject(message);
                var messageData = Encoding.UTF8.GetBytes(messageJson);
                writer.Write(IPAddress.HostToNetworkOrder(messageData.Length));
                writer.Write(messageData);
                await writer.BaseStream.FlushAsync(); 
            }
            catch (Exception ex)
            {
                Log($"메시지 전송 오류: {ex.Message}");
            }
        }


        // 클라이언트에 따른 청크 크기 결정 메서드
        private int GetChunkSize(Client client) =>
            Math.Max(MinChunkSize, Math.Min(MaxChunkSize, (int)(DefaultChunkSize * client.NetworkQuality)));

        // 지연 시간 계산 
        private double CalculateDelay(Client client) => 0.1 / client.NetworkQuality;

        // 클라이언트에게 파일 전송 
        private async Task SendFilesToClientAsync(Client client, BinaryWriter writer, CancellationToken cancellationToken)
        {
            if (!Directory.Exists(FilesDir))
            {
                Log($"파일 디렉터리 존재하지 않음: {FilesDir}");
                return;
            }

            foreach (var filePath in Directory.GetFiles(FilesDir))
            {
                await SendFileToClientAsync(client, writer, filePath, cancellationToken);
            }
        }

        // 개별 파일을 클라이언트에게 전송
        private async Task SendFileToClientAsync(Client client, BinaryWriter writer, string filePath, CancellationToken cancellationToken)
        {
            Log($"클라이언트에게 파일 전송 시작: {filePath}");

            try
            {
                var fileInfo = new FileInfo(filePath);
                if (fileInfo.Length > MaxFileSize)
                {
                    Log($"파일 크기 초과: {filePath}");
                    return;
                }

                // 파일 전송 시작 
                await SendMessageAsync(writer, new
                {
                    type = "file_start",
                    content = new { filename = fileInfo.Name, filesize = fileInfo.Length }
                });

                var chunkSize = GetChunkSize(client);
                using var fileStream = File.OpenRead(filePath);
                var buffer = new byte[chunkSize];
                int bytesRead;
                while ((bytesRead = await fileStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken)) > 0)
                {
                    await SendMessageAsync(writer, new
                    {
                        type = "file_chunk",
                        content = Convert.ToBase64String(buffer, 0, bytesRead)
                    });

                    await _stats.AddTransferredBytes(bytesRead);
                    await Task.Delay(TimeSpan.FromSeconds(CalculateDelay(client)), cancellationToken);
                }

                // 파일 전송 완료 
                await SendMessageAsync(writer, new
                {
                    type = "file_end",
                    content = new { filename = fileInfo.Name }
                });

                Log($"클라이언트에게 파일 전송 완료: {filePath}");
            }
            catch (Exception ex)
            {
                Log($"파일 전송 오류: {ex.Message}");
            }
        }

        // 통계 모니터링 
        public async Task MonitorStatsAsync(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(10), cancellationToken);
                Log($"현재 연결: {_stats.ActiveConnections}, 총 전송량: {_stats.TotalTransferred} 바이트");
            }
        }
    }

    // 클라이언트 정보 
    class Client
    {
        public TcpClient TcpClient { get; }
        public DateTime LastSeen { get; set; }
        public double NetworkQuality { get; set; }

        public Client(TcpClient tcpClient)
        {
            TcpClient = tcpClient;
            LastSeen = DateTime.Now;
            NetworkQuality = 1.0; // 기본 네트워크 품질
        }
    }

    // 서버 통계 
    class Stats
    {
        private int _activeConnections;
        private long _totalTransferred;
        private readonly object _lock = new();

        public int ActiveConnections => _activeConnections;
        public long TotalTransferred => _totalTransferred;

        // 연결 수 증가 
        public Task IncrementActiveConnections()
        {
            Interlocked.Increment(ref _activeConnections);
            return Task.CompletedTask;
        }

        // 연결 수 감소 
        public Task DecrementActiveConnections()
        {
            Interlocked.Decrement(ref _activeConnections);
            return Task.CompletedTask;
        }

        // 전송된 바이트 수
        public Task AddTransferredBytes(long bytes)
        {
            Interlocked.Add(ref _totalTransferred, bytes);
            return Task.CompletedTask;
        }
    }

}
