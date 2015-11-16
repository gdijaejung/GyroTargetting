
#include "stdafx.h"
#include "GyroTarget.h"
#include "../Common/Graphic/character/character.h"
#include "BaseFlightProtocol.h"
#include "../Common/UIComponent/BufferedSerial.h"

#include <objidl.h>
#include <gdiplus.h> 
#pragma comment( lib, "gdiplus.lib" ) 
using namespace Gdiplus;
using namespace graphic;


class cViewer : public framework::cGameMain
{
public:
	cViewer();
	virtual ~cViewer();

	virtual bool OnInit() override;
	virtual void OnUpdate(const float elapseT) override;
	virtual void OnRender(const float elapseT) override;
	virtual void OnShutdown() override;
	virtual void MessageProc(UINT message, WPARAM wParam, LPARAM lParam) override;
	bool SyncIMU();
	void SendCommand(const unsigned char cmd);
	int RecvCommand(const unsigned char cmd, OUT unsigned char buffer[], const int maxLen);


private:
	network::cUDPServer m_udpServer;
	network::cUDPClient m_udpClient;
	graphic::cCharacter m_character;
	graphic::cCharacter m_cube;
	graphic::cSphere m_sphere;
	graphic::cText m_text;
	graphic::cText m_rectText[4];
	float m_rect[4];
	float m_euler[2];
	graphic::cText m_eulerText[2];

	bool m_dbgPrint;
	bool m_displayType;
	int m_syncIMUState;
	int m_imuRcvCount;
	bool m_accelCalibrate;

	string m_filePath;
	POINT m_curPos;
	bool m_LButtonDown;
	bool m_RButtonDown;
	bool m_MButtonDown;
	Matrix44 m_rotateTm;

	// GDI plus
	ULONG_PTR m_gdiplusToken;
	GdiplusStartupInput m_gdiplusStartupInput;
};

INIT_FRAMEWORK(cViewer);


cViewer::cViewer()
{
	m_windowName = L"Gyro Targetting";
	const RECT r = { 0, 0, 1024, 768 };
	m_windowRect = r;

	m_dbgPrint = false;
	m_displayType = true;
	m_accelCalibrate = false;
	ZeroMemory(m_rect, sizeof(m_rect));

	m_LButtonDown = false;
	m_RButtonDown = false;
	m_MButtonDown = false;

	m_imuRcvCount = 0;
}

cViewer::~cViewer()
{
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
	graphic::ReleaseRenderer();
}


bool cViewer::OnInit()
{
	DragAcceptFiles(m_hWnd, TRUE);

	Gdiplus::GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, NULL);

	graphic::cResourceManager::Get()->SetMediaDirectory("./media/");

	m_renderer.GetDevice()->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
	m_renderer.GetDevice()->LightEnable(0, true);

	const int WINSIZE_X = 1024;		//초기 윈도우 가로 크기
	const int WINSIZE_Y = 768;	//초기 윈도우 세로 크기
	GetMainCamera()->Init(&m_renderer);
	GetMainCamera()->SetCamera(Vector3(30, 30, -30), Vector3(0, 0, 0), Vector3(0, 1, 0));
	GetMainCamera()->SetProjection(D3DX_PI / 4.f, (float)WINSIZE_X / (float)WINSIZE_Y, 1.f, 10000.0f);

	const Vector3 lightPos(300, 300, -300);
	GetMainLight().SetPosition(lightPos);
	GetMainLight().SetDirection(lightPos.Normal());
	GetMainLight().Bind(m_renderer, 0);
	GetMainLight().Init(cLight::LIGHT_DIRECTIONAL,
		Vector4(0.7f, 0.7f, 0.7f, 0), Vector4(0.2f, 0.2f, 0.2f, 0));

	m_udpServer.Init(0, 8888);
	m_udpServer.m_sleepMillis = 0;

	//m_udpClient.Init("127.0.0.1", 8889);
	m_udpClient.Init("192.168.0.23", 8889);
	m_udpClient.m_sleepMillis = 10;

	m_cube.Create(m_renderer, "cube.dat");

	m_text.Create(m_renderer);
	m_text.SetPos(10, 30);
	m_text.SetColor(D3DXCOLOR(1, 1, 1, 1));
	m_text.SetText("rcv IMU = 0");

	for (int i = 0; i < 4; ++i)
	{
		m_rectText[i].Create(m_renderer);
		m_rectText[i].SetPos(10, 50+(i*20));
		m_rectText[i].SetColor(D3DXCOLOR(1, 1, 1, 1));
		m_rectText[i].SetText("rect = ");
	}

	m_eulerText[0].Create(m_renderer);
	m_eulerText[0].SetPos(200, 10);
	m_eulerText[0].SetColor(D3DXCOLOR(1, 1, 1, 1));
	m_eulerText[1].Create(m_renderer);
	m_eulerText[1].SetPos(200, 30);
	m_eulerText[1].SetColor(D3DXCOLOR(1, 1, 1, 1));


	m_syncIMUState = 0;

	return true;
}


