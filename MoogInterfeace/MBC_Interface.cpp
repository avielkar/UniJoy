// tabs=4
//************************************************************
//	COPYRIGHT 2010 Moog Inc. - ALL RIGHTS RESERVED
//
// This file is the product of Moog Inc. and cannot be 
// reproduced, copied, or used in any shape or form without 
// the express written consent of Moog Inc.
//************************************************************
//
//  Defines class that manages interface to Motion Base Computer (MBC).
//  Writes commands to MBC, receives status from MBC using
//  firewire or ethernet.
//
//	Revision History: See end of file.
//
//*************************************************************

#include "MBC_Interface.h"
#include "LogOutput.h"
#include "ConvertEnum.h"
#include <chrono>
#include <thread>
#include <iostream>


DWORD WINAPI TransmitThread(LPVOID lpThreadParameter);
DWORD WINAPI ReceiveThread(LPVOID lpThreadParameter);


// String convert tables

static TConvertEnum sg_szMachineStates[] =
{
	{ EM_STATE_POWERUP, "Power-Up" },
	{ EM_STATE_IDLE, "Ready" },
	{ EM_STATE_STANDBY, "Standby" },
	{ EM_STATE_ENGAGED, "Engaged" },
	{ EM_STATE_SETTLING, "Settling" },
	{ EM_STATE_FAULT_1, "Fault-1" },
	{ EM_STATE_FAULT_2, "Fault-2" },
	{ EM_STATE_FAULT_3, "Fault-3" },
	{ EM_STATE_DISABLED, "Disabled" },
	{ EM_STATE_INHIBITED, "Inhibited" },
	{ EM_STATE_FROZEN, "Frozen" },
	{ EM_STATE_UNKNOWN, "None" }
};

static TConvertEnum sg_szMotionStates[] =
{
	{ MOTION_MODE_MAINT, "Maint" },
	{ MOTION_MODE_DOF, "DOF" },
	{ MOTION_MODE_LENGTH, "Length" },
	{ MOTION_MODE_MDA, "MDA" },
	{ MOTION_MODE_INVALID, NULL },
};

static TConvertEnum sg_szMotionCommands[] =
{
	{ MOTION_CMD_NULL, "None" },
	{ MOTION_CMD_ENGAGE, "Engage" },
	{ MOTION_CMD_DISENGAGE, "Disengage" },
	{ MOTION_CMD_RESET, "Reset" },
	{ MOTION_CMD_START, "Start" },
	{ MOTION_CMD_DRAWBRIDGE_UP, "Drawbridge Up" },
	{ MOTION_CMD_DRAWBRIDGE_DOWN, "Drawbridge Down" },
	{ MOTION_CMD_DRAWBRIDGE_STOP, "Drawbridge Stop" },
	{ MOTION_CMD_FLIGHT_COMPARTMENT_DOOR, "Flight Compartment Door" },
	{ MOTION_CMD_NULL, NULL },
};


//---------------------------------------------------------
// Constructor.
//---------------------------------------------------------
CMBCInterface::CMBCInterface()
{
	m_bOpen = false;

	m_pCommandMsg = (MSG_HEADER_TYPE *)m_txStorageBuffer;    // pointer to command to MBC
	m_pStatusMsg = (MSG_HEADER_TYPE *)m_rxStorageBuffer;     // pointer to status from MBC

	m_hostSocket = INVALID_SOCKET;

	m_iMachineState = EM_STATE_UNKNOWN;
	m_hStatusChangeEvent = NULL;

	SetCommandMode(eCMD_MODE_LENGTH);

	m_eSigGenWave = CSignalGenerator::eWAVEFORM_NONE;
	m_eSigGenAxis = eSIGNALGEN_AXIS_NONE;

	m_iUpdateRateHz = 30;
	m_bRedundantCommands = false;

	m_hXmtThread = NULL;
	m_hRcvThread = NULL;

	memset((void*)&m_msgHeader, 0, sizeof(m_msgHeader));
	memset((void*)&m_motionCommandWord, 0, sizeof(m_motionCommandWord));
	memset((void*)&m_statusResponseWord, 0, sizeof(m_statusResponseWord));
	memset((void*)&m_lengthCmd, 0, sizeof(m_lengthCmd));
	memset((void*)&m_mdaCmd, 0, sizeof(m_mdaCmd));
	memset((void*)&m_dofCmd, 0, sizeof(m_dofCmd));

	m_eDirectDisplamentMode = eDIRECT_DISPLACEMENT_MODE_NONE;
	memset((void*)&m_DirectDisplacementDOF, 0, sizeof(m_DirectDisplacementDOF));
	memset((void*)&m_DirectDisplacementLength, 0, sizeof(m_DirectDisplacementLength));

	m_eBuffetMode = eBUFFET_MODE_NONE;
	memset((void*)&m_BuffetCmdDis, 0, sizeof(m_BuffetCmdDis));
	memset((void*)&m_BuffetCmdAcc, 0, sizeof(m_BuffetCmdAcc));

	m_eWhiteNoiseMode = eWHITENOISE_MODE_NONE;
	memset((void*)&m_WhiteNoiseCmdDis, 0, sizeof(m_WhiteNoiseCmdDis));
	memset((void*)&m_WhiteNoiseCmdAcc, 0, sizeof(m_WhiteNoiseCmdAcc));

	m_uiNumCommandsToSkip = 0;

}

