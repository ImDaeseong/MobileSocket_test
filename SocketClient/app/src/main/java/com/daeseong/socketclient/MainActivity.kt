package com.daeseong.socketclient

import android.Manifest
import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.net.SocketException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private val tag = MainActivity::class.java.simpleName

    private lateinit var btn1: Button
    private lateinit var btn2: Button
    private lateinit var btn3: Button
    private lateinit var et1: EditText
    private lateinit var et2: EditText
    private lateinit var tv1: TextView


    private lateinit var socketManager: SocketManager

    private var currentFileName: String = ""
    private var fileBuffer: ByteArrayOutputStream? = null
    private var totalFileSize: Long = 0
    private var receivedSize: Long = 0
    private var sIP: String = ""


    private lateinit var requestPermissions: ActivityResultLauncher<Array<String>>
    private val PERMISSIONS = arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE)

    private fun initPermissionsLauncher() {

        requestPermissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            val allGranted = result.all { it.value }
            if (allGranted) {
                Log.e(tag, "전체 퍼미션 허용")
                sendFileRequest()
            } else {
                Log.e(tag, "퍼미션 요청 거절 상태")
            }
        }
    }

    private fun requestPermission() {
        requestPermissions.launch(PERMISSIONS)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        initPermissionsLauncher()

        btn1 = findViewById(R.id.btn1)
        btn2 = findViewById(R.id.btn2)
        btn3 = findViewById(R.id.btn3)
        et1 = findViewById(R.id.et1)
        et2 = findViewById(R.id.et2)
        tv1 = findViewById(R.id.tv1)

        //접속
        btn1.setOnClickListener {

            sIP = et2.text.toString()
            connectToServer()
        }

        //파일 다운로드
        btn2.setOnClickListener {

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {

                //Log.e(tag, "Android 10 (Q, API 레벨 29) 이상에서는 권한 요청 필요 없음")
                sendFileRequest()

            } else {

                //Log.e(tag, "Android 9 (Pie, API 레벨 28) 이하 버전에서는 외부 저장소 권한 요청")
                requestPermission()
            }

        }

        //메시지 전달
        btn3.setOnClickListener {
            sendMessage()
        }

        //저장된 서버 IP
        sIP = SharedPreferencesUtil.getValue(this, "server_IP", "")
        if(sIP.isNotEmpty()) {
            et2.setText(sIP)
        }

        //클라이언트 소켓
        socketManager = SocketManager.getInstance()
        setupSocketListeners()
        connectToServer()
    }

    override fun onDestroy() {
        super.onDestroy()

        lifecycleScope.launch {
            socketManager.close()
            log("소켓 해제")
        }
    }

    private fun setupSocketListeners() {

        with(socketManager) {

            setOnReceiveListener { message ->
                when (message.optString("type")) {
                    "heartbeat_ack" -> log("라이브 메시지")
                    "chat" -> log("받은 내용: ${message.optString("content")}")
                    "file_start" -> handleFileStart(message)
                    "file_chunk" -> handleFileChunk(message)
                    "file_end" -> handleFileEnd(message)
                    else -> log("메시지 타입 오류: ${message.optString("type")}")
                }
            }

            setOnConnectListener {
                log("서버 접속")

                //접속 성공시 IP 저장
                SharedPreferencesUtil.setValue(this@MainActivity, "server_IP", sIP)

                //네트워크 상태 서버에 전달
                startNetworkQualityMonitoring()

                //접속 버튼 상태
                //updateButtonState(isConnected = true)
            }

            setOnDisconnectListener {
                log("서버 접속 끊김")

                //접속 버튼 상태
                //updateButtonState(isConnected = false)
            }

            setOnSendCompleteListener { bytesSent ->
                log("보낸 bytes: $bytesSent")
            }
        }
    }

    private fun updateButtonState(isConnected: Boolean) {
        runOnUiThread {
            btn1.isEnabled = !isConnected
        }
    }

    private fun connectToServer() {
        lifecycleScope.launch {
            val serverIP = sIP
            val port = 11011

            try {
                log("서버 연결 시도 중...")
                withTimeout(SocketManager.CONNECTION_TIMEOUT_MS.toLong()) {
                    socketManager.connect(serverIP, port)
                }
                log("서버 접속 성공")
            } catch (e: Exception) {
                when (e) {
                    is TimeoutCancellationException -> log("서버 연결 시간 초과")
                    is SocketException -> log("서버 접속 실패: ${e.message}")
                    else -> log("서버 접속 중 오류 발생: ${e.message}")
                }
                log("서버 연결 실패.")
            }
        }
    }

    private fun sendFileRequest() {
        if (socketManager.isConnected()) {
            lifecycleScope.launch {
                try {
                    val jsonMessage = JSONObject().apply {
                        put("type", "filerequest")
                        put("content", "all")
                    }
                    socketManager.send(jsonMessage)
                    log("파일 요청")
                } catch (e: Exception) {
                    log("파일 요청 실패: ${e.message}")
                }
            }
        } else if (!socketManager.isConnected()) {
            log("서버에 연결되어 있지 않습니다.")
        }
    }

    private fun sendMessage() {
        val message = et1.text.toString()
        if (message.isNotEmpty() && socketManager.isConnected()) {
            lifecycleScope.launch {
                try {
                    val jsonMessage = JSONObject().apply {
                        put("type", "chat")
                        put("content", message)
                    }
                    socketManager.send(jsonMessage)
                    log("메시지 전달: $message")
                    et1.setText("")
                } catch (e: Exception) {
                    log("메시지 전달 실패: ${e.message}")
                }
            }
        } else if (!socketManager.isConnected()) {
            log("서버에 연결되어 있지 않습니다.")
        }
    }


    private fun handleFileStart(message: JSONObject) {
        lifecycleScope.launch {
            try {
                val content = message.getJSONObject("content")
                val fileName = content.getString("filename")
                val fileSize = content.getLong("filesize")
                log("파일 정보 - 파일명: $fileName, 파일크기: $fileSize bytes")

                currentFileName = fileName
                totalFileSize = fileSize
                receivedSize = 0
                fileBuffer = ByteArrayOutputStream()
            } catch (e: Exception) {
                log("파일 정보 오류: ${e.message}")
            }
        }
    }

    private fun handleFileChunk(message: JSONObject) {
        lifecycleScope.launch {
            try {
                val fileChunk = message.getString("content")
                val decodedData = android.util.Base64.decode(fileChunk, android.util.Base64.DEFAULT)
                fileBuffer?.write(decodedData)
                receivedSize += decodedData.size
                updateProgress()
            } catch (e: Exception) {
                log("파일 수신 오류: ${e.message}")
            }
        }
    }

    private fun handleFileEnd(message: JSONObject) {
        lifecycleScope.launch {
            try {
                val content = message.getJSONObject("content")
                val fileName = content.getString("filename")

                if (currentFileName == fileName && fileBuffer != null) {
                    val fileData = fileBuffer!!.toByteArray()
                    saveFile(fileName, fileData)
                    log("파일 수신 완료: $fileName")
                } else {
                    log("파일 수신 완료: 파일 정보가 일치하지 않음")
                }
            } catch (e: Exception) {
                log("파일 수신 완료 오류: ${e.message}")
            } finally {
                currentFileName = ""
                fileBuffer?.close()
                fileBuffer = null
                totalFileSize = 0
                receivedSize = 0
            }
        }
    }

    private fun updateProgress() {
        val progress = (receivedSize.toFloat() / totalFileSize.toFloat() * 100).toInt()
        log("다운로드 진행: $progress%")
    }

    private fun saveFile(fileName: String, fileData: ByteArray) {
        getFilePathInfo(fileName, fileData)
    }

    private fun getFilePathInfo(fileName: String, fileData: ByteArray) {

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {

            //Log.e(tag, "Android 10 (Q, API 레벨 29) 이상 버전에서는 MediaStore를 통해 다운로드 폴더에 파일 저장")

            val resolver: ContentResolver = contentResolver
            val contentValues = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
                put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
            }

            val uri: Uri? = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, contentValues)
            try {
                uri?.let {
                    val os: OutputStream? = resolver.openOutputStream(it)
                    os?.use {
                        it.write(fileData)
                        log("파일 저장 성공: $fileName")
                    }
                }
            } catch (e: IOException) {
                e.printStackTrace()
                log("파일 저장 실패: ${e.message}")
            }

        } else {

            //Log.e(tag, "Android 9 (Pie, API 레벨 28) 이하 버전에서는 직접 외부 저장소의 다운로드 폴더에 파일 저장")

            val file = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), fileName)
            try {
                FileOutputStream(file).use { fos ->
                    fos.write(fileData)
                    log("파일 저장 성공: ${file.absolutePath}")
                }
            } catch (e: IOException) {
                e.printStackTrace()
                log("파일 저장 실패: ${e.message}")
            }
        }
    }

    private fun startNetworkQualityMonitoring() {
        val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        connectivityManager.registerNetworkCallback(
            NetworkRequest.Builder().build(),
            object : ConnectivityManager.NetworkCallback() {
                override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
                    val downstreamBandwidthKbps = networkCapabilities.linkDownstreamBandwidthKbps
                    val upstreamBandwidthKbps = networkCapabilities.linkUpstreamBandwidthKbps
                    val quality = calculateNetworkQuality(downstreamBandwidthKbps, upstreamBandwidthKbps)
                    sendNetworkQualityToServer(quality)
                }
            }
        )
    }

    private fun calculateNetworkQuality(downstreamBandwidthKbps: Int, upstreamBandwidthKbps: Int): Float {
        // 네트워크 품질을 0.0 ~ 1.0 사이의 값으로 계산
        val totalBandwidth = downstreamBandwidthKbps + upstreamBandwidthKbps
        return when {
            totalBandwidth > 10000 -> 1.0f  // 10 Mbps 이상
            totalBandwidth > 5000 -> 0.75f  // 5-10 Mbps
            totalBandwidth > 2000 -> 0.5f   // 2-5 Mbps
            totalBandwidth > 1000 -> 0.25f  // 1-2 Mbps
            else -> 0.1f                    // 1 Mbps 미만
        }
    }

    private fun sendNetworkQualityToServer(quality: Float) {
        if (socketManager.isConnected()) {
            lifecycleScope.launch {
                try {
                    val jsonMessage = JSONObject().apply {
                        put("type", "network_quality")
                        put("content", quality)
                    }
                    socketManager.send(jsonMessage)
                    log("네트워크 품질 정보 전송: $quality")
                } catch (e: Exception) {
                    log("네트워크 품질 정보 전송 실패: ${e.message}")
                }
            }
        }
    }

    private fun log(message: String) {

        runOnUiThread {

            val sCurrentTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(
                Date()
            )
            tv1.append("$sCurrentTime: $message\n")


            //50줄 이상이면 초기화
            if (tv1.lineCount >= 50) {
                tv1.text = ""
            }

            //스크롤 맨 아래로 이동
            tv1.post {

                val layout = tv1.layout
                if (layout != null) {
                    val scrollAmount = layout.getLineTop(tv1.lineCount) - tv1.height
                    if (scrollAmount > 0) {
                        tv1.scrollTo(0, scrollAmount)
                    } else {
                        tv1.scrollTo(0, 0)
                    }
                }
            }

        }
    }

}