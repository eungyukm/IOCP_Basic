// ChattingServer.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "../Chat.h"
#include <process.h>
#include <list>
#include <mutex>

const int maxWorkingThread = 8;
unsigned int __stdcall ChattingPacketProcess(void *arg);
const int maxBuffer = 1024;
class ClientInfo : public WSAOVERLAPPED
{
public:
	// 모든 클라이언트의 정보를 관리해야 하므로
	// 각 클라이언트를 구분할 정보(핸들)가 필요
	static int UserHandle;
	int userHandle;	// 이 유저핸들은 모두가 달라야 한다.

	WSABUF dataBuffer;	// IOCP에서 패킷을 채울 메모리
	SOCKET socket;
	char messgaeBuffer[maxBuffer];
	unsigned long recvByte;	// 읽은 패킷 길이
	unsigned long flag;	// WSARecv의 작동 조정

	ClientInfo();
	void Reset();
};


int ClientInfo::UserHandle = 0;
ClientInfo::ClientInfo()
{
	userHandle = UserHandle;
	++UserHandle;
	// 들어온 순서에 따라 각자 다른 핸들값을 가지게 된다.
	Reset();
}

void ClientInfo::Reset()
{
	// 내가 가지고 있는 메모리 모두 지운다.
	// 하지만 유저핸들은 지워져서는 안되므로
	int myHandle = userHandle;
	SOCKET sock = socket;
	memset(this, 0, sizeof(*this));
	userHandle = myHandle;
	socket = sock;
	// userHandle 과 socket은 다른 곳에 저장했다가 다시 세팅한다.

	dataBuffer.buf = messgaeBuffer;
	dataBuffer.len = maxBuffer;
	flag = 0;
	recvByte = 0;
}

// 채팅서버는 한사람의 채팅을 모든사람에게 전달해야 하므로
// 클라이언트 정보(clientInfo)를 모아서 관리해야 한다.
list<ClientInfo *> clientList;
mutex mutexList;
int main()
{
	// 1. 소켓 초기화
	WSADATA wsaData;
	int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (startupResult != 0)
	{
		cout << "소켓 초기화 실패" << endl;
		return 1;
	}

	// 2. 소켓 생성
	SOCKET listenSocket = WSASocket(
		AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED
	);

	if (listenSocket == INVALID_SOCKET)
	{
		cout << "listenSocket 생성 실패" << endl;
		WSACleanup();
		return 1;
	}
	// 소켓 정보 구조체
	SOCKADDR_IN sockInfo;
	memset(&sockInfo, 0, sizeof(sockInfo));
	sockInfo.sin_family = PF_INET;
	sockInfo.sin_port = serverPort;
	sockInfo.sin_addr.S_un.S_addr
		= htonl(INADDR_ANY);

	// 3. 바인드
	int bindResult = ::bind(listenSocket, (SOCKADDR *)&sockInfo,
		sizeof(sockInfo));
	if (bindResult == SOCKET_ERROR)
	{
		cout << "listenSocket 바인드 에러" << endl;
		WSACleanup();
	}

	// 4. 리슨
	int listenResult = listen(listenSocket, 5);
	if (listenResult == SOCKET_ERROR)
	{
		cout << "listen() 호출 실패" << endl;
		WSACleanup();
		return 1;
	}

	// 5. IOCP 객체 생성
	HANDLE iocpHandle =
		CreateIoCompletionPort(
			INVALID_HANDLE_VALUE, // 최초 만들때 소켓이 없으므로
			nullptr, // 역시 최초에 만들때 IOCP 핸들이 없으므로
			0, // 최초로 만듦으로
			maxWorkingThread);	// 최대 워킹스레드 갯수
		// IOCP 객체를 새로 만들었음.

	if (iocpHandle == INVALID_HANDLE_VALUE)
	{
		cout << "iocp 객체 생성 실패" << endl;
		WSACleanup();
		return 1;
	}

	// 6. IOCP용 Working Thread 생성
	for (int cnt = 0; cnt < maxWorkingThread; ++cnt)
	{
		_beginthreadex(
			nullptr, // 보안 코드
			0,	// 스텍 크기, 0이면 기본크기로 세팅
			ChattingPacketProcess,// 함수 이름
			&iocpHandle, // 스레드로 보내는 iocp 핸들을 전달한다.
			0, // 스레드의 초기 상태 - 스레드 생성과 함께 실행
			nullptr);	// 스레드 ID를 가져올 필요가 없다.
	}

	while (true)
	{
		// 7. 클라이언트에서의 접속 확인
		SOCKADDR_IN clientInfo;
		int size = sizeof(clientInfo);
		SOCKET sockToCli = // 클라이언트로의 소켓
			accept(listenSocket, // 리슨 소켓 정보에서 받아온다.
			(SOCKADDR *)&clientInfo, // 클라이언트 정보
				&size);	// 구조체 크기

		if (sockToCli == INVALID_SOCKET)
		{
			cout << "소켓 억셉트 실패" << endl;
			// 여기서는 이미 많은 사람들이 채팅을
			// 하고 있는 상황. 그러므로 여기서 서버를
			// 종료해 버린다면 큰 문제.
			// 잘못된 소켓은 버리고 반복문 다시 실행
			continue;
		}
		// 소켓 확인
		cout << "클라이언트 접속 확인" << endl;

		//8. 접속된 소켓을 IOCP에 등록
		ClientInfo *cliInfo = new ClientInfo();
		// 새로 접속한 클라이언트의 정보
		cliInfo->socket = sockToCli;

		// 여기서 만든 소켓 정보를 리스트에 저장한다.
		mutexList.lock();
		clientList.push_back(cliInfo);
		mutexList.unlock();


		iocpHandle =
			CreateIoCompletionPort(
			(HANDLE)sockToCli, // 등록할 소켓
				iocpHandle,	// 이전에 만들어 놓은 핸들
				(ULONG_PTR)cliInfo, // 입력을 구분하기 위한 키값
				maxWorkingThread
			);

		// 9. WorkingThread와 연결시키기 위해서
		// 패킷 접수가 끝났을 경우 워킹스레드를 실행시키기 위한 함수
		int recveResult =
			WSARecv(cliInfo->socket,
				&cliInfo->dataBuffer,
				1,
				&cliInfo->recvByte,
				&cliInfo->flag,
				cliInfo,	// 이 객체 정보가 워킹 스레드로 전달
				nullptr);

		if (recveResult != 0) // 뭔가 에러
		{
			// pending은 데이터 송수신 중이므로 에러가 아님
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				cout << "Pending Error" << endl;
				continue;
			}
		}
	}
	return 0;
}