//---------------------------------------------------------
// Destructor, closes the interface to the MBC.
//---------------------------------------------------------
CMBCInterface::~CMBCInterface()
{
	if (m_bOpen)
	Close();
}

//---------------------------------------------------------
// Resets the command data
//---------------------------------------------------------
void CMBCInterface::ResetCommands()
{
	memset((void*)&m_lengthCmd, 0, sizeof(m_lengthCmd));
	memset((void*)&m_mdaCmd, 0, sizeof(m_mdaCmd));
	memset((void*)&m_dofCmd, 0, sizeof(m_dofCmd));

	m_eDirectDisplamentMode = eDIRECT_DISPLACEMENT_MODE_NONE;
	memset((void*)&m_DirectDisplacementDOF, 0, sizeof(m_DirectDisplacementDOF));
	memset((void*)&m_DirectDisplacementLength, 0, sizeof(m_DirectDisplacementLength));

	m_eBuffetMode = eBUFFET_MODE_NONE;
	memset((void*)&m_BuffetCmdDis, 0, sizeof(m_BuffetCmdDis));
	memset((void*)&m_BuffetCmdAcc, 0, sizeof(m_BuffetCmdAcc));

	m_eWhiteNoiseMode = eWHITENOISE_MODE_NONE;
	memset((void*)&m_WhiteNoiseCmdDis, 0, sizeof(m_WhiteNoiseCmdDis));
	memset((void*)&m_WhiteNoiseCmdAcc, 0, sizeof(m_WhiteNoiseCmdAcc));

	m_uiNumCommandsToSkip = 0;
}

//---------------------------------------------------------
// Opens interface to MBC, given the specified
// configuration text file.
// 
// Input:
//      Pointer to opened configuration file.
// Return:
//      true if open of interface is successful, else false.
//---------------------------------------------------------
bool CMBCInterface::Open(CConfigFile* pConfig)
{
	const char* pszParam;

	// Get Configuration
	m_eHostInterface = eINTERFACE_ETHERNET;
	m_iUpdateRateHz = atoi(pConfig->Read("MBC", "UpdateRateHz", "30"));
	pszParam = pConfig->Read("MBC", "RedundantCommands", "False");
	m_bRedundantCommands = (_stricmp(pszParam, "True") == 0) ? true : false;

	// initialize the status response word
	m_statusResponseWord.theBits.RequestedFeedbackMode = /*FEEDBACK_LENGTH ||*/ FEEDBACK_DOF;
	m_statusResponseWord.theBits.updateRate = m_iUpdateRateHz;

	pszParam = pConfig->Read("MBC_Ethernet", "MBCIpAddress", "10.1.1.1");
	strcpy_s(m_szMBCIpAddress, MAX_PATH, pszParam);
	pszParam = pConfig->Read("MBC_Ethernet", "MBCPort", "16384");
	m_iMBCPort = atoi(pszParam);
	pszParam = pConfig->Read("MBC_Ethernet", "SCCIpAddress", "10.1.1.1");
	strcpy_s(m_szSCCIpAddress, MAX_PATH, pszParam);
	pszParam = pConfig->Read("MBC_Ethernet", "SCCNetmask", "255.255.255.0");
	strcpy_s(m_szSCCNetmask, MAX_PATH, pszParam);
	pszParam = pConfig->Read("MBC_Ethernet", "SCCPort", "16386");
	m_iSCCPort = atoi(pszParam);

    // ~(Michael Saar) --- DEBUG
    std::cout << "MBC IP Address: " << m_szMBCIpAddress << std::endl;
    std::cout << "MBC Port: " << m_iMBCPort << std::endl;
    std::cout << "SCC IP Address: " << m_szSCCIpAddress << std::endl;
    std::cout << "SCC Netmask: " << m_szSCCNetmask << std::endl;
    std::cout << "SCC Port: " << m_iSCCPort << std::endl;
    // ~(Michael Saar) --- DEBUG END
	LogWriteLn("Opening Ethernet Sockets");
	LogWriteLn("Host(%s:%d) MBC(%s:%d)", m_szSCCIpAddress, m_iSCCPort, m_szMBCIpAddress, m_iMBCPort);

	// Initialize the network driver, set the network adapter settings.
	WSAData data;
	WSAStartup(MAKEWORD(2, 2), &data);

	// Host Address
	m_localAddress.sin_family = AF_INET;
	m_localAddress.sin_addr.s_addr = inet_addr(m_szSCCIpAddress);
	m_localAddress.sin_port = htons(m_iSCCPort);

	// MBC Address
	m_destAddress.sin_family = AF_INET;
	m_destAddress.sin_addr.s_addr = inet_addr(m_szMBCIpAddress);
	m_destAddress.sin_port = htons(m_iMBCPort);

	//todo:why the socket is created in UDP protocl and not TCP protocol ?
	// create the sockets
	m_hostSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	// bind to the local address
	bind(m_hostSocket, (sockaddr *)&m_localAddress, sizeof(m_localAddress));
	if (INVALID_SOCKET == m_hostSocket) {
		LogWriteLn("Socket creation failure");
		return false;
	}

	// Event for status change
	m_hStatusChangeEvent = CreateEvent(NULL, false, false, NULL);

	// Create transmit thread to send commands to MBC.
	m_hXmtThread = CreateThread(NULL,
		0,
		TransmitThread,
		(PVOID)this,
		0,
		0
		);
	SetThreadPriority(m_hXmtThread, THREAD_PRIORITY_TIME_CRITICAL);

	// Create receive thread to receive status from MBC.
	m_hRcvThread = CreateThread(NULL,
		0,
		ReceiveThread,
		(PVOID)this,
		0,
		0
		);
	SetThreadPriority(m_hRcvThread, THREAD_PRIORITY_TIME_CRITICAL);

	m_logger = pConfig->GetLogger();

	m_bOpen = true;
	return true;
}




