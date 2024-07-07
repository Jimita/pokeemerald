#include "global.h"
#include "main.h"

#include <stdio.h>

#define STUB_FUNC(func) func { puts("function \"" #func "\" is a stub"); }
#define STUB_FUNC_BLOCK(func, block) func { puts("function \"" #func "\" is a stub"); block }
#define STUB_FUNC_QUIET(func) func {}
#define STUB_FUNC_QUIET_BLOCK(func, block) func { block }

STUB_FUNC_QUIET_BLOCK(bool8 HandleLinkConnection(), return 0;)
STUB_FUNC_QUIET(void Task_InitUnionRoom())
STUB_FUNC(int MultiBoot(struct MultiBootParam *mp))
STUB_FUNC(void RegisterRamReset(u32 resetFlags))
STUB_FUNC(void IntrMain())
STUB_FUNC(void GameCubeMultiBoot_Hash())
STUB_FUNC_QUIET(void GameCubeMultiBoot_Main())
STUB_FUNC(void GameCubeMultiBoot_ExecuteProgram())
STUB_FUNC(void GameCubeMultiBoot_Init())
STUB_FUNC(void GameCubeMultiBoot_HandleSerialInterrupt())
STUB_FUNC(void GameCubeMultiBoot_Quit())
STUB_FUNC(void rfu_initializeAPI())
STUB_FUNC(void InitRFUAPI(void))
/*STUB_FUNC(void rfu_STC_clearAPIVariables())
STUB_FUNC(void rfu_REQ_PARENT_resumeRetransmitAndChange())
STUB_FUNC(void rfu_UNI_PARENT_getDRAC_ACK())
STUB_FUNC(void rfu_setTimerInterrupt())
STUB_FUNC(void rfu_getSTWIRecvBuffer())
STUB_FUNC(void rfu_setMSCCallback())
STUB_FUNC(void rfu_setREQCallback())
STUB_FUNC(void rfu_enableREQCallback())
STUB_FUNC(void rfu_STC_REQ_callback())
STUB_FUNC(void rfu_CB_defaultCallback())
STUB_FUNC(void rfu_waitREQComplete())
STUB_FUNC(void rfu_REQ_RFUStatus())
STUB_FUNC(void rfu_getRFUStatus())
STUB_FUNC(void rfu_MBOOT_CHILD_inheritanceLinkStatus())
STUB_FUNC(void rfu_REQ_stopMode())
STUB_FUNC(void rfu_CB_stopMode())
STUB_FUNC(void rfu_REQBN_softReset_and_checkID())
STUB_FUNC(void rfu_REQ_reset())
STUB_FUNC(void rfu_CB_reset())
STUB_FUNC(void rfu_REQ_configSystem())
STUB_FUNC(void rfu_REQ_configGameData())
STUB_FUNC(void rfu_CB_configGameData())
STUB_FUNC(void rfu_REQ_startSearchChild())
STUB_FUNC(void rfu_CB_startSearchChild())
STUB_FUNC(void rfu_STC_clearLinkStatus())
STUB_FUNC(void rfu_REQ_pollSearchChild())
STUB_FUNC(void rfu_REQ_endSearchChild())
STUB_FUNC(void rfu_CB_pollAndEndSearchChild())
STUB_FUNC(void rfu_STC_readChildList())
STUB_FUNC(void rfu_REQ_startSearchParent())
STUB_FUNC(void rfu_CB_startSearchParent())
STUB_FUNC(void rfu_REQ_pollSearchParent())
STUB_FUNC(void rfu_CB_pollSearchParent())
STUB_FUNC(void rfu_REQ_endSearchParent())
STUB_FUNC(void rfu_STC_readParentCandidateList())
STUB_FUNC(void rfu_REQ_startConnectParent())
STUB_FUNC(void rfu_REQ_pollConnectParent())
STUB_FUNC(void rfu_CB_pollConnectParent())
STUB_FUNC(void rfu_getConnectParentStatus())
STUB_FUNC(void rfu_REQ_endConnectParent())
STUB_FUNC(void rfu_syncVBlank())
STUB_FUNC(void rfu_REQBN_watchLink())
STUB_FUNC(void rfu_STC_removeLinkData())
STUB_FUNC(void rfu_REQ_disconnect())
STUB_FUNC(void rfu_CB_disconnect())
STUB_FUNC(void rfu_REQ_CHILD_startConnectRecovery())
STUB_FUNC(void rfu_REQ_CHILD_pollConnectRecovery())
STUB_FUNC(void rfu_CB_CHILD_pollConnectRecovery())
STUB_FUNC(void rfu_CHILD_getConnectRecoveryStatus())
STUB_FUNC(void rfu_REQ_CHILD_endConnectRecovery())
STUB_FUNC(void rfu_STC_fastCopy())
STUB_FUNC(void rfu_REQ_changeMasterSlave())
STUB_FUNC(void rfu_getMasterSlave())
STUB_FUNC(void rfu_clearAllSlot())
STUB_FUNC(void rfu_STC_releaseFrame())
STUB_FUNC(void rfu_clearSlot())
STUB_FUNC(void rfu_setRecvBuffer())
STUB_FUNC(void rfu_NI_setSendData())
STUB_FUNC(void rfu_UNI_setSendData())
STUB_FUNC(void rfu_NI_CHILD_setSendGameName())
STUB_FUNC(void rfu_STC_setSendData_org())
STUB_FUNC(void rfu_changeSendTarget())
STUB_FUNC(void rfu_NI_stopReceivingData())
STUB_FUNC(void rfu_UNI_changeAndReadySendData())
STUB_FUNC(void rfu_UNI_readySendData())
STUB_FUNC(void rfu_UNI_clearRecvNewDataFlag())
STUB_FUNC(void rfu_REQ_sendData())
STUB_FUNC(void rfu_CB_sendData())
STUB_FUNC(void rfu_CB_sendData2())
STUB_FUNC(void rfu_CB_sendData3())
STUB_FUNC(void rfu_constructSendLLFrame())
STUB_FUNC(void rfu_STC_NI_constructLLSF())
STUB_FUNC(void rfu_STC_UNI_constructLLSF())
STUB_FUNC(void rfu_REQ_recvData())
STUB_FUNC(void rfu_CB_recvData())
STUB_FUNC(void rfu_STC_PARENT_analyzeRecvPacket())
STUB_FUNC(void rfu_STC_CHILD_analyzeRecvPacket())
STUB_FUNC(void rfu_STC_analyzeLLSF())
STUB_FUNC(void rfu_STC_UNI_receive())
STUB_FUNC(void rfu_STC_NI_receive_Sender())
STUB_FUNC(void rfu_STC_NI_receive_Receiver())
STUB_FUNC(void rfu_STC_NI_initSlot_asRecvControllData())
STUB_FUNC(void rfu_STC_NI_initSlot_asRecvDataEntity())
STUB_FUNC(void rfu_NI_checkCommFailCounter())
STUB_FUNC(void rfu_REQ_noise())
STUB_FUNC(void AgbRFU_checkID())
*/
STUB_FUNC_BLOCK(u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n), return 0;)
STUB_FUNC_BLOCK(u32 VerifyFlashSector(u16 sectorNum, u8 *src), return 0;)
/*
STUB_FUNC(void Sio32IDInit())
STUB_FUNC(void Sio32IDMain())
STUB_FUNC(void Sio32IDIntr())
*/
STUB_FUNC(void MultiBootInit(struct MultiBootParam *mp))
STUB_FUNC(int MultiBootMain(struct MultiBootParam *mp))
STUB_FUNC(void MultiBootStartProbe(struct MultiBootParam *mp))
STUB_FUNC(void MultiBootStartMaster(struct MultiBootParam *mp, const u8 *srcp, int length, u8 palette_color, s8 palette_speed))
STUB_FUNC(int MultiBootCheckComplete(struct MultiBootParam *mp))
//STUB_FUNC(IntrFunc IntrSIO32(void))