void cViewer::OnUpdate(const float elapseT)
{
	//m_character.Move(elapseT);

// 	char buff[512];
// 	const int len = m_udpServer.GetRecvData(buff, sizeof(buff));
// 	if (len > 0)
// 	{
// 		if (len < sizeof(buff))
// 			buff[len] = NULL;
// 
// 	}

	RET(!m_udpServer.IsConnect());

	static float incT = 0;
	incT += elapseT;
	if (incT < 0.03f)
		return;
	incT = 0;
	
	SyncIMU();
}


void cViewer::OnRender(const float elapseT)
{
	if (m_renderer.ClearScene())
	{
		m_renderer.BeginScene();

		GetMainLight().Bind(m_renderer, 0);

		m_renderer.RenderGrid();
		m_renderer.RenderAxis();

		//m_character.Render(m_renderer, Matrix44::Identity);
		m_cube.Render(m_renderer, Matrix44::Identity);

		m_renderer.RenderFPS();
		m_text.Render();

		for (int i = 0; i < 4; ++i)
		{
			m_rectText[i].SetText(common::format("%f", m_rect[i]));
			m_rectText[i].Render();
		}

		m_eulerText[0].SetText(common::format("roll = %f", m_euler[0]));
		m_eulerText[0].Render();
		m_eulerText[1].SetText(common::format("pitch = %f", m_euler[1]));
		m_eulerText[1].Render(); 

		m_renderer.EndScene();
		m_renderer.Present();
	}
}


void cViewer::OnShutdown()
{

}


// Cube보드에 IMU정보를 요청하고, 응답한 정보를 토대로 IMU정보를 갱신한다.
bool cViewer::SyncIMU()
{
	//RETV(!m_isStart, false);
	RETV(!m_udpServer.IsConnect(), false);
	RETV(!m_udpClient.IsConnect(), false);

	if (m_syncIMUState == 0)
	{
		SendCommand(MSP_ATTITUDE);
		m_syncIMUState = 1;
	}
	else if (m_syncIMUState == 1)
	{
		m_syncIMUState = 0;
		BYTE buffer[64];
 		const int len = RecvCommand(MSP_ATTITUDE, buffer, sizeof(buffer));
 		if (len > 0)
 		{
			// 자세정보 업데이트
			int roll = *(short*)&buffer[0]; // +- 1800
			int pitch = *(short*)&buffer[2]; // +- 860
			int yaw = *(short*)&buffer[4]; // 0 ~ 360
			//common::dbg::Print("%d %d %d", roll, pitch, yaw);
			m_euler[0] = roll*0.1f;
			m_euler[1] = pitch*0.1f;

			Quaternion qr, qp, qy;
			qr.Euler(Vector3(0, 0, -roll*0.1f));
			qp.Euler(Vector3(pitch*0.1f, 0, 0));
			qy.Euler(Vector3(0, (float)yaw, 0));

			Quaternion q = qy * qp * qr;
			m_cube.SetTransform(q.GetMatrix());

			m_imuRcvCount++;
			m_text.SetText(common::format("rcv IMU = %d", m_imuRcvCount));
// 
// 			if (m_resetHead)
// 				m_syncIMUState = 2;
 			if (m_accelCalibrate)
 				m_syncIMUState = 3;

// 			m_errorCount = 0;
 			return true;
		}
		else
		{
 			// 정보를 못받으면, 연결을 끊는다.
// 			++m_errorCount;
// 			if (m_errorCount > 5)
// 				Stop();
 		}
	}
	else if (m_syncIMUState == 2)
	{
		SendCommand(MSP_CUBE_RESETHEAD);
//		m_resetHead = false;
		m_syncIMUState = 0;
	}
	else if (m_syncIMUState == 3)
	{
		SendCommand(MSP_ACC_CALIBRATION);
		m_accelCalibrate = false;
		m_syncIMUState = 0;
	}

	return false;
}