//---------------------------------------------------------
// Closes interface to MBC.
//---------------------------------------------------------
void CMBCInterface::Close()
{
	// Stop transmit thread
	if (m_hXmtThread != NULL)
	{
		SuspendThread(m_hXmtThread);
		CloseHandle(m_hXmtThread);
	}

	// Stop receive thread
	if (m_hRcvThread != NULL)
	{
		SuspendThread(m_hRcvThread);
		CloseHandle(m_hRcvThread);
	}

	if (m_hStatusChangeEvent != NULL)
	{
		CloseHandle(m_hStatusChangeEvent);
	}

	if (m_hostSocket != INVALID_SOCKET)
	{
		closesocket(m_hostSocket);
	}

	m_bOpen = false;

	WSACleanup();
}

void CMBCInterface::SetSendThreadPriority(DWORD priority)
{
	SetThreadPriority(m_hXmtThread, priority);
}
void CMBCInterface::SetReceivehreadPriority(DWORD priority)
{
	SetThreadPriority(m_hRcvThread, priority);
}


//---------------------------------------------------------
// Thread for receiving status messages from MBC.
// When a status message is received, it is compared
// to last status message. If different, then the
// fields that have changed are output to log.
//---------------------------------------------------------
DWORD WINAPI ReceiveThread(LPVOID lpThreadParameter)
{
	CMBCInterface* pMBCIf = (CMBCInterface*)lpThreadParameter;

	pMBCIf->ReceiveStatusLoop();

	return 0;
}

void CMBCInterface::ReceiveStatusLoop()
{
	MBC2SCC_MB_STAT_RESPONSE_MSG_TYPE lastRcvRsp;    /* last receive response */
	memset((void*)&lastRcvRsp, 0, sizeof(lastRcvRsp));

	while (1)
	{
		// sleep, to generate the desired command rate.
		Sleep(1000 / m_iUpdateRateHz);

		if (GetStatusMessage())
		{

			MBC2SCC_MB_STAT_RESPONSE_MSG_TYPE* pRspMsg = (MBC2SCC_MB_STAT_RESPONSE_MSG_TYPE*)m_pStatusMsg;

			*m_logger << "MBC Response sequence number: " << pRspMsg->msgHeader.packetSequenceCount;
				
			//*m_logger << "Get response msg with optional status  : " << pRspMsg->optStatusData << " \n";

			if (pRspMsg->optStatusData == OPT_STATUS_DATA_DOF)
			{
				//MBC2SCC_DOF_RESPONSE_MSG_TYPE * dofResponseMsg = (MBC2SCC_DOF_RESPONSE_MSG_TYPE*)m_rxStorageBuffer;
				//memcpy(dofResponseMsg, m_rxStorageBuffer + sizeof(MBC2SCC_MB_STAT_RESPONSE_MSG_TYPE), sizeof(MBC2SCC_DOF_RESPONSE_MSG_TYPE));
				m_currentPosition = (MBC2SCC_DOF_RESPONSE_MSG_TYPE*)(m_rxStorageBuffer + sizeof(MBC2SCC_MB_STAT_RESPONSE_MSG_TYPE));

				/**m_logger << "DOF Roll Resp : " << m_currentPosition->rollFeedback << " ; ";

				*m_logger << "DOF Pitch Resp : " << m_currentPosition->pitchFeedback << " ; ";

				*m_logger << "DOF Yaw Resp : " << m_currentPosition->yawFeedback << " ; ";

				*m_logger << "DOF Long Resp : " << m_currentPosition->longitudinalFeedback << " ; ";

				*m_logger << "DOF Lateral Resp : " << m_currentPosition->lateralFeedback << " ; ";

				*m_logger << "DOF Heave Resp : " << m_currentPosition->heaveFeedback;*/
			}

			// compare with last packet to check for changes. Skip over header
			// since packet counters will change.
			if (memcmp(((char*)pRspMsg) + sizeof(MSG_HEADER_TYPE),
				((char*)&lastRcvRsp) + sizeof(MSG_HEADER_TYPE),
				sizeof(lastRcvRsp) - sizeof(MSG_HEADER_TYPE)) != 0)
			{
				// Output the state
				LogWriteLn("State[%s] ", GetMachineStateStr(pRspMsg->statusResponseWord.theBits.encodedMachineState));
				// if rate changed, output
				if (pRspMsg->statusResponseWord.theBits.updateRate != lastRcvRsp.statusResponseWord.theBits.updateRate)
					LogWrite("Rate[%04d] ", pRspMsg->statusResponseWord.theBits.updateRate);
				// output motion mode
				if (pRspMsg->statusResponseWord.theBits.motionMode != lastRcvRsp.statusResponseWord.theBits.motionMode)
					LogWrite("Motion[%s] ", GetMotionModeStr(pRspMsg->statusResponseWord.theBits.motionMode));
				// output feedback mode
				if (pRspMsg->statusResponseWord.theBits.feedbackMode != lastRcvRsp.statusResponseWord.theBits.feedbackMode)
					LogWrite("FB[%s] ", GetMotionModeStr(pRspMsg->statusResponseWord.theBits.feedbackMode));
				// output discrete IO
				if (pRspMsg->discreteIO.theValue != lastRcvRsp.discreteIO.theValue)
					LogWrite("IO[0x%08X] ", pRspMsg->discreteIO.theValue);
				// fault 1
				if (pRspMsg->latchedFault1.theValue != lastRcvRsp.latchedFault1.theValue)
					LogWrite("F1[0x%08X] ", pRspMsg->latchedFault1.theValue);
				// fault 2
				if (pRspMsg->latchedFault2.theValue != lastRcvRsp.latchedFault2.theValue)
					LogWrite("F2[0x%08X] ", pRspMsg->latchedFault2.theValue);
				// fault 3
				if (pRspMsg->latchedFault3.theValue != lastRcvRsp.latchedFault3.theValue)
					LogWrite("F3[0x%08X] ", pRspMsg->latchedFault3.theValue);

				// store the new machine state                
				m_iMachineState = (ENCODED_MACHINE_STATE_TYPE)pRspMsg->statusResponseWord.theBits.encodedMachineState;

				// save packet for comparison with next
				lastRcvRsp = *pRspMsg;

				// set event that machine state has changed.
				SetEvent(m_hStatusChangeEvent);
			}
		}
	}
}

