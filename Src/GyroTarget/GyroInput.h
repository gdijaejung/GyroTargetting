//
// 자이로 센서로부터 오는 입력을 처리한다.
// Naze32 프로토콜을 이용.
//
// Serial, UDP 입력 중 하나를 선택할 수 있다.
//
#pragma once


class cGyroInput
{
public:
	cGyroInput();
	virtual ~cGyroInput();

	enum INPUTTYPE {SER, UDP};

	bool Init(const INPUTTYPE type, const int serialPort, const int baudRate, 
		const int udpPort=0, const string &sndIp="", const int sndPort=0);
	void Update(const float deltaSeconds);
	void Close();
	bool IsConnect();


protected:
	void SendCommand(const unsigned char cmd);
	int RecvCommand(const unsigned char cmd, OUT unsigned char buffer[], const int maxLen);
	bool SyncIMU();


public:
	CBufferedSerial m_serial;
	network::cUDPServer m_udpSvr;		// UDP 수신용
	network::cUDPClient m_udpClient;	// UDP 송신용
	Quaternion m_rot;
	float m_euler[3];
	bool m_accelCalibrate;
	int m_imuRcvCount;


protected:
	INPUTTYPE m_type;
	int m_syncIMUState;
	float m_incTime;
};