// Naze32 CLI 명령 전송
void cViewer::SendCommand(const unsigned char cmd)
{
	unsigned char packet[64];
	int checksum = 0;
	int idx = 0;
	packet[idx++] = '$';
	packet[idx++] = 'M';
	packet[idx++] = '<';
	packet[idx++] = 0;
	checksum ^= 0;
	packet[idx++] = cmd;
	checksum ^= cmd;
	packet[idx++] = checksum;
	m_udpClient.SendData((char*)packet, idx);
}


// 모터 정보를 받는다.
// Naze32 CLI 정보 수신
// return value : 0 정보 수신중
//						   n 수신된 정보 수
//						   -1 수신 완료, 실패
int cViewer::RecvCommand(const unsigned char cmd, OUT unsigned char buffer[], const int maxLen)
{
	if (!m_udpServer.IsConnect())
 		return 0;

 	char rcvBuffer[512];
	const int rcvLen = m_udpServer.GetRecvData(rcvBuffer, sizeof(rcvBuffer));
	if (rcvLen <= 0)
		return 0;

	int state = 0;
	int len = 0;
	int readLen = 0;
	int msp = 0;
	int noDataCnt = 0;
	int checkSum = 0;
	int idx = 0;
	while (1)
	{
		unsigned char c;
		if (rcvLen > idx)
		{
			c = rcvBuffer[idx++];
		}
		else
		{
			break;
		}

		//if (serial.ReadData(&c, 1) <= 0)
// 		{
// 			Sleep(1);
// 			++noDataCnt;
// 			if (noDataCnt > 100)
// 				break; // exception
// 			continue;
// 		}

		switch (state)
		{
		case 0:
		{
			state = (c == '$') ? 1 : 0;
			//cout << c;
		}
		break;

		case 1:
		{
			state = (c == 'M') ? 2 : 0;
			//cout << c;
		}
		break;

		case 2:
		{
			state = (c == '>') ? 3 : 0;
			//cout << c;
		}
		break;

		case 3:
		{
			len = c;
			//cout << (int)c;
			checkSum ^= c;
			state = 4;
		}
		break;

		case 4:
		{
			msp = c;
			//cout << (int)c << " ";
			checkSum ^= c;
			state = 5;
		}
		break;

		case 5:
		{
			if (len > readLen)
			{
				checkSum ^= c;
				if (readLen < maxLen)
					buffer[readLen] = c;

				//cout << (int)c << " ";
			}
			else
			{
				if (checkSum == c)
				{
					if (msp != cmd)
						return 0;
					return readLen; // end;
				}
				else
				{
					if (msp != cmd)
						return 0;
					return -1; // end;
				}
			}

			++readLen;
		}
		break;

		default:
			break;
		}
	}

	return 0;
}