//---------------------------------------------------------
// Transmit Thread that sends command packets to the MBC.
//---------------------------------------------------------
DWORD WINAPI TransmitThread(LPVOID lpThreadParameter)
{
	CMBCInterface* pMBCIf = (CMBCInterface*)lpThreadParameter;

	while (1)
	{
		pMBCIf->SendCommandMessage();
	}
}





//---------------------------------------------------------
// Sends command message to MBC based on the command 
// configuration set by the application.
// Builds the command packet and sends it on the interface
// transport.
//
// Return:
//      true if successful, else false.
//---------------------------------------------------------
bool CMBCInterface::SendCommandMessage()
{
	MSG_HEADER_TYPE* pPacket = (MSG_HEADER_TYPE*)m_txStorageBuffer;

	// sleep, to generate the desired command rate.
	Sleep(1000 / m_iUpdateRateHz);
	//std::this_thread::sleep_for(std::chrono::nanoseconds(1));

	// based on command mode, update the position based on signal
	// generator, if enabled.
	//
	switch (m_eCommandMode)
	{
	case eCMD_MODE_PLAYBACK:
		m_msgHeader.messageID = SCC2MBC_PLAYBACK_COMMAND_MODE_MSG_ID;
		break;

	case eCMD_MODE_LENGTH:
		m_msgHeader.messageID = SCC2MBC_LENGTH_COMMAND_MODE_MSG_ID;

		// if state is engaged, and signal generator is enabled,
		// then get next position sample.
		if ((EM_STATE_ENGAGED == m_iMachineState) &&
			(CSignalGenerator::eWAVEFORM_NONE != m_eSigGenWave))
		{
			float fNextLength = m_SignalGenerator.GetSample();
			// if redundant commands, then update length every other command
			if (m_bRedundantCommands)
			{
				if ((m_msgHeader.packetSequenceCount % 2) == 0)
				{
					break;
				}
			}

			// update length position
			SetLengthPosition(fNextLength);
		}
		break;

	case eCMD_MODE_DOF:
		m_msgHeader.messageID = SCC2MBC_DOF_COMMAND_MODE_MSG_ID;
		// if state is engaged, and signal generator is enabled,
		// then get next position sample.
		if ((EM_STATE_ENGAGED == m_iMachineState) &&
			(CSignalGenerator::eWAVEFORM_NONE != m_eSigGenWave))
		{
			float fNextPos = m_SignalGenerator.GetSample();

			switch (m_eSigGenAxis)
			{
			case eSIGNALGEN_AXIS_ROLL:
				m_dofCmd.roll = fNextPos; break;
			case eSIGNALGEN_AXIS_PITCH:
				m_dofCmd.pitch = fNextPos; break;
			case eSIGNALGEN_AXIS_YAW:
				m_dofCmd.yaw = fNextPos; break;
			case eSIGNALGEN_AXIS_LONGITUDINAL:
				m_dofCmd.longitudinal = fNextPos; break;
			case eSIGNALGEN_AXIS_LATITUDINAL:
				m_dofCmd.lateral = fNextPos; break;
			case eSIGNALGEN_AXIS_HEAVE:
				m_dofCmd.heave = fNextPos; break;
			}
		}
		break;

	case eCMD_MODE_MDA:
		m_msgHeader.messageID = SCC2MBC_MDA_COMMAND_MODE_MSG_ID;

		// if state is engaged, and signal generator is enabled,
		// then get next position sample.
		if ((EM_STATE_ENGAGED == m_iMachineState) &&
			(CSignalGenerator::eWAVEFORM_NONE != m_eSigGenWave))
		{
			float fNextPos = m_SignalGenerator.GetSample();

			switch (m_eSigGenAxis)
			{
			case eSIGNALGEN_AXIS_ROLL:
				m_mdaCmd.roll = fNextPos; break;
			case eSIGNALGEN_AXIS_PITCH:
				m_mdaCmd.pitch = fNextPos; break;
			case eSIGNALGEN_AXIS_YAW:
				m_mdaCmd.yaw = fNextPos; break;
			case eSIGNALGEN_AXIS_LONGITUDINAL:
				m_mdaCmd.longitudinalAcceleration = fNextPos; break;
			case eSIGNALGEN_AXIS_LATITUDINAL:
				m_mdaCmd.lateralAcceleration = fNextPos; break;
			case eSIGNALGEN_AXIS_HEAVE:
				m_mdaCmd.verticalAcceleration = fNextPos; break;
			}
		}
		break;
	}

	// increment packet sequence counter
	m_msgHeader.packetSequenceCount++;

	// build the packet, copy bytes to network order
	int iPacketLength = BuildCommandMessage();
	SwapSCCBytes(m_txBuffer, m_txStorageBuffer, iPacketLength, TO_NET_ORDER);

	// send to network device, unless skipping for testing.
	if (m_uiNumCommandsToSkip > 0)
	{
		m_uiNumCommandsToSkip--;
	}
	else
	{
		//todo:what happened if not all the data for the packet is sent? Does the sendto is blocking function here , or not (with option) ?
		// send the pkt
		sendto(m_hostSocket, m_txBuffer, iPacketLength , 0, (sockaddr *)&m_destAddress, sizeof(m_destAddress));
	}
	return true;
}

