package main

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"time"
)

const (
	defaultChunkSize = 16 * 1024 // 기본 청크 크기를 16KB로 줄임
	minChunkSize     = 4 * 1024  // 최소 청크 크기 4KB
	maxChunkSize     = 64 * 1024 // 최대 청크 크기 64KB
	numWorkers       = 50
	jobQueueSize     = 1000
	filesDir         = "./files"
	maxFileSize      = 100 * 1024 * 1024 // 최대 파일 크기를 100MB로 줄임
)

type Client struct {
	conn           net.Conn
	id             string
	lastSeen       time.Time
	networkQuality float64 // 0.0 (최악) ~ 1.0 (최상)
}

type Server struct {
	clients    sync.Map
	workerPool *WorkerPool
	stats      *Stats
}

type Message struct {
	Type    string      `json:"type"`
	Content interface{} `json:"content"`
}

type Job struct {
	clientID string
	message  Message
}

type WorkerPool struct {
	jobQueue chan Job
	wg       sync.WaitGroup
}

type Stats struct {
	activeConnections int64
	totalTransferred  int64
	mu                sync.Mutex
}

func NewWorkerPool(numWorkers, jobQueueSize int) *WorkerPool {
	wp := &WorkerPool{
		jobQueue: make(chan Job, jobQueueSize),
	}
	wp.start(numWorkers)
	return wp
}

func (wp *WorkerPool) start(numWorkers int) {
	for i := 0; i < numWorkers; i++ {
		wp.wg.Add(1)
		go wp.worker()
	}
}

func (wp *WorkerPool) worker() {
	defer wp.wg.Done()
	for job := range wp.jobQueue {
		handleJob(job)
	}
}

func (wp *WorkerPool) submitJob(job Job) {
	wp.jobQueue <- job
}

func handleJob(job Job) {
	switch job.message.Type {
	case "filerequest":
		log.Printf("클라이언트 %s로부터 파일 요청 받음", job.clientID)
		sendFilesToClient(job.clientID)
	default:
		log.Printf("알 수 없는 작업 타입: %s", job.message.Type)
	}
}

var server *Server

func main() {
	runtime.GOMAXPROCS(runtime.NumCPU())

	server = NewServer()
	defer server.Shutdown()

	listener, err := net.Listen("tcp", ":11011")
	if err != nil {
		log.Fatalf("네트워크 오류: %v", err)
	}
	defer listener.Close()

	log.Printf("서버 시작: %s", listener.Addr().String())

	go server.monitorStats()

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("연결 수락 오류: %v", err)
			continue
		}

		go server.handleConnection(conn)
	}
}

func NewServer() *Server {
	return &Server{
		workerPool: NewWorkerPool(numWorkers, jobQueueSize),
		stats:      &Stats{},
	}
}

func (s *Server) handleConnection(conn net.Conn) {
	client := &Client{
		conn:           conn,
		id:             conn.RemoteAddr().String(),
		lastSeen:       time.Now(),
		networkQuality: 1.0, // 초기 네트워크 품질을 최상으로 설정
	}

	log.Printf("클라이언트 연결: %s", client.id)
	s.stats.incrementActiveConnections()

	defer func() {
		conn.Close()
		s.removeClient(client.id)
		s.stats.decrementActiveConnections()
		log.Printf("클라이언트 연결 종료: %s", client.id)
	}()

	s.addClient(client)

	for {
		message, err := readMessage(conn)
		if err != nil {
			if err != io.EOF {
				log.Printf("메시지 읽기 오류: %v", err)
			}
			return
		}

		switch message.Type {
		case "heartbeat":
			err = sendMessage(conn, Message{Type: "heartbeat_ack"})
		case "chat":
			log.Printf("%s로부터 메시지 받음: %v", client.id, message.Content)
		case "filerequest":
			job := Job{
				clientID: client.id,
				message:  message,
			}
			s.workerPool.submitJob(job)
		case "network_quality":
			if quality, ok := message.Content.(float64); ok {
				client.networkQuality = quality
				log.Printf("클라이언트 %s의 네트워크 품질 업데이트: %f", client.id, quality)
			}
		default:
			log.Printf("알 수 없는 메시지 타입: %s", message.Type)
		}

		if err != nil {
			log.Printf("메시지 처리 오류: %v", err)
			return
		}

		client.lastSeen = time.Now()
	}
}

func (s *Server) addClient(client *Client) {
	s.clients.Store(client.id, client)
}

func (s *Server) removeClient(id string) {
	s.clients.Delete(id)
}

func readMessage(conn net.Conn) (Message, error) {
	lengthBuf := make([]byte, 4)
	_, err := io.ReadFull(conn, lengthBuf)
	if err != nil {
		return Message{}, err
	}

	length := int(binary.BigEndian.Uint32(lengthBuf))
	messageBuf := make([]byte, length)
	_, err = io.ReadFull(conn, messageBuf)
	if err != nil {
		return Message{}, err
	}

	var message Message
	err = json.Unmarshal(messageBuf, &message)
	if err != nil {
		return Message{}, errors.New("유효하지 않은 메시지 형식")
	}

	return message, nil
}

