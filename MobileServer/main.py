import asyncio
import json
import os
import base64
import aiofiles
import logging
import struct

# 설정
DEFAULT_CHUNK_SIZE = 16 * 1024  # 16KB 기본 청크 크기
MIN_CHUNK_SIZE = 4 * 1024  # 4KB 최소 청크 크기
MAX_CHUNK_SIZE = 64 * 1024  # 64KB 최대 청크 크기
FILES_DIR = './files'  # 파일 디렉토리
MAX_FILE_SIZE = 100 * 1024 * 1024  # 100MB 최대 파일 크기

# 로깅 설정
logging.basicConfig(level=logging.INFO)


class Client:
    def __init__(self, writer):
        self.writer = writer
        self.last_seen = asyncio.get_event_loop().time()
        self.network_quality = 1.0  # 초기 네트워크 품질


async def read_message(reader):
    try:
        length_data = await reader.readexactly(4)  # 메시지 길이 읽기
        length = struct.unpack('>I', length_data)[0]  # 길이를 정수로 변환
        message_data = await reader.readexactly(length)  # 메시지 데이터 읽기
        message = json.loads(message_data.decode())  # 메시지 디코딩
        return message
    except asyncio.IncompleteReadError:
        return None
    except Exception as e:
        logging.error(f'메시지 읽기 오류: {e}')
        return None


async def send_message(writer, message):
    try:
        message_data = json.dumps(message).encode()  # 메시지 인코딩
        length = len(message_data)
        writer.write(struct.pack('>I', length))  # 메시지 길이 전송
        writer.write(message_data)  # 메시지 데이터 전송
        await writer.drain()  # 데이터 전송 대기
    except Exception as e:
        logging.error(f'메시지 전송 오류: {e}')


def get_chunk_size(client):
    chunk_size = int(DEFAULT_CHUNK_SIZE * client.network_quality)  # 네트워크 품질에 따른 청크 크기 결정
    return max(MIN_CHUNK_SIZE, min(MAX_CHUNK_SIZE, chunk_size))


def calculate_delay(client):
    base_delay = 0.1  # 100ms 기본 딜레이
    return base_delay / client.network_quality


async def send_start_message(client, filename, filesize):
    message = {
        'type': 'file_start',
        'content': {'filename': filename, 'filesize': filesize}
    }
    await send_message(client.writer, message)  # 파일 전송 시작 메시지 전송


async def send_file_chunk(client, chunk):
    message = {
        'type': 'file_chunk',
        'content': base64.b64encode(chunk).decode()  # 청크 데이터를 base64 인코딩
    }
    await send_message(client.writer, message)  # 파일 청크 메시지 전송


async def send_end_message(client, file_name):
    message = {
        'type': 'file_end',
        'content': {'filename': file_name}
    }
    await send_message(client.writer, message)  # 파일 전송 완료 메시지 전송


class Server:
    def __init__(self):
        self.stats = Stats()  # 통계 객체 초기화
        self._server = None

    async def handle_client(self, reader, writer):
        client = Client(writer)
        client_id = writer.get_extra_info('peername')
        logging.info(f'클라이언트 연결: {client_id}')
        await self.stats.increment_active_connections()  # 비동기적으로 활성 연결 수 증가

        try:
            while True:
                message = await read_message(reader)
                if not message:
                    break

                if message['type'] == 'heartbeat':
                    await send_message(writer, {'type': 'heartbeat_ack'})
                elif message['type'] == 'chat':
                    logging.info(f'{client_id}로부터 메시지 받음: {message["content"]}')
                elif message['type'] == 'filerequest':
                    await self.send_files_to_client(client, message)
                elif message['type'] == 'network_quality':
                    client.network_quality = message['content']
                    logging.info(f'클라이언트 {client_id}의 네트워크 품질 업데이트: {message["content"]}')
                else:
                    logging.warning(f'알 수 없는 메시지 타입: {message["type"]}')

                client.last_seen = asyncio.get_event_loop().time()
        except Exception as e:
            logging.error(f'클라이언트 처리 중 오류 발생: {e}')
        finally:
            writer.close()
            await writer.wait_closed()
            await self.stats.decrement_active_connections()  # 비동기적으로 활성 연결 수 감소
            logging.info(f'클라이언트 연결 종료: {client_id}')

    async def send_files_to_client(self, client, message):
        files = os.listdir(FILES_DIR)  # 파일 디렉토리 내 파일 목록 가져오기
        for file_name in files:
            file_path = os.path.join(FILES_DIR, file_name)
            if os.path.isfile(file_path):
                await self.send_file_to_client(client, file_path)  # 클라이언트에게 파일 전송

    async def send_file_to_client(self, client, file_path):
        logging.info(f'클라이언트에게 파일 전송 시작: {file_path}')

        try:
            async with aiofiles.open(file_path, 'rb') as file:
                file_size = os.path.getsize(file_path)
                if file_size > MAX_FILE_SIZE:
                    logging.warning(f'파일 크기 초과: {file_path}')
                    return

                await send_start_message(client, os.path.basename(file_path), file_size)  # 파일 전송 시작 메시지

                chunk_size = get_chunk_size(client)
                total_sent = 0

                while True:
                    chunk = await file.read(chunk_size)
                    if not chunk:
                        break

                    await send_file_chunk(client, chunk)  # 파일 청크 전송
                    total_sent += len(chunk)
                    await self.stats.add_transferred_bytes(len(chunk))  # 비동기적으로 전송된 바이트 수 증가

                    logging.info(f'클라이언트에게 {total_sent}/{file_size} 바이트 전송 완료')
                    await asyncio.sleep(calculate_delay(client))  # 딜레이 계산 및 적용

                await send_end_message(client, os.path.basename(file_path))  # 파일 전송 완료 메시지
                logging.info(f'클라이언트에게 파일 전송 완료: {file_path}')
        except Exception as e:
            logging.error(f'파일 전송 오류: {e}')

    async def monitor_stats(self):
        while True:
            await asyncio.sleep(10)
            active_connections = self.stats.active_connections  # 현재 연결 수
            total_transferred = self.stats.total_transferred  # 총 전송된 바이트 수
            logging.info(f'현재 연결: {active_connections}, 총 전송량: {total_transferred} 바이트')

    async def start(self):
        self._server = await asyncio.start_server(self.handle_client, '0.0.0.0', 11011)  # 서버 시작
        logging.info('서버 시작: 0.0.0.0:11011')

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            logging.info('서버 종료 완료')


class Stats:
    def __init__(self):
        self.active_connections = 0
        self.total_transferred = 0
        self.lock = asyncio.Lock()  # 비동기 락

    async def increment_active_connections(self):
        async with self.lock:
            self.active_connections += 1

    async def decrement_active_connections(self):
        async with self.lock:
            self.active_connections -= 1

    async def add_transferred_bytes(self, bytes):
        async with self.lock:
            self.total_transferred += bytes


async def main():
    server = Server()
    asyncio.create_task(server.monitor_stats())  # 통계 모니터링 시작

    await server.start()  # 서버 시작

    try:
        await asyncio.Event().wait()  # 서버가 종료될 때까지 대기
    except KeyboardInterrupt:
        logging.info('서버 종료 요청 받음')
    finally:
        await server.stop()  # 서버 종료


if __name__ == '__main__':
    asyncio.run(main())

