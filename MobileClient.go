package main

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

const (
	heartbeatInterval = 10 * time.Second
	downloadFolder    = "Downloads"
)

// SocketManager 구조체 정의
type SocketManager struct {
	conn           net.Conn
	isConnected    bool
	onReceive      func(message map[string]interface{})
	onConnect      func()
	onDisconnect   func()
	onSendComplete func(bytesSent int)
}

// 새로운 SocketManager 인스턴스 생성
func NewSocketManager() *SocketManager {
	return &SocketManager{}
}

// 서버에 연결하는 함수
func (sm *SocketManager) Connect(serverIP string, port int) error {
	var err error
	sm.conn, err = net.Dial("tcp", fmt.Sprintf("%s:%d", serverIP, port))
	if err != nil {
		return err
	}
	sm.isConnected = true

	if sm.onConnect != nil {
		sm.onConnect()
	}

	go sm.startListening()
	go sm.startHeartbeat()

	return nil
}

// 서버로부터의 메시지를 수신하는 함수
func (sm *SocketManager) startListening() {
	reader := bufio.NewReader(sm.conn)
	for sm.isConnected {
		lengthBuffer := make([]byte, 4)
		_, err := io.ReadFull(reader, lengthBuffer)
		if err != nil {
			if err == io.EOF {
				sm.Disconnect()
				return
			}
			log.Printf("메시지 길이 읽기 오류: %v", err)
			continue
		}

		messageLength := int(lengthBuffer[0])<<24 | int(lengthBuffer[1])<<16 | int(lengthBuffer[2])<<8 | int(lengthBuffer[3])
		messageBuffer := make([]byte, messageLength)
		_, err = io.ReadFull(reader, messageBuffer)
		if err != nil {
			log.Printf("메시지 읽기 오류: %v", err)
			continue
		}

		var message map[string]interface{}
		err = json.Unmarshal(messageBuffer, &message)
		if err != nil {
			log.Printf("메시지 파싱 오류: %v", err)
			continue
		}

		if sm.onReceive != nil {
			sm.onReceive(message)
		}
	}
}

// 서버에 하트비트 메시지를 주기적으로 전송하는 함수
func (sm *SocketManager) startHeartbeat() {
	for sm.isConnected {
		time.Sleep(heartbeatInterval)
		if sm.isConnected {
			heartbeatMessage := map[string]interface{}{
				"type": "heartbeat",
			}
			sm.send(heartbeatMessage)
		}
	}
}

// 메시지를 서버로 전송하는 함수
func (sm *SocketManager) send(message map[string]interface{}) {
	if !sm.isConnected {
		log.Println("서버에 연결되어 있지 않습니다")
		return
	}

	messageBytes, err := json.Marshal(message)
	if err != nil {
		log.Printf("메시지 JSON 인코딩 오류: %v", err)
		return
	}

	lengthPrefix := make([]byte, 4)
	length := len(messageBytes)
	lengthPrefix[0] = byte(length >> 24)
	lengthPrefix[1] = byte(length >> 16)
	lengthPrefix[2] = byte(length >> 8)
	lengthPrefix[3] = byte(length)

	_, err = sm.conn.Write(append(lengthPrefix, messageBytes...))
	if err != nil {
		log.Printf("메시지 전송 오류: %v", err)
		sm.Disconnect()
		return
	}

	if sm.onSendComplete != nil {
		sm.onSendComplete(len(lengthPrefix) + len(messageBytes))
	}
}

// 서버와의 연결을 끊는 함수
func (sm *SocketManager) Disconnect() {
	if !sm.isConnected {
		return
	}
	sm.isConnected = false
	sm.conn.Close()
	if sm.onDisconnect != nil {
		sm.onDisconnect()
	}
}

// 수신 메시지 리스너 설정
func (sm *SocketManager) SetOnReceiveListener(listener func(message map[string]interface{})) {
	sm.onReceive = listener
}

// 연결 성공 리스너 설정
func (sm *SocketManager) SetOnConnectListener(listener func()) {
	sm.onConnect = listener
}

// 연결 끊김 리스너 설정
func (sm *SocketManager) SetOnDisconnectListener(listener func()) {
	sm.onDisconnect = listener
}

// 메시지 전송 완료 리스너 설정
func (sm *SocketManager) SetOnSendCompleteListener(listener func(bytesSent int)) {
	sm.onSendComplete = listener
}

// 파일 전송 시작 처리 함수
func handleFileStart(message map[string]interface{}) {
	content := message["content"].(map[string]interface{})
	fileName := content["filename"].(string)
	fileSize := int64(content["filesize"].(float64))

	currentFileName = fileName
	totalFileSize = fileSize
	receivedSize = 0
	fileBuffer.Reset()
	fmt.Printf("파일 전송 시작: %s (%d bytes)\n", fileName, fileSize)
}