func sendMessage(conn net.Conn, message Message) error {
	jsonMessage, err := json.Marshal(message)
	if err != nil {
		return err
	}

	length := uint32(len(jsonMessage))
	lengthBuf := make([]byte, 4)
	binary.BigEndian.PutUint32(lengthBuf, length)

	_, err = conn.Write(lengthBuf)
	if err != nil {
		return err
	}

	_, err = conn.Write(jsonMessage)
	if err != nil {
		return err
	}

	return nil
}

func sendFilesToClient(clientID string) {
	files, err := os.ReadDir(filesDir)
	if err != nil {
		log.Printf("파일 디렉토리 읽기 오류: %v", err)
		return
	}

	for _, file := range files {
		if !file.IsDir() {
			filePath := filepath.Join(filesDir, file.Name())
			sendFileToClient(clientID, filePath)
		}
	}
}

func sendFileToClient(clientID, filePath string) {
	log.Printf("클라이언트 %s에게 파일 전송 시작: %s", clientID, filePath)

	file, err := os.Open(filePath)
	if err != nil {
		log.Printf("파일 열기 오류: %v", err)
		return
	}
	defer file.Close()

	fileInfo, err := file.Stat()
	if err != nil {
		log.Printf("파일 정보 가져오기 오류: %v", err)
		return
	}

	if fileInfo.Size() > maxFileSize {
		log.Printf("파일 크기 초과: %s", filePath)
		return
	}

	sendStartMessage(clientID, fileInfo.Name(), fileInfo.Size())

	chunkSize := getChunkSize(clientID)
	buf := make([]byte, chunkSize)
	totalSent := int64(0)
	for {
		n, err := file.Read(buf)
		if err != nil && err != io.EOF {
			log.Printf("파일 읽기 오류: %v", err)
			return
		}
		if n == 0 {
			break
		}

		err = sendFileChunk(clientID, buf[:n])
		if err != nil {
			log.Printf("청크 전송 오류: %v", err)
			return
		}
		totalSent += int64(n)
		server.stats.addTransferredBytes(int64(n))

		log.Printf("클라이언트 %s에게 %d/%d 바이트 전송 완료", clientID, totalSent, fileInfo.Size())

		// 네트워크 상태에 따라 전송 속도 조절
		time.Sleep(calculateDelay(clientID))
	}

	sendEndMessage(clientID, fileInfo.Name())
	log.Printf("클라이언트 %s에게 파일 전송 완료: %s", clientID, filePath)
}

func getChunkSize(clientID string) int {
	clientInterface, ok := server.clients.Load(clientID)
	if !ok {
		return defaultChunkSize
	}
	client, ok := clientInterface.(*Client)
	if !ok {
		return defaultChunkSize
	}

	// 네트워크 품질에 따라 청크 크기 조절
	chunkSize := int(float64(defaultChunkSize) * client.networkQuality)
	if chunkSize < minChunkSize {
		return minChunkSize
	}
	if chunkSize > maxChunkSize {
		return maxChunkSize
	}
	return chunkSize
}

func calculateDelay(clientID string) time.Duration {
	clientInterface, ok := server.clients.Load(clientID)
	if !ok {
		return 0
	}
	client, ok := clientInterface.(*Client)
	if !ok {
		return 0
	}

	// 네트워크 품질이 낮을수록 더 긴 지연 시간 적용
	baseDelay := time.Millisecond * 100
	return time.Duration(float64(baseDelay) / client.networkQuality)
}

func sendStartMessage(clientID, filename string, filesize int64) {
	message := Message{
		Type: "file_start",
		Content: map[string]interface{}{
			"filename": filename,
			"filesize": filesize,
		},
	}
	sendMessageToClient(clientID, message)
}

func sendFileChunk(clientID string, chunk []byte) error {
	message := Message{
		Type:    "file_chunk",
		Content: base64.StdEncoding.EncodeToString(chunk),
	}
	return sendMessageToClient(clientID, message)
}

func sendEndMessage(clientID string, fileName string) {
	message := Message{
		Type: "file_end",
		Content: map[string]interface{}{
			"filename": fileName,
		},
	}
	sendMessageToClient(clientID, message)
}

func sendMessageToClient(clientID string, message Message) error {
	clientInterface, ok := server.clients.Load(clientID)
	if !ok {
		return errors.New("클라이언트를 찾을 수 없습니다")
	}
	client, ok := clientInterface.(*Client)
	if !ok {
		return errors.New("잘못된 클라이언트 타입")
	}

	return sendMessage(client.conn, message)
}

func (s *Stats) incrementActiveConnections() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.activeConnections++
}

func (s *Stats) decrementActiveConnections() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.activeConnections--
}

func (s *Stats) addTransferredBytes(bytes int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.totalTransferred += bytes
}

func (s *Stats) getStats() (int64, int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.activeConnections, s.totalTransferred
}

func (s *Server) monitorStats() {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		activeConnections, totalTransferred := s.stats.getStats()
		log.Printf("현재 연결: %d, 총 전송량: %d 바이트", activeConnections, totalTransferred)
	}
}

func (s *Server) Shutdown() {
	log.Println("서버 종료 중...")
	s.clients.Range(func(key, value interface{}) bool {
		client := value.(*Client)
		client.conn.Close()
		return true
	})
	close(s.workerPool.jobQueue)
	s.workerPool.wg.Wait()
	log.Println("서버 종료 완료")
}
