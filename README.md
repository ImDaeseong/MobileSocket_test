## MobileSocket_test

go 서버 : files 폴더에 있는 모든 파일을 클라이언트에 전달 <br>
MobileServer 서버 : aiofiles 라이브러리를 사용한 python 비동기 서버 <br>
dotnetMobileServer 서버 : TcpListener 사용한 비동기 서버 <br>
dotnetMobileClient 클라이어트 : TcpClient 사용한 클라이언트 <br>
SocketClient 클라이언트 : Socket 사용해서 구현한 코틀린 클라이언트<br>

파이썬 프로그램 배포 방법<br>
pyinstaller --onefile main.py

go 프로그램 실행 파일 만들기<br>
E:\GoApp\src>go build -o mobileserve.exe MobileServer.go
