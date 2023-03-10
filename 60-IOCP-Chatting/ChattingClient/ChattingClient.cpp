// ChattingClient.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "../Chat.h"
#include <process.h>
#include <stdio.h>
unsigned int __stdcall ReciveThread(void *arg);

int main()
{
	// 1. winsock 초기화
	WSADATA wsaData;
	int startUpReulst =
		WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (startUpReulst != 0)
	{
		cout << "winsoc초기화 실패 " << endl;
		return 1;
	}

	// 2. 소켓 생성 서버로 접속할 소켓
	SOCKET toServer =
		WSASocket(
			AF_INET,		// 4바이트 주소
			SOCK_STREAM,	// 양방향 전송
			0,				// 프로토콜 타입
			nullptr,		// 프로토콜 정보
			0,
			WSA_FLAG_OVERLAPPED);
	if (toServer == INVALID_SOCKET)
	{
		cout << "소켓 생성 실패" << endl;
		WSACleanup();
		return 1;
	}

	// 서버로의 접속 시도
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_addr.S_un.S_addr =
		inet_addr(serverIP);	// 문자열을 주소형태로 바꾼다.
	serverAddr.sin_port = serverPort;

	int connectResult = connect(toServer, (SOCKADDR *)&serverAddr, sizeof(serverAddr));

	if (connectResult == SOCKET_ERROR)
	{
		cout << "접속 실패" << endl;
		WSACleanup();
		return 1;
	}

	cout << "접속 성공" << endl;

	// recv를 전담할 스레드 생성
	_beginthreadex(
		nullptr,	// 보안관련 정보
		0,			// 기본 스택 사이즈
		ReciveThread,
		&toServer,	// 소켓이 있어야 리시브 가능
		0,
		nullptr);


	while (true)
	{
		char buffer[1024];
		cout << "전송할 문자열 : ";
		cin >> buffer;

		int snedByte = send(toServer, buffer, strlen(buffer) + 1, 0);
		cout << "Send " << snedByte << "bytes" << endl;
	}
	return 0;
}

unsigned int __stdcall ReciveThread(void *arg)
{
	SOCKET *sockPtr = (SOCKET *)arg;
	SOCKET toServer = *sockPtr;
	char buffer[1024] = { 0 };	// 패킷을 받을 버퍼

	while (true)
	{
		recv(toServer, buffer, 1024, 0);

		cout << "받은 내용 : " << buffer << endl;
	}
	return 0;
}