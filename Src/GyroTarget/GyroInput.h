//
// ���̷� �����κ��� ���� �Է��� ó���Ѵ�.
// Naze32 ���������� �̿�.
//
// Serial, UDP �Է� �� �ϳ��� ������ �� �ִ�.
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
	network::cUDPServer m_udpSvr;		// UDP ���ſ�
	network::cUDPClient m_udpClient;	// UDP �۽ſ�
	Quaternion m_rot;
	float m_euler[3];
	bool m_accelCalibrate;
	int m_imuRcvCount;


protected:
	INPUTTYPE m_type;
	int m_syncIMUState;
	float m_incTime;
};