//---------------------------------------------------------
// Builds the command packet, stores in m_txStorageBuffer.
//
// Return:
//      true if successful, else false.
//---------------------------------------------------------
int CMBCInterface::BuildCommandMessage()
{
	SE_ACTIVE_TYPE seActive = SE_ACTIVE_CMD_DONE;

	// copy the message header last, since the packet length will change
	CmdPacketInit();

	CmdPacketAppend(&m_motionCommandWord, sizeof(m_motionCommandWord));
	CmdPacketAppend(&m_statusResponseWord, sizeof(m_statusResponseWord));

	// copy the command based on the type
	switch (m_eCommandMode)
	{
	case eCMD_MODE_LENGTH:
		CmdPacketAppend(&m_lengthCmd, sizeof(m_lengthCmd));
		break;
	case eCMD_MODE_DOF:
		CmdPacketAppend(&m_dofCmd, sizeof(m_dofCmd));
		break;
	case eCMD_MODE_MDA:
		CmdPacketAppend(&m_mdaCmd, sizeof(m_mdaCmd));
		break;
	case eCMD_MODE_PLAYBACK:
		break;
	}

	// Special Effects not allowed in Playback mode
	if (eCMD_MODE_PLAYBACK != m_eCommandMode)
	{
		// Add  Direct Displacement (DOF) Option 1
		if (eDIRECT_DISPLACEMENT_MODE_DOF == m_eDirectDisplamentMode)
		{
			seActive = SE_ACTIVE_CMD_DOF;
			CmdPacketAppend(&seActive, sizeof(seActive));
			CmdPacketAppend(&m_DirectDisplacementDOF, sizeof(m_DirectDisplacementDOF));
		}
		else
			// Add  Direct Displacement (Length) Option 2
			if (eDIRECT_DISPLACEMENT_MODE_LENGTH == m_eDirectDisplamentMode)
			{
				seActive = SE_ACTIVE_CMD_LENGTH;
				CmdPacketAppend(&seActive, sizeof(seActive));
				CmdPacketAppend(&m_DirectDisplacementLength, sizeof(m_DirectDisplacementLength));
			}
			else
				// Add Buffets (Displacement) Option 3
				if (eBUFFET_MODE_DISPLACEMENT == m_eBuffetMode)
				{
					seActive = SE_ACTIVE_CMD_BUFFET;
					CmdPacketAppend(&seActive, sizeof(seActive));
					m_BuffetCmdDis.numSineWaves = MAX_SE_BUFFET_WAVES;
					CmdPacketAppend(&m_BuffetCmdDis, sizeof(m_BuffetCmdDis));
				}
				else
					// Add Buffets (Acceleration) Option 8
					if (eBUFFET_MODE_ACCELERATION == m_eBuffetMode)
					{
						seActive = SE_ACTIVE_CMD_BUFFET_X;
						CmdPacketAppend(&seActive, sizeof(seActive));
						m_BuffetCmdAcc.numSineWaves = MAX_SE_BUFFET_WAVES;
						CmdPacketAppend(&m_BuffetCmdAcc, sizeof(m_BuffetCmdAcc));
					}

		// Add WhiteNoise (Displacement) Option 4
		if (eWHITENOISE_MODE_DISPLACEMENT == m_eWhiteNoiseMode)
		{
			seActive = SE_ACTIVE_CMD_WHITE_NOISE;
			CmdPacketAppend(&seActive, sizeof(seActive));
			m_WhiteNoiseCmdDis.numWhiteNoiseWaves = MAX_SE_WHITE_NOISE_WAVES;
			CmdPacketAppend(&m_WhiteNoiseCmdDis, sizeof(m_WhiteNoiseCmdDis));
		}
		else
			// Add WhiteNoise (Acceleration) Option 8
			if (eWHITENOISE_MODE_ACCELERATION == m_eWhiteNoiseMode)
			{
				seActive = SE_ACTIVE_CMD_WHITE_NOISE_X;
				CmdPacketAppend(&seActive, sizeof(seActive));
				m_WhiteNoiseCmdAcc.numWhiteNoiseXWaves = MAX_SE_WHITE_NOISE_X_WAVES;
				CmdPacketAppend(&m_WhiteNoiseCmdAcc, sizeof(m_WhiteNoiseCmdAcc));
			}

		// add special effects terminator
		seActive = SE_ACTIVE_CMD_DONE;
		CmdPacketAppend(&seActive, sizeof(seActive));
	}

	// update packet length and copy message header
	CmdPacketDone();

	return CmdPacketLen();
}