// 채팅 패킷을 조작하는 스레드
unsigned int __stdcall ChattingPacketProcess(void *arg)
{
	// arg : iocp 핸들의 포인터
	HANDLE *hndPnt = (HANDLE *)arg;
	HANDLE iocpHandle = *hndPnt;

	// 입출력 처리 큐로 들어가기 위한 변수들
	unsigned long recvBytes;
	ULONG_PTR completionKey;
	WSAOVERLAPPED *cliInfo;
	unsigned long flag;

	// 스레드는 무한 루프 필요
	while (true)
	{
		// 스레드는 일단 GetQuedCompletionStatus()
		// 함수로 들어가 대기상태로 바뀐다.
		// 어느 소켓에서든지 패킷 수신이 완료되면
		// 대기중인 스레드 하나를 깨우고 소켓정보를
		// GetQueuedCompletionStatus()의 인수로 전달한다.
		BOOL result =
			GetQueuedCompletionStatus(
				iocpHandle, // iocpHandle
				&recvBytes, // 받은 패킷 길이
				&completionKey, //
				&cliInfo,	// WSARecv에서 보내는 소켓 관련 객체
				INFINITE);	// 무한대기

		ClientInfo *clientInfo = (ClientInfo *)cliInfo;
		if (result == FALSE) //클라이언트 접속 종료
		{ // 열려있는 
			cout << "접속 종료" << endl;
			// 소켓 닫기
			closesocket(clientInfo->socket);

			// 리스트에서 제거
			mutexList.lock();
			clientList.remove_if(
				[clientInfo](ClientInfo *client)->bool
			{
				return client->userHandle == clientInfo->userHandle;
			});
			mutexList.unlock();

			delete clientInfo;
			continue;
		}
		// 제대로 된 패킷이 들어왔다.
		cout << "받은 패킷 " << recvBytes << "bytes" << endl;

		// 받은 패킷을 clientInfo->messgaeBuffer에 저장
		// 이 내용을 모든 클라이언트에게 전송
		mutexList.lock();
		for (list<ClientInfo *>::iterator client = clientList.begin();
			client != clientList.end(); ++client)
		{
			// client는 현재 접속해 있는 전체 클라이언트들의 정보
			// client의 소켓 (*client)->socket 이므로
			// 이 소켓으로 패킷내용을 전달하면 끝
			send((*client)->socket, clientInfo->messgaeBuffer,
				strlen(clientInfo->messgaeBuffer) + 1, 0);
		}
		mutexList.unlock();

		// 패킷 처리가 완료되었으므로 다음 패킷을 처리하기
		// 위해서는 다시 WSARecv를 실행 해야 한다.
		// 그 전에 clientInfo를 초기화 해야 한다.
		clientInfo->Reset();
		int recvReult =
			WSARecv(
				clientInfo->socket,
				&clientInfo->dataBuffer,
				1,
				&clientInfo->recvByte,
				&clientInfo->flag,
				clientInfo,
				nullptr);

		if (recvReult == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				cout << "pending error" << endl;
			}
		}
	}
}