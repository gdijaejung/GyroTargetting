
#include "stdafx.h"
#include "gyroinput.h"
#include "BaseFlightProtocol.h"


cGyroInput::cGyroInput() 
	: m_incTime(0)
	, m_type(SER)
	, m_syncIMUState(0)
	, m_accelCalibrate(0)
	, m_imuRcvCount(0)
{
	ZeroMemory(m_euler, sizeof(m_euler));
}

cGyroInput::~cGyroInput()
{
	Close();
}


bool cGyroInput::Init(const INPUTTYPE type, const int serialPort, const int baudRate, 
	const int udpPort, const string &sndIp, const int sndPort)
{
	Close();

	switch (type)
	{
	case SER:
		if (!m_serial.Open(serialPort, baudRate))
			return false;
		break;

	case UDP:
		if (!m_udpSvr.Init(0, udpPort))
			return false;
		if (!m_udpClient.Init(sndIp, sndPort, 10))
		{
			m_udpSvr.Close();
			return 0;
		}
		m_serial.SetMaxWaitTime(20);
		break;

	default:
		break;
	}

	m_type = type;
	return true;
}


void cGyroInput::Update(const float deltaSeconds)
{
	RET(!IsConnect());

	m_incTime += deltaSeconds;
	if (m_incTime < 0.01f)
		return;
	m_incTime = 0;

	SyncIMU();
}


void cGyroInput::Close()
{
	m_udpSvr.Close();
	m_udpClient.Close();
	m_serial.Close();
}


bool cGyroInput::IsConnect()
{
	switch (m_type)
	{
	case SER: return m_serial.IsOpened()? true : false;
	case UDP: return m_udpSvr.IsConnect();
	}
	return false;
}



// Cube보드에 IMU정보를 요청하고, 응답한 정보를 토대로 IMU정보를 갱신한다.
bool cGyroInput::SyncIMU()
{
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
			const float chRoll = roll*0.1f;
			const float chPitch = pitch*0.1f;
			const float chYaw = (float)yaw;

// 			if ((abs(m_euler[0] - chRoll) > 30) ||
// 				(abs(m_euler[1] - chPitch) > 30) ||
// 				(abs(m_euler[2] - chYaw) > 30))
// 			{
// 				m_euler[0] = chRoll;
// 				m_euler[1] = chPitch;
// 				m_euler[2] = chYaw;
// 				return false; // error occur
// 			}

			m_euler[0] = chRoll;
			m_euler[1] = chPitch;
			m_euler[2] = chYaw;

			Quaternion qr, qp, qy;
			qr.Euler(Vector3(0, 0, -chRoll));
			qp.Euler(Vector3(chPitch, 0, 0));
			qy.Euler(Vector3(0, (float)yaw, 0));

			m_rot = qy * qp * qr;

 			m_imuRcvCount++;

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
void cGyroInput::SendCommand(const unsigned char cmd)
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

	switch (m_type)
	{
	case SER: m_serial.SendData((char*)packet, idx); break;
	case UDP: m_udpClient.SendData((char*)packet, idx); break;
	}
}


// 모터 정보를 받는다.
// Naze32 CLI 정보 수신
// return value : 0 정보 수신중
//						   n 수신된 정보 수
//						   -1 수신 완료, 실패
int cGyroInput::RecvCommand(const unsigned char cmd, OUT unsigned char buffer[], const int maxLen)
{
	RETV(!IsConnect(), 0);

	int rcvLen = 0;
	char rcvBuffer[512];
	switch (m_type)
	{
	case UDP: 
		rcvLen = m_udpSvr.GetRecvData(rcvBuffer, sizeof(rcvBuffer));
		if (rcvLen <= 0)
			return 0;
		break;
	}

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

		//--------------------------------------------------------
		// 입력 버퍼 처리
		switch (m_type)
		{
		case UDP:
			if ((state == 0) && (idx > 10))
				return 0;
			if (rcvLen > idx)
				c = rcvBuffer[idx++];
			else
				return 0;
			break;

		case SER:
			if (m_serial.ReadData(&c, 1) <= 0)
			{
				Sleep(1);
				++noDataCnt;
				if (noDataCnt > 100)
					return 0; // exception
				continue;
			}
			break;
		}
		//--------------------------------------------------------


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
