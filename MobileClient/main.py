import asyncio
import json
import socket
import struct
import base64
import os
from collections import defaultdict

# 상수 정의
DEFAULT_CHUNK_SIZE = 16 * 1024  # 16KB
MIN_CHUNK_SIZE = 4 * 1024  # 4KB
MAX_CHUNK_SIZE = 64 * 1024  # 64KB
MAX_FILE_SIZE = 100 * 1024 * 1024  # 100MB


class SocketManager:
    CONNECTION_TIMEOUT_MS = 5  # 연결 타임아웃 (초)

    def __init__(self):
        self.socket = None
        self.is_connected = False
        self.on_receive_listener = None
        self.on_connect_listener = None
        self.on_disconnect_listener = None
        self.on_send_complete_listener = None

    async def connect(self, server_ip, port):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            await asyncio.wait_for(asyncio.get_event_loop().sock_connect(self.socket, (server_ip, port)),
                                   timeout=self.CONNECTION_TIMEOUT_MS)
            self.is_connected = True
            if self.on_connect_listener:
                self.on_connect_listener()
            print(f"서버에 연결됨: {server_ip}:{port}")
        except Exception as e:
            print(f"연결 오류: {e}")
            self.is_connected = False

    async def disconnect(self):
        if self.socket:
            self.socket.close()
        self.is_connected = False
        if self.on_disconnect_listener:
            self.on_disconnect_listener()
        print("서버와의 연결이 끊어졌습니다.")

    async def listen(self):
        while self.is_connected:
            try:
                print("메시지 수신 대기 중...")
                data = await asyncio.get_event_loop().sock_recv(self.socket, 4)
                if not data:
                    print("연결이 종료되었습니다.")
                    break
                length = struct.unpack('>I', data)[0]
                # print(f"수신된 메시지 길이: {length}")
                data = await asyncio.get_event_loop().sock_recv(self.socket, length)
                message = data.decode('utf-8')
                # print(f"수신된 원시 메시지: {message}")
                json_message = json.loads(message)
                if self.on_receive_listener:
                    await self.on_receive_listener(json_message)
            except json.JSONDecodeError as e:
                print(f"JSON 디코딩 오류: {e}")
                print(f"문제의 메시지: {message}")
            except Exception as e:
                print(f"수신 오류: {e}")
                break
        await self.disconnect()

    async def send_message(self, message):
        if not self.is_connected:
            raise Exception("서버에 연결되어 있지 않음")
        message_bytes = message.encode('utf-8')
        length_prefix = struct.pack('>I', len(message_bytes))
        await asyncio.get_event_loop().sock_sendall(self.socket, length_prefix + message_bytes)
        if self.on_send_complete_listener:
            self.on_send_complete_listener(len(length_prefix) + len(message_bytes))
        print(f"메시지 전송 완료: {message}")

    # 리스너 설정 메서드들
    def set_on_receive_listener(self, listener):
        self.on_receive_listener = listener

    def set_on_connect_listener(self, listener):
        self.on_connect_listener = listener

    def set_on_disconnect_listener(self, listener):
        self.on_disconnect_listener = listener

    def set_on_send_complete_listener(self, listener):
        self.on_send_complete_listener = listener


