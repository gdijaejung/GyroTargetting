//
// E2Box Serial 입력
//	- Baudrate 912600
//
//

#include "stdafx.h"
#include "../Common/Graphic/character/character.h"
#include "../Common/UIComponent/BufferedSerial.h"


using namespace graphic;
using namespace std;


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


private:
	CBufferedSerial m_serial;
	network::cUDPClient m_udpCamera;	// 타겟정보 전송
	network::cUDPClient m_udpSndData;	// debug용 정보 전송 (roll,pitch,yaw 각 (float))

	graphic::cCharacter m_cube;
	graphic::cText m_text;
	graphic::cText m_rectText[4];
	Matrix44 m_baseTm;
	Matrix44 m_qtm;


	float m_euler[3];
	float m_rect[4];
	bool m_checkRect[4];
	graphic::cText m_eulerText[2];
	int m_rcvIMUCnt;


	enum STATE {
		INIT,
		START,
	};
	STATE m_state;


	POINT m_curPos;
	bool m_LButtonDown;
	bool m_RButtonDown;
	bool m_MButtonDown;
	Matrix44 m_rotateTm;
};

INIT_FRAMEWORK(cViewer);



struct SMGCameraData
{
	float x1, y1, x2, y2; // 총이 가르키는 위치 0 ~ 1 사이 값. 화면의 왼쪽 아래가 {0,0}
	int fire1; // 플레어1 격발, 1:격발, 0:Nothing~
	int fire2; // 플레어2 격발, 1:격발, 0:Nothing~
	int reload1; // 플레어1 리로드, 1:리로드, 0:Nothing~
	int reload2; // 플레어2 리로드, 1:리로드, 0:Nothing~
	int start1; // 플레어1 스타트버튼 On/Off, 1:On, 0:Off
	int start2; // 플레어2 스타트버튼 On/Off, 1:On, 0:Off
	int credit; // 게임 플레이 할 수 있는 회수
	int coinCount; // 여분의 동전 개수
	int coinPerGame; // 한 게임당 동전 개수
};



cViewer::cViewer()
{
	m_windowName = L"E2Box Gyro Targetting";
	const RECT r = { 0, 0, 1024, 768 };
	m_windowRect = r;
	ZeroMemory(m_rect, sizeof(m_rect));
	ZeroMemory(m_checkRect, sizeof(m_checkRect));

	m_state = INIT;
	m_rcvIMUCnt = 0;

	m_LButtonDown = false;
	m_RButtonDown = false;
	m_MButtonDown = false;
}

cViewer::~cViewer()
{
}


bool cViewer::OnInit()
{
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


	//------------------------------------------------------------------------------------
	// Init Connection
	if (!m_serial.Open(5, 912600))
		return false;

	if (!m_udpSndData.Init("127.0.0.1", 8890))
	{
		cout << "Client Connect Error!! 127.0.0.1 8890 " << endl;
		return false;
	}
	m_udpSndData.m_sleepMillis = 10;

	if (!m_udpCamera.Init("127.0.0.1", 10001))
	{
		cout << "Client Connect Error!! 127.0.0.1 10001 " << endl;
		return false;
	}
	m_udpCamera.m_sleepMillis = 10;
	//------------------------------------------------------------------------------------


	m_cube.Create(m_renderer, "cube.dat");

	m_text.Create(m_renderer);
	m_text.SetPos(10, 30);
	m_text.SetColor(D3DXCOLOR(1, 1, 1, 1));
	m_text.SetText("rcv IMU = 0");

	for (int i = 0; i < 4; ++i)
	{
		m_rectText[i].Create(m_renderer);
		m_rectText[i].SetPos(10, 50 + (i * 20));
		m_rectText[i].SetColor(D3DXCOLOR(1, 1, 1, 1));
		m_rectText[i].SetText("rect = ");
	}

	m_eulerText[0].Create(m_renderer);
	m_eulerText[0].SetPos(200, 10);
	m_eulerText[0].SetColor(D3DXCOLOR(1, 1, 1, 1));
	m_eulerText[1].Create(m_renderer);
	m_eulerText[1].SetPos(200, 30);
	m_eulerText[1].SetColor(D3DXCOLOR(1, 1, 1, 1));

	return true;
}


void cViewer::OnUpdate(const float elapseT)
{
	char buff[512];
	int len = 0;
	m_serial.ReadStringUntil('\n', buff, len, sizeof(buff));
	if (len > 0)
	{
		if (len < sizeof(buff))
			buff[len] = NULL;

		vector<string> toks;
		common::tokenizer(buff, ",", "", toks);

		if (toks.size() >= 6)
		{
			static int cnt = 0;

			const int i = toks[0].find('-');
			if (string::npos == i)
				return;

			const string idStr = toks[0].substr(i + 1);
			const int id = atoi(idStr.c_str());
			if (id < 0)
				return;

			const float x = (float)atof(toks[1].c_str());
			const float y = (float)atof(toks[2].c_str());
			const float z = (float)atof(toks[3].c_str());
			const float w = (float)atof(toks[4].c_str());
			const Quaternion q(y, x, z, w);
			const Matrix44 qtm = q.GetMatrix();

			m_cube.SetTransform(qtm * m_baseTm); 
			m_qtm = qtm;

			Vector3 euler = (qtm * m_baseTm).GetQuaternion().Euler();
			m_euler[0] = euler.x;
			m_euler[1] = euler.z;
			m_euler[2] = euler.y;


			m_text.SetText(common::format("rcv IMU = %d", m_rcvIMUCnt++));
		}
	}

	if (INIT == m_state)
	{
		if (m_checkRect[0] && m_checkRect[1] && m_checkRect[2] && m_checkRect[3])
			m_state = START;
	}
	else if (START == m_state)
	{
		const float w = m_rect[2] - m_rect[0];
		const float h = m_rect[3] - m_rect[1];
		const float x = m_euler[0] - m_rect[0];
		const float y = m_euler[1] - m_rect[1];

		const float nx = x / w;
		const float ny = y / h;

		SMGCameraData sndData;
		ZeroMemory(&sndData, sizeof(sndData));
		sndData.x1 = nx;
		sndData.y1 = 1 - ny;
		m_udpCamera.SendData((char*)&sndData, sizeof(sndData));
	}

}


void cViewer::OnRender(const float elapseT)
{
	if (m_renderer.ClearScene())
	{
		m_renderer.BeginScene();

		GetMainLight().Bind(m_renderer, 0);

		m_renderer.RenderGrid();
		m_renderer.RenderAxis();

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


void cViewer::MessageProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
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
		case VK_TAB:
		{
			static bool flag = false;
			m_renderer.GetDevice()->SetRenderState(D3DRS_CULLMODE, flag ? D3DCULL_CCW : D3DCULL_NONE);
			m_renderer.GetDevice()->SetRenderState(D3DRS_FILLMODE, flag ? D3DFILL_SOLID : D3DFILL_WIREFRAME);
			flag = !flag;
		}
		break;

		case VK_SPACE:
			m_baseTm = m_qtm.Inverse();
			break;

 		case VK_LEFT: m_rect[0] = m_euler[0]; m_checkRect[0] = true;  break;
 		case VK_RIGHT: m_rect[2] = m_euler[0]; m_checkRect[1] = true; break;
 		case VK_UP: m_rect[1] = m_euler[1]; m_checkRect[2] = true; break;
 		case VK_DOWN: m_rect[3] = m_euler[1]; m_checkRect[3] = true; break;
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