// 파일 청크 처리 함수
func handleFileChunk(message map[string]interface{}) {
	fileChunk := message["content"].(string)
	decodedData, _ := base64.StdEncoding.DecodeString(fileChunk)
	fileBuffer.Write(decodedData)
	receivedSize += int64(len(decodedData))
	fmt.Printf("파일 청크 수신. 진행률: %.2f%%\n", float64(receivedSize)/float64(totalFileSize)*100)
}

// 파일 전송 종료 처리 함수
func handleFileEnd(message map[string]interface{}) {
	content := message["content"].(map[string]interface{})
	fileName := content["filename"].(string)

	if currentFileName != fileName {
		fmt.Println("파일 이름 불일치. 예상:", currentFileName, "수신된:", fileName)
		return
	}

	fileData := fileBuffer.Bytes()
	err := saveFile(fileName, fileData)
	if err != nil {
		fmt.Printf("파일 저장 오류: %v\n", err)
		return
	}

	fmt.Printf("파일 전송 완료: %s\n", fileName)
}

// 파일을 저장하는 함수
func saveFile(fileName string, fileData []byte) error {
	currentDir, err := os.Getwd()
	if err != nil {
		return err
	}
	downloadsDir := filepath.Join(currentDir, downloadFolder)
	filePath := filepath.Join(downloadsDir, fileName)

	if _, err := os.Stat(downloadsDir); os.IsNotExist(err) {
		err := os.MkdirAll(downloadsDir, os.ModePerm)
		if err != nil {
			return err
		}
	}

	file, err := os.Create(filePath)
	if err != nil {
		return err
	}
	defer file.Close()

	_, err = file.Write(fileData)
	if err != nil {
		return err
	}

	return nil
}

var currentFileName string
var fileBuffer = bytes.NewBuffer(nil)
var totalFileSize int64
var receivedSize int64

func main() {
	socketManager := NewSocketManager()

	socketManager.SetOnReceiveListener(func(message map[string]interface{}) {
		switch message["type"].(string) {
		case "heartbeat_ack":
			//fmt.Println("하트비트 응답 수신")
		case "chat":
			fmt.Println("메시지 수신:", message["content"])
		case "file_start":
			handleFileStart(message)
		case "file_chunk":
			handleFileChunk(message)
		case "file_end":
			handleFileEnd(message)
		default:
			fmt.Println("알 수 없는 메시지 타입:", message["type"])
		}
	})

	socketManager.SetOnConnectListener(func() {
		fmt.Println("서버에 연결됨")
	})

	socketManager.SetOnDisconnectListener(func() {
		fmt.Println("서버와의 연결 끊김")
	})

	socketManager.SetOnSendCompleteListener(func(bytesSent int) {
		//fmt.Println("전송된 바이트 수:", bytesSent)
	})

	scanner := bufio.NewScanner(os.Stdin)
	for {
		fmt.Print("명령어 입력 (connect, sendfile, sendmessage, exit): ")
		if !scanner.Scan() {
			if err := scanner.Err(); err != nil {
				log.Println("입력 오류:", err)
			}
			continue
		}
		command := scanner.Text()

		switch command {
		case "connect":
			fmt.Print("서버 IP 입력: ")
			if !scanner.Scan() {
				log.Println("입력 오류")
				continue
			}
			serverIP := scanner.Text()

			fmt.Print("포트 입력: ")
			if !scanner.Scan() {
				log.Println("입력 오류")
				continue
			}
			port := scanner.Text()
			portInt, err := strconv.Atoi(port)
			if err != nil {
				log.Printf("포트 번호 오류: %v", err)
				continue
			}

			err = socketManager.Connect(serverIP, portInt)
			if err != nil {
				log.Printf("연결 오류: %v", err)
			}

		case "sendfile":
			if !socketManager.isConnected {
				fmt.Println("서버에 연결되어 있지 않습니다")
				continue
			}

			fileRequest := map[string]interface{}{
				"type":    "filerequest",
				"content": "all",
			}
			socketManager.send(fileRequest)

		case "sendmessage":
			fmt.Print("메시지 입력: ")
			if !scanner.Scan() {
				log.Println("입력 오류")
				continue
			}
			message := scanner.Text()

			if !socketManager.isConnected {
				fmt.Println("서버에 연결되어 있지 않습니다")
				continue
			}

			jsonMessage := map[string]interface{}{
				"type":    "chat",
				"content": message,
			}
			socketManager.send(jsonMessage)

		case "exit":
			socketManager.Disconnect()
			return

		default:
			fmt.Println("알 수 없는 명령어")
		}
	}
}