class FileManager:
    def __init__(self):
        self.file_buffers = defaultdict(lambda: {"file": None, "total_size": 0, "received_size": 0})

    async def handle_file_start(self, message):
        # print("파일 시작 처리 함수 시작")
        # print(f"받은 메시지: {message}")

        content = message.get("content", {})
        file_name = content.get("filename")
        file_size = content.get("filesize")

        if not file_name or not file_size:
            print(f"오류: 파일 이름 또는 크기 정보가 없습니다. 메시지: {message}")
            return

        if file_size > MAX_FILE_SIZE:
            print(f"오류: 파일 크기가 제한을 초과했습니다. 파일명: {file_name}, 파일크기: {file_size}")
            return

        print(f"파일 시작 - 파일명: {file_name}, 파일크기: {file_size} 바이트")

        download_folder = os.path.join(os.getcwd(), "Download")
        os.makedirs(download_folder, exist_ok=True)

        file_path = os.path.join(download_folder, file_name)
        # print(f"파일 저장 경로: {file_path}")
        self.file_buffers[file_name]["file"] = open(file_path, "wb")
        self.file_buffers[file_name]["total_size"] = file_size
        self.file_buffers[file_name]["received_size"] = 0
        print(f"파일 다운로드 준비 완료: {file_name}")

    async def handle_file_chunk(self, message):
        file_name = message.get("filename")
        file_chunk = message.get("content")

        if not file_name or not file_chunk:
            print(f"오류: 파일 이름 또는 청크 데이터가 없습니다. 메시지: {message}")
            return

        decoded_data = base64.b64decode(file_chunk)

        buffer = self.file_buffers[file_name]
        if buffer["file"] is None:
            print(f"오류: {file_name}에 대한 파일 버퍼가 초기화되지 않았습니다.")
            return

        buffer["file"].write(decoded_data)
        buffer["received_size"] += len(decoded_data)
        self.update_progress(file_name)

    async def handle_file_end(self, message):
        content = message.get("content", {})
        file_name = content.get("filename")

        if not file_name:
            print(f"오류: 파일 이름 정보가 없습니다. 메시지: {message}")
            return

        buffer = self.file_buffers[file_name]

        if buffer["file"]:
            buffer["file"].close()
            print(f"파일 수신 완료: {file_name}")
        else:
            print(f"오류: {file_name}에 대한 파일 버퍼가 없습니다.")

        del self.file_buffers[file_name]

    def update_progress(self, file_name):
        buffer = self.file_buffers[file_name]
        progress = int((buffer["received_size"] / buffer["total_size"]) * 100)
        print(f"{file_name} 다운로드 진행: {progress}%")


async def handle_user_input(socket_manager, file_manager):
    while True:
        command = await asyncio.get_event_loop().run_in_executor(None, input, "커맨드 입력 (1: 파일 요청, 2: 메시지 전송, q: 종료): ")
        command = command.strip()

        if command == '1':
            await send_file_request(socket_manager)
        elif command == '2':
            message = await asyncio.get_event_loop().run_in_executor(None, input, "전송할 메시지 입력: ")
            await socket_manager.send_message(json.dumps({"type": "chat", "content": message.strip()}))
        elif command == 'q':
            await socket_manager.disconnect()
            break
        else:
            print("잘못된 커맨드입니다.")


async def send_file_request(socket_manager):
    request_message = json.dumps({"type": "filerequest", "content": "all"})
    await socket_manager.send_message(request_message)


async def main():
    socket_manager = SocketManager()
    file_manager = FileManager()

    async def on_receive(message):
        try:
            # print(f"수신된 메시지: {message}")
            msg_type = message.get("type")
            if msg_type is None:
                print("오류: 메시지에 'type' 필드가 없습니다.")
                return

            # print(f"수신된 메시지 타입: {msg_type}")
            if msg_type == "heartbeat_ack":
                print("라이브 메시지")
            elif msg_type == "chat":
                print(f"받은 내용: {message.get('content')}")
            elif msg_type == "file_start":
                print("파일 시작 메시지 수신됨. 내용:", message)
                await file_manager.handle_file_start(message)
            elif msg_type == "file_chunk":
                await file_manager.handle_file_chunk(message)
            elif msg_type == "file_end":
                await file_manager.handle_file_end(message)
            else:
                print(f"알 수 없는 메시지 타입: {msg_type}")
        except Exception as e:
            print(f"메시지 처리 중 오류 발생: {e}")

    socket_manager.set_on_receive_listener(on_receive)
    socket_manager.set_on_connect_listener(lambda: print("서버에 연결됨"))
    socket_manager.set_on_disconnect_listener(lambda: print("서버와의 연결 종료됨"))
    socket_manager.set_on_send_complete_listener(lambda length: print(f"전송 완료: {length} 바이트"))

    await socket_manager.connect('127.0.0.1', 51111)

    # 사용자 입력 처리와 메시지 수신을 동시에 실행
    await asyncio.gather(
        handle_user_input(socket_manager, file_manager),
        socket_manager.listen()
    )


if __name__ == "__main__":
    asyncio.run(main())