//-----------------------------------------------------------------------------
// String convert routines.
//-----------------------------------------------------------------------------

const char *CMBCInterface::GetMotionCommandStr(MOTION_CMD_WORD_TYPE iMotionCommand)
{
	return ConvertEnumToStr(iMotionCommand, sg_szMotionCommands);
}
const char* CMBCInterface::GetMachineStateStr(int iCode)
{
	return ConvertEnumToStr(iCode, sg_szMachineStates);
}
ENCODED_MACHINE_STATE_TYPE CMBCInterface::GetMachineStateEnum(const char* szState)
{
	return (ENCODED_MACHINE_STATE_TYPE)ConvertStrToEnum(szState, sg_szMachineStates);
}
const char* CMBCInterface::GetMotionModeStr(int iMode)
{
	return ConvertEnumToStr(iMode, sg_szMotionStates);
}


//---------------------------------------------------------
// Waits for specified state. 
// 
// Input:
//      iWaitMachineState   - state to wait for
//      iTimeoutMsecs - time to wait for state
//      bExitOnFault - if true, then return if fault state is entered
//                      if false, then ignore fault states
// Return:
//      true if state reached, false if timeout or
//         fault stat entered.
//---------------------------------------------------------
bool CMBCInterface::WaitForState(ENCODED_MACHINE_STATE_TYPE eWaitMachineState, int iTimeoutMsecs, bool bExitOnFault /* =true */)
{
#define STATE_CHECK_PERIOD_MS 200

	LogWriteLn("Wait For State[%s]", GetMachineStateStr(eWaitMachineState));

	for (int i = 0; i < (iTimeoutMsecs / STATE_CHECK_PERIOD_MS); i++)
	{
		// check for state match
		if (m_iMachineState == eWaitMachineState)
		{
			return true;
		}

		// wait for state change
		WaitForSingleObject(m_hStatusChangeEvent, STATE_CHECK_PERIOD_MS);

		// check for fault state
		if (bExitOnFault)
		{
			switch (m_iMachineState)
			{
			case EM_STATE_FAULT_1:
			case EM_STATE_FAULT_2:
			case EM_STATE_FAULT_3:
				LogWriteLn("Fault occurred waiting for state[%s].", GetMachineStateStr(eWaitMachineState));
				return false;
			}
		}
	}

	LogWriteLn("Timeout waiting for state[%s].", GetMachineStateStr(eWaitMachineState));

	return false;
}


//---------------------------------------------------------
// Sets motion command for next command message.
//---------------------------------------------------------
void CMBCInterface::SetMotionCommand(MOTION_CMD_WORD_TYPE eMotionCommand)
{
	LogWriteLn("Motion Command[%s]", GetMotionCommandStr(eMotionCommand));
	m_motionCommandWord = eMotionCommand;
}
//---------------------------------------------------------
// Sets command mode for next command message.
//---------------------------------------------------------
void CMBCInterface::SetCommandMode(TECommandMode eMode, unsigned int uiPlaybackFileId /* = 0 */)
{
	m_eCommandMode = eMode;

	m_statusResponseWord.theBits.ReplayFile = uiPlaybackFileId;
}

//---------------------------------------------------------
// Sets length position for next command message.
//---------------------------------------------------------
void CMBCInterface::SetLengthPosition(double dLength)
{
	m_lengthCmd.actuatorLength.A =
		m_lengthCmd.actuatorLength.B =
		m_lengthCmd.actuatorLength.C =
		m_lengthCmd.actuatorLength.D =
		m_lengthCmd.actuatorLength.E =
		m_lengthCmd.actuatorLength.F = (float)dLength;
}

//---------------------------------------------------------
// Sets dof position for next command message.
//---------------------------------------------------------
void CMBCInterface::SetDofPosition(TEDofAxis eDofAxis, double dValue)
{
	switch (eDofAxis)
	{
	case eDOF_AXIS_ROLL:
		m_dofCmd.roll = (float)dValue;
		break;
	case eDOF_AXIS_PITCH:
		m_dofCmd.pitch = (float)dValue;
		break;
	case eDOF_AXIS_YAW:
		m_dofCmd.yaw = (float)dValue;
		break;
	case eDOF_AXIS_LONGITUDINAL:
		m_dofCmd.longitudinal = (float)dValue;
		break;
	case eDOF_AXIS_LATITUDINAL:
		m_dofCmd.lateral = (float)dValue;
		break;
	case eDOF_AXIS_HEAVE:
		m_dofCmd.heave = (float)dValue;
		break;
	}
}

MBC2SCC_DOF_RESPONSE_MSG_TYPE* CMBCInterface::GetDofPosition()
{
	return m_currentPosition;
}


