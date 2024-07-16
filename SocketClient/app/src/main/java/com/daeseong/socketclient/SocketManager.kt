package com.daeseong.socketclient

import android.util.Log
import kotlinx.coroutines.*
import org.json.JSONObject
import java.io.*
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketException
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class SocketManager private constructor() : AutoCloseable {

    private val tag = SocketManager::class.java.simpleName

    private var socket: Socket? = null
    private val isConnected = AtomicBoolean(false)
    private var onReceiveListener: ((JSONObject) -> Unit)? = null
    private var onConnectListener: (() -> Unit)? = null
    private var onDisconnectListener: (() -> Unit)? = null
    private var onSendCompleteListener: ((Int) -> Unit)? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private var networkQuality: Float = 1.0f

    companion object {
        @Volatile
        private var instance: SocketManager? = null

        fun getInstance(): SocketManager =
            instance ?: synchronized(this) {
                instance ?: SocketManager().also { instance = it }
            }

        private const val RECONNECT_DELAY_MS = 5000L
        private const val MAX_RECONNECT_ATTEMPTS = 5
        private const val HEARTBEAT_INTERVAL_MS = 10000L
        const val CONNECTION_TIMEOUT_MS = 5000 // 5초
        private const val MAX_MESSAGE_SIZE = 100 * 1024 * 1024 // 100MB를 바이트 단위로 표현
    }

    private var serverIP: String = ""
    private var port: Int = 0
    private var reconnectAttempts = 0
    private var heartbeatJob: Job? = null

    suspend fun connect(serverIP: String, port: Int) {
        if (isConnected.get()) {
            throw IllegalStateException("이미 접속중")
        }

        this@SocketManager.serverIP = serverIP
        this@SocketManager.port = port
        reconnectAttempts = 0
        connectWithRetry()
    }

    private suspend fun connectWithRetry() {
        while (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            try {
                withContext(Dispatchers.IO) {
                    socket = Socket()
                    socket?.connect(InetSocketAddress(serverIP, port), CONNECTION_TIMEOUT_MS)
                    isConnected.set(true)
                }
                onConnectListener?.invoke()
                startListening()
                startHeartbeat()
                reconnectAttempts = 0
                return
            } catch (e: Exception) {
                Log.e(tag, "접속 실패: ${e.message}", e)
                reconnectAttempts++
                delay(RECONNECT_DELAY_MS)
            }
        }
        throw SocketException("최대 재접속 시도 횟수 ($MAX_RECONNECT_ATTEMPTS) 초과하여 접속 실패")
    }

    private fun startListening() {
        scope.launch {
            try {
                while (isConnected.get()) {
                    val message = withContext(Dispatchers.IO) {
                        readMessage()
                    }

                    if (message != null) {
                        withContext(Dispatchers.Main) {
                            onReceiveListener?.invoke(message)
                        }
                    } else {
                        Log.e(tag, "메시지 null")
                        break
                    }
                }
            } catch (e: Exception) {
                Log.e(tag, "수신 중 오류 발생: ${e.message}", e)
            } finally {
                reconnect()
            }
        }
    }

    private fun startHeartbeat() {
        heartbeatJob = scope.launch {
            while (isConnected.get()) {
                delay(HEARTBEAT_INTERVAL_MS)
                try {
                    val heartbeatMessage = JSONObject().apply {
                        put("type", "heartbeat")
                    }
                    send(heartbeatMessage)
                } catch (e: Exception) {
                    Log.e(tag, "하트비트 전송 실패: ${e.message}", e)
                    reconnect()
                }
            }
        }
    }

    private suspend fun readMessage(): JSONObject? {
        return withContext(Dispatchers.IO) {
            try {
                val lengthBuffer = ByteArray(4)
                val bytesRead = socket?.getInputStream()?.read(lengthBuffer)
                if (bytesRead != 4) return@withContext null

                val messageLength = ByteBuffer.wrap(lengthBuffer).int

                if (messageLength > MAX_MESSAGE_SIZE) {
                    Log.e(tag, "메시지 사이즈 초과")
                    return@withContext null
                }

                val messageBuffer = ByteArray(messageLength)
                var totalBytesRead = 0
                while (totalBytesRead < messageLength) {
                    val read = socket?.getInputStream()?.read(messageBuffer, totalBytesRead, messageLength - totalBytesRead)
                    if (read == -1) break
                    totalBytesRead += read ?: 0
                }
                if (totalBytesRead != messageLength) return@withContext null

                JSONObject(String(messageBuffer))
            } catch (e: SocketException) {
                Log.e(tag, "소켓 오류: ${e.message}", e)
                throw e // SocketException은 재연결을 시도해야 할 예외로 두고 상위 전달
            } catch (e: Exception) {
                Log.e(tag, "메시지 읽기 실패: ${e.message}", e)
                null
            }
        }
    }

    suspend fun send(message: JSONObject) {
        if (!isConnected.get()) {
            throw IllegalStateException("연결되지 않음")
        }

        try {
            val bytesSent = withContext(Dispatchers.IO) {
                val messageBytes = message.toString().toByteArray()
                val lengthPrefix = ByteBuffer.allocate(4).putInt(messageBytes.size).array()
                val outputStream = socket?.getOutputStream()
                outputStream?.write(lengthPrefix)
                outputStream?.write(messageBytes)
                outputStream?.flush()
                4 + messageBytes.size
            }

            withContext(Dispatchers.Main) {
                onSendCompleteListener?.invoke(bytesSent)
            }
        } catch (e: Exception) {
            Log.e(tag, "메시지 전송 실패: ${e.message}", e)
            reconnect()
            throw IOException("메시지 전송 실패: ${e.message}")
        }
    }

    fun setNetworkQuality(quality: Float) {
        networkQuality = quality
    }

    private fun disconnect() {
        if (!isConnected.getAndSet(false)) return

        try {
            socket?.close()
        } catch (e: Exception) {
            Log.e(tag, "연결 종료 중 오류 발생: ${e.message}", e)
        } finally {
            heartbeatJob?.cancel()
            scope.launch(Dispatchers.Main) {
                onDisconnectListener?.invoke()
            }
        }
    }

    fun setOnReceiveListener(listener: (JSONObject) -> Unit) {
        onReceiveListener = listener
    }

    fun setOnConnectListener(listener: () -> Unit) {
        onConnectListener = listener
    }

    fun setOnDisconnectListener(listener: () -> Unit) {
        onDisconnectListener = listener
    }

    fun setOnSendCompleteListener(listener: (Int) -> Unit) {
        onSendCompleteListener = listener
    }

    fun isConnected(): Boolean = isConnected.get()

    override fun close() {
        disconnect()
        scope.cancel()
    }

    private fun reconnect() {
        disconnect()
        scope.launch {
            try {
                connectWithRetry()
            } catch (e: Exception) {
                Log.e(tag, "재접속 실패: ${e.message}", e)
            }
        }
    }
}