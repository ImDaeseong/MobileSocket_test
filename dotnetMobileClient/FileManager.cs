using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace dotnetMobileClient
{
    public class FileManager
    {
        // 최대 파일 크기 100MB
        private const int MaxFileSize = 100 * 1024 * 1024; // 100MB

        // 파일 스트림을 저장할 딕셔너리
        private readonly Dictionary<string, FileStream> fileStreams = new Dictionary<string, FileStream>();

        // 파일 다운로드 시작 
        public async Task HandleFileStart(string jsonMessage)
        {
            try
            {
                
                var message = JsonConvert.DeserializeObject<JObject>(jsonMessage);
                var content = message["content"] as JObject;
                var fileName = content["filename"].ToString();
                var fileSize = content["filesize"].ToObject<long>();

                // 파일 크기 체크
                if (fileSize > MaxFileSize)
                {
                    Console.WriteLine($"파일 크기 초과: {fileName}");
                    return;
                }

                // 다운로드 폴더 생성 및 파일 스트림 초기화
                var downloadFolder = Path.Combine(Directory.GetCurrentDirectory(), "Download");
                Directory.CreateDirectory(downloadFolder);

                var filePath = Path.Combine(downloadFolder, fileName);
                fileStreams[fileName] = new FileStream(filePath, FileMode.Create, FileAccess.Write, FileShare.None, bufferSize: 4096, useAsync: true);

                Console.WriteLine($"파일 다운로드 시작: {fileName}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"파일 시작 처리 중 오류 발생: {ex.Message}");
            }
        }

        // 파일 다운로드 종료
        public async Task<String> HandleFileEnd(string jsonMessage)
        {
            try
            {
                var message = JsonConvert.DeserializeObject<JObject>(jsonMessage);
                var content = message["content"] as JObject;
                var fileName = content["filename"].ToString();

                // 파일 스트림이 존재하는지 확인 후 종료 처리
                if (fileStreams.TryGetValue(fileName, out var fileStream))
                {
                    await fileStream.FlushAsync();
                    await fileStream.DisposeAsync();
                    fileStreams.Remove(fileName);
                    Console.WriteLine($"파일 다운로드 완료: {fileName}");

                    return fileName; // 파일명 반환
                }
                else
                {
                    Console.WriteLine($"파일 스트림을 찾을 수 없음: {fileName}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"파일 종료 처리 중 오류 발생: {ex.Message}");
            }

            return null;
        }

        // 파일 청크 처리
        public async Task HandleFileChunk(string jsonMessage)
        {
            try
            {
                var message = JsonConvert.DeserializeObject<JObject>(jsonMessage);
                var content = message["content"]?.ToString();

                // 청크 데이터가 비어있는지 확인
                if (string.IsNullOrEmpty(content))
                {
                    Console.WriteLine("파일 청크 데이터가 비어있습니다.");
                    return;
                }

                // 현재 활성화된 파일 스트림을 찾음
                var activeFileStream = fileStreams.FirstOrDefault(kvp => kvp.Value != null).Value;
                if (activeFileStream == null)
                {
                    Console.WriteLine("활성화된 파일 스트림이 없습니다.");
                    return;
                }

                // 청크 데이터를 디코딩하여 파일에 기록
                var chunk = Convert.FromBase64String(content);
                await activeFileStream.WriteAsync(chunk, 0, chunk.Length);

                Console.WriteLine($"청크 수신: {chunk.Length} 바이트");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"파일 청크 처리 중 오류 발생: {ex.Message}");
                Console.WriteLine($"전체 메시지: {jsonMessage}");  // 디버깅을 위해 전체 메시지 출력
            }
        }
    }
}