//---------------------------------------------------------
// Sets the mda parameter for mda command.
//---------------------------------------------------------
void CMBCInterface::SetMda(TEMdaParam eMdaParam, double dValue)
{
	float fValue = (float)dValue;

	switch (eMdaParam)
	{
	case eMDA_PARAM_NONE:
		break;
	case eMDA_PARAM_ROLL:
		m_mdaCmd.roll = fValue;
		break;
	case eMDA_PARAM_PITCH:
		m_mdaCmd.pitch = fValue;
		break;
	case eMDA_PARAM_YAW:
		m_mdaCmd.yaw = fValue;
		break;
	case eMDA_PARAM_ROLLRATE:
		m_mdaCmd.rollRate = fValue;
		break;
	case eMDA_PARAM_PITCHRATE:
		m_mdaCmd.pitchRate = fValue;
		break;
	case eMDA_PARAM_YAWRATE:
		m_mdaCmd.yawRate = fValue;
		break;
	case eMDA_PARAM_ROLLACC:
		m_mdaCmd.rollAcceleration = fValue;
		break;
	case eMDA_PARAM_PITCHACC:
		m_mdaCmd.pitchAcceleration = fValue;
		break;
	case eMDA_PARAM_YAWACC:
		m_mdaCmd.yawAcceleration = fValue;
		break;
	case eMDA_PARAM_LONGACC:
		m_mdaCmd.longitudinalAcceleration = fValue;
		break;
	case eMDA_PARAM_LATACC:
		m_mdaCmd.lateralAcceleration = fValue;
		break;
	case eMDA_PARAM_HEAVEACC:
		m_mdaCmd.verticalAcceleration = fValue;
		break;
	case eMDA_PARAM_FREEZE:
		m_mdaCmd.mdaCommandWord = (unsigned int)fValue;
		break;
	}
}

//---------------------------------------------------------
// Sets DirectDisplament parameters.
//---------------------------------------------------------
void CMBCInterface::SetDirectDisplacement
(
TEDirectDisplacementMode eMode,
double dVal1,
double dVal2,
double dVal3,
double dVal4,
double dVal5,
double dVal6
)
{
	// set the mode if not already set. if already set, then error since it
	// should not be changed.
	if ((m_eDirectDisplamentMode != eDIRECT_DISPLACEMENT_MODE_NONE) &&
		(m_eDirectDisplamentMode != eMode))
	{
		printf("\nDirect Displament mode change not allowed!");
		return;
	}
	m_eDirectDisplamentMode = eMode;

	switch (m_eDirectDisplamentMode)
	{
	case eDIRECT_DISPLACEMENT_MODE_DOF:
	{
		SCC2MBC_SE_DOF_CMD_MSG_TYPE* pDirectDisplacement = &m_DirectDisplacementDOF;
		pDirectDisplacement->roll = (float)dVal1;
		pDirectDisplacement->pitch = (float)dVal2;
		pDirectDisplacement->yaw = (float)dVal3;
		pDirectDisplacement->longitudinal = (float)dVal4;
		pDirectDisplacement->lateral = (float)dVal5;
		pDirectDisplacement->heave = (float)dVal6;
	}
	break;
	case eDIRECT_DISPLACEMENT_MODE_LENGTH:
	{
		ACTUATOR_TYPE* pDirectDisplacement = &m_DirectDisplacementLength.actuatorLength;
		pDirectDisplacement->A = (float)dVal1;
		pDirectDisplacement->B = (float)dVal2;
		pDirectDisplacement->C = (float)dVal3;
		pDirectDisplacement->D = (float)dVal4;
		pDirectDisplacement->E = (float)dVal5;
		pDirectDisplacement->F = (float)dVal6;
	}
	break;
	}
}


//---------------------------------------------------------
// Sets buffet parameters.
//---------------------------------------------------------
void CMBCInterface::SetBuffet
(
TEBuffetMode eMode,
unsigned int iIndex,
double dFreqHz,
double dAmpX,
double dAmpY,
double dAmpZ
)
{
	// set the mode if not already set. if already set, then error since it
	// should not be changed.
	if ((m_eBuffetMode != eBUFFET_MODE_NONE) &&
		(m_eBuffetMode != eMode))
	{
		printf("\nBuffet mode change not allowed!");
		return;
	}
	m_eBuffetMode = eMode;

	// make index 0-based
	iIndex--;

	switch (m_eBuffetMode)
	{
	case eBUFFET_MODE_DISPLACEMENT:
		if (iIndex < MAX_SE_BUFFET_WAVES)
		{
			SCC2MBC_SE_BUFFET_CMD* pBuffet = &m_BuffetCmdDis.Buffet[iIndex];

			pBuffet->frequency = (float)dFreqHz;
			pBuffet->amplitude[0] = (float)dAmpX;
			pBuffet->amplitude[1] = (float)dAmpY;
			pBuffet->amplitude[2] = (float)dAmpZ;
		}
		break;
	case eBUFFET_MODE_ACCELERATION:
		if (iIndex < MAX_SE_BUFFET_WAVES)
		{
			SCC2MBC_SE_BUFFET_CMD* pBuffet = &m_BuffetCmdAcc.Buffet[iIndex];

			pBuffet->frequency = (float)dFreqHz;
			pBuffet->amplitude[0] = (float)dAmpX;
			pBuffet->amplitude[1] = (float)dAmpY;
			pBuffet->amplitude[2] = (float)dAmpZ;
		}
		break;
	}
}