void cViewer::MessageProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DROPFILES:
	{
		HDROP hdrop = (HDROP)wParam;
		char filePath[MAX_PATH];
		const UINT size = DragQueryFileA(hdrop, 0, filePath, MAX_PATH);
		if (size == 0)
			return;// handle error...

		m_filePath = filePath;

		const graphic::RESOURCE_TYPE::TYPE type = graphic::cResourceManager::Get()->GetFileKind(filePath);
		switch (type)
		{
		case graphic::RESOURCE_TYPE::MESH:
			break;

		case graphic::RESOURCE_TYPE::ANIMATION:
			break;
		}
	}
	break;

	case WM_MOUSEWHEEL:
	{
		int fwKeys = GET_KEYSTATE_WPARAM(wParam);
		int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		dbg::Print("%d %d", fwKeys, zDelta);

		const float len = graphic::GetMainCamera()->GetDistance();
		float zoomLen = (len > 100) ? 50 : (len / 4.f);
		if (fwKeys & 0x4)
			zoomLen = zoomLen / 10.f;

		graphic::GetMainCamera()->Zoom((zDelta < 0) ? -zoomLen : zoomLen);
	}
	break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_F5: // Refresh
		{
			if (m_filePath.empty())
				return;
		}
		break;
		case VK_BACK:
			// 회전 행렬 초기화.
			m_dbgPrint = !m_dbgPrint;
			break;
		case VK_TAB:
		{
			static bool flag = false;
			m_renderer.GetDevice()->SetRenderState(D3DRS_CULLMODE, flag ? D3DCULL_CCW : D3DCULL_NONE);
			m_renderer.GetDevice()->SetRenderState(D3DRS_FILLMODE, flag ? D3DFILL_SOLID : D3DFILL_WIREFRAME);
			flag = !flag;
		}
		break;

		case VK_SPACE:
			m_accelCalibrate = true;
			break;

		case VK_LEFT: m_rect[0] = m_euler[0]; break;
		case VK_RIGHT: m_rect[2] = m_euler[0]; break;
		case VK_UP: m_rect[1] = m_euler[1]; break;
		case VK_DOWN: m_rect[3] = m_euler[1]; break;

		case VK_RETURN:
			m_displayType = !m_displayType;
			break;

		}
		break;

	case WM_LBUTTONDOWN:
	{
		m_LButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
	}
	break;

	case WM_LBUTTONUP:
		m_LButtonDown = false;
		break;

	case WM_RBUTTONDOWN:
	{
		m_RButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
	}
	break;

	case WM_RBUTTONUP:
		m_RButtonDown = false;
		break;

	case WM_MBUTTONDOWN:
		m_MButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
		break;

	case WM_MBUTTONUP:
		m_MButtonDown = false;
		break;

	case WM_MOUSEMOVE:
	{
		if (m_LButtonDown)
		{
			POINT pos = { LOWORD(lParam), HIWORD(lParam) };
			const int x = pos.x - m_curPos.x;
			const int y = pos.y - m_curPos.y;
			m_curPos = pos;

			Quaternion q1(graphic::GetMainCamera()->GetRight(), -y * 0.01f);
			Quaternion q2(graphic::GetMainCamera()->GetUpVector(), -x * 0.01f);

			m_rotateTm *= (q2.GetMatrix() * q1.GetMatrix());
		}
		else if (m_RButtonDown)
		{
			POINT pos = { LOWORD(lParam), HIWORD(lParam) };
			const int x = pos.x - m_curPos.x;
			const int y = pos.y - m_curPos.y;
			m_curPos = pos;

			//if (GetAsyncKeyState('C'))
			{
				graphic::GetMainCamera()->Yaw2(x * 0.005f);
				graphic::GetMainCamera()->Pitch2(y * 0.005f);
			}
		}
		else if (m_MButtonDown)
		{
			const POINT point = { LOWORD(lParam), HIWORD(lParam) };
			const POINT pos = { point.x - m_curPos.x, point.y - m_curPos.y };
			m_curPos = point;

			const float len = graphic::GetMainCamera()->GetDistance();
			graphic::GetMainCamera()->MoveRight(-pos.x * len * 0.001f);
			graphic::GetMainCamera()->MoveUp(pos.y * len * 0.001f);
		}
		else
		{
		}

	}
	break;
	}
}