//---------------------------------------------------------
// Sets white noise generator parameters
//---------------------------------------------------------
void CMBCInterface::SetWhiteNoise
(
TEWhiteNoiseMode eMode,
unsigned int iIndex,
double dAmpX, double dHpX, double dLpX,
double dAmpY, double dHpY, double dLpY,
double dAmpZ, double dHpZ, double dLpZ
)
{
	// set the mode if not already set. if already set, then error since it
	// should not be changed.
	if ((m_eWhiteNoiseMode != eWHITENOISE_MODE_NONE) &&
		(m_eWhiteNoiseMode != eMode))
	{
		printf("\nWhite Noise mode change not allowed!");
		return;
	}
	m_eWhiteNoiseMode = eMode;

	// make index 0-based
	iIndex--;

	switch (m_eWhiteNoiseMode)
	{
	case eWHITENOISE_MODE_DISPLACEMENT:
		if (iIndex < MAX_SE_WHITE_NOISE_WAVES)
		{
			SCC2MBC_SE_WHITE_NOISE_CMD* pWave = &m_WhiteNoiseCmdDis.WhiteNoise[iIndex];

			pWave->amplitude[0] = (float)dAmpX;
			pWave->amplitude[1] = (float)dAmpY;
			pWave->amplitude[2] = (float)dAmpZ;
		}
		break;
	case eWHITENOISE_MODE_ACCELERATION:
		if (iIndex < MAX_SE_WHITE_NOISE_X_WAVES)
		{
			SCC2MBC_SE_WHITE_NOISE_X_CMD* pWave = &m_WhiteNoiseCmdAcc.WhiteNoiseX[iIndex*MAX_NUM_AXIS];

			pWave[0].amplitude = (float)dAmpX;
			pWave[0].HP_Roll_Off = (float)dHpX;
			pWave[0].LP_Roll_Off = (float)dLpX;

			pWave[1].amplitude = (float)dAmpY;
			pWave[1].HP_Roll_Off = (float)dHpY;
			pWave[1].LP_Roll_Off = (float)dLpY;

			pWave[2].amplitude = (float)dAmpZ;
			pWave[2].HP_Roll_Off = (float)dHpZ;
			pWave[2].LP_Roll_Off = (float)dLpZ;
		}
		break;
	}
}

//---------------------------------------------------------
// Sets signal generator parameters.
//---------------------------------------------------------
void CMBCInterface::SetSignalGenerator
(TESignalAxis eSignalGenAxis,
CSignalGenerator::TWaveForm eWaveForm,
double dFrequency,
double dAmplitude,
double dOffset
)
{
	m_eSigGenWave = eWaveForm;
	m_eSigGenAxis = eSignalGenAxis;
	m_SignalGenerator.SetParameters(eWaveForm, dFrequency, dAmplitude, dOffset, m_iUpdateRateHz);
}



//---------------------------------------------------------
// Waits for status message from MBC.
// If true, status message is copied to m_rxStorageBuffer.
//
// Return:
//      true if successful, else false.
//---------------------------------------------------------
bool CMBCInterface::GetStatusMessage()
{
	sockaddr_in senderAddr;

	// Blocking Rx for host data 
	int iFromLength = sizeof(senderAddr);
	int iResult = recvfrom(m_hostSocket, m_rxBuffer, TXRX_BUFFER_SIZE, 0, (sockaddr *)&senderAddr, &iFromLength);
	if (iResult > 0) {
		SwapSCCBytes(m_rxStorageBuffer, m_rxBuffer, iResult, TO_HOST_ORDER);
		return true;
	}
	return false;
}

//---------------------------------------------------------
// Swaps data between network and host order.
//---------------------------------------------------------
void
CMBCInterface::SwapSCCBytes(char* pDst, char* pSrc, int iNumBytesToSwap, BYTE_ORDER_TYPE convert)
{
	unsigned int  intsToSwap = iNumBytesToSwap / sizeof(int);
	unsigned int  intCntr = 0;
	unsigned int *pDstIntPtr = (unsigned int*)pDst;
	unsigned int *pSrcIntPtr = (unsigned int*)pSrc;

	for (intCntr = 0; intCntr < intsToSwap; intCntr++)
	{
		if (convert == TO_NET_ORDER)
			*pDstIntPtr++ = htonl(*pSrcIntPtr++);
		else
			if (convert == TO_HOST_ORDER)
				*pDstIntPtr++ = ntohl(*pSrcIntPtr++);
	}

	return;
}

//---------------------------------------------------------
// Used in command packet building.
// Initializes building of the packet.
//---------------------------------------------------------
void CMBCInterface::CmdPacketInit()
{
	m_iCmdPktLen = 0;
	// start buffer after message header.
	// msg header added at end when packet length is known.
	m_pCmdBuffer = m_txStorageBuffer + sizeof(m_msgHeader);
}

//---------------------------------------------------------
// Used in command packet building.
// Appends data to the packet and updates pointers and 
// counters.
//---------------------------------------------------------
void CMBCInterface::CmdPacketAppend(void* pData, int iLen)
{
	memcpy((void*)m_pCmdBuffer, pData, iLen);
	m_iCmdPktLen += iLen;
	m_pCmdBuffer += iLen;
}
//---------------------------------------------------------
// Used in command packet building.
// Completes packet by updating message header packet
// length and writing header to the packet.
//---------------------------------------------------------
void CMBCInterface::CmdPacketDone()
{
	int iDataLen = 0;

	// update packet length and copy message header
	m_msgHeader.packetLength = m_iCmdPktLen;
	iDataLen = sizeof(m_msgHeader);
	memcpy(m_txStorageBuffer, &m_msgHeader, iDataLen);
	m_iCmdPktLen += iDataLen;
}

//---------------------------------------------------------
// Used in command packet building.
// Returns packet length that was built.
//---------------------------------------------------------
int CMBCInterface::CmdPacketLen()
{
	return m_iCmdPktLen;
}

//-------------------------------------------------------------------
// END OF FILE
//-------------------------------------------------------------------
