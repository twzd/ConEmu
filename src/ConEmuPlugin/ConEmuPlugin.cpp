/* ****************************************** 
   Changes history 
   Maximus5: ����� ��� static
   2009-01-31 Maximus5
	*) ���������� ��� ������ InfoW.Control() � ��� ���������� ���������� � ��������� ��������� �������.
	*) ACTL_GETWINDOWINFO �������� �� ����� ����������� ACTL_GETSHORTWINDOWINFO, ����� � �� ���� FREE ��������. ��� ���, ��� ��������� ������ �� ���� �� ������������. � ��� ������������, ����� ACTL_GETWINDOWINFO ���� �����������.
	*) ����� ECTL_GETINFO ���� �������� ECTL_FREEINFO.
****************************************** */

//#include <stdio.h>
#include <windows.h>
//#include <windowsx.h>
//#include <string.h>
//#include <tchar.h>
#include "..\common\common.hpp"
#include "..\common\pluginW789.hpp"
#include "PluginHeader.h"
#include <vector>

extern "C" {
#include "../common/ConEmuCheck.h"
}

WARNING("����������, ��� � gszRootKey �� ����������� ��� ������������/������������");

#define MAKEFARVERSION(major,minor,build) ( ((major)<<8) | (minor) | ((build)<<16))

// minimal(?) FAR version 2.0 alpha build 757
int WINAPI _export GetMinFarVersionW(void)
{
	return MAKEFARVERSION(2,0,757);
}

/* COMMON - ���� ��������� �� ����������� */
void WINAPI _export GetPluginInfoW(struct PluginInfo *pi)
{
    static WCHAR *szMenu[1], szMenu1[255];
    szMenu[0]=szMenu1; //lstrcpyW(szMenu[0], L"[&\x2560] ConEmu"); -> 0x2584
    //szMenu[0][1] = L'&';
    //szMenu[0][2] = 0x2560;

	// ���������, �� ���������� �� ������� ������� �������, � ���� �� - ����������� �������
	IsKeyChanged(TRUE);

	if (gcPlugKey) szMenu1[0]=0; else lstrcpyW(szMenu1, L"[&\x2584] ");
	lstrcpynW(szMenu1+lstrlenW(szMenu1), GetMsgW(2), 240);


	pi->StructSize = sizeof(struct PluginInfo);
	pi->Flags = PF_EDITOR | PF_VIEWER | PF_DIALOG | PF_PRELOAD;
	pi->DiskMenuStrings = NULL;
	pi->DiskMenuNumbers = 0;
	pi->PluginMenuStrings = szMenu;
	pi->PluginMenuStringsNumber = 1;
	pi->PluginConfigStrings = NULL;
	pi->PluginConfigStringsNumber = 0;
	pi->CommandPrefix = NULL;
	pi->Reserved = 0;	
}


BOOL gbInfoW_OK = FALSE;
HANDLE WINAPI _export OpenPluginW(int OpenFrom,INT_PTR Item)
{
	if (!gbInfoW_OK)
		return INVALID_HANDLE_VALUE;

	if (gnReqCommand != (DWORD)-1) {
		gnPluginOpenFrom = OpenFrom;
		ProcessCommand(gnReqCommand, FALSE/*bReqMainThread*/, gpReqCommandData);
	}
	return INVALID_HANDLE_VALUE;
}
/* COMMON - end */


HWND ConEmuHwnd=NULL; // �������� ����� ���� ���������. ��� �������� ����.
BOOL TerminalMode = FALSE;
HWND FarHwnd=NULL;
DWORD gnMainThreadId = 0;
//HANDLE hEventCmd[MAXCMDCOUNT], hEventAlive=NULL, hEventReady=NULL;
HANDLE hThread=NULL;
FarVersion gFarVersion;
WCHAR gszDir1[CONEMUTABMAX], gszDir2[CONEMUTABMAX];
WCHAR gszRootKey[MAX_PATH];
int maxTabCount = 0, lastWindowCount = 0, gnCurTabCount = 0;
ConEmuTab* tabs = NULL; //(ConEmuTab*) calloc(maxTabCount, sizeof(ConEmuTab));
LPBYTE gpData = NULL, gpCursor = NULL;
CESERVER_REQ* gpCmdRet = NULL;
DWORD  gnDataSize=0;
//HANDLE ghMapping = NULL;
DWORD gnReqCommand = -1;
int gnPluginOpenFrom = -1;
LPVOID gpReqCommandData = NULL;
HANDLE ghReqCommandEvent = NULL;
UINT gnMsgTabChanged = 0;
CRITICAL_SECTION csTabs, csData;
WCHAR gcPlugKey=0;
BOOL  gbPlugKeyChanged=FALSE;
HKEY  ghRegMonitorKey=NULL; HANDLE ghRegMonitorEvt=NULL;
HMODULE ghFarHintsFix = NULL;
WCHAR gszPluginServerPipe[MAX_PATH];
#define MAX_SERVER_THREADS 3
//HANDLE ghServerThreads[MAX_SERVER_THREADS] = {NULL,NULL,NULL};
//HANDLE ghActiveServerThread = NULL;
HANDLE ghServerThread = NULL;
DWORD  gnServerThreadId = 0;
std::vector<HANDLE> ghCommandThreads;
//DWORD  gnServerThreadsId[MAX_SERVER_THREADS] = {0,0,0};
HANDLE ghServerTerminateEvent = NULL;
HANDLE ghPluginSemaphore = NULL;

//#if defined(__GNUC__)
//typedef HWND (APIENTRY *FGetConsoleWindow)();
//FGetConsoleWindow GetConsoleWindow = NULL;
//#endif
extern void SetConsoleFontSizeTo(HWND inConWnd, int inSizeX, int inSizeY);

#if defined(__GNUC__)
extern "C"{
  BOOL WINAPI DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved );
  HWND WINAPI GetFarHWND();
  HWND WINAPI GetFarHWND2(BOOL abConEmuOnly);
  void WINAPI GetFarVersion ( FarVersion* pfv );
  int  WINAPI ProcessEditorInputW(void* Rec);
  void WINAPI SetStartupInfoW(void *aInfo);
  BOOL WINAPI IsTerminalMode();
  BOOL WINAPI IsConsoleActive();
};
#endif

BOOL WINAPI DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			{
				#ifdef _DEBUG
				//if (!IsDebuggerPresent()) MessageBoxA(GetForegroundWindow(), "ConEmu.dll loaded", "ConEmu", 0);
				#endif
				//#if defined(__GNUC__)
				//GetConsoleWindow = (FGetConsoleWindow)GetProcAddress(GetModuleHandle(L"kernel32.dll"),"GetConsoleWindow");
				//#endif
				HWND hConWnd = GetConsoleWindow();
				gnMainThreadId = GetCurrentThreadId();
				InitHWND(hConWnd);
				
			    // Check Terminal mode
			    TCHAR szVarValue[MAX_PATH];
			    szVarValue[0] = 0;
			    if (GetEnvironmentVariable(L"TERM", szVarValue, 63)) {
				    TerminalMode = TRUE;
			    }
			    
			    if (!TerminalMode) {
					// FarHints fix for multiconsole mode...
					if (GetModuleFileName((HMODULE)hModule, szVarValue, MAX_PATH)) {
						WCHAR *pszSlash = wcsrchr(szVarValue, L'\\');
						if (pszSlash) pszSlash++; else pszSlash = szVarValue;
						lstrcpyW(pszSlash, L"infis.dll");
						ghFarHintsFix = LoadLibrary(szVarValue);
					}
			    }
			}
			break;
		case DLL_PROCESS_DETACH:
			if (ghFarHintsFix) {
				FreeLibrary(ghFarHintsFix);
				ghFarHintsFix = NULL;
			}
			break;
	}
	return TRUE;
}

BOOL WINAPI IsConsoleActive()
{
	if (ConEmuHwnd) {
		if (IsWindow(ConEmuHwnd)) {
			HWND hParent = GetParent(ConEmuHwnd);
			if (hParent) {
				HWND hTest = (HWND)GetWindowLong(hParent, GWL_USERDATA);
				return (hTest == FarHwnd);
			}
		}
	}
	return TRUE;
}

HWND WINAPI GetFarHWND2(BOOL abConEmuOnly)
{
	if (ConEmuHwnd) {
		if (IsWindow(ConEmuHwnd))
			return ConEmuHwnd;
		ConEmuHwnd = NULL;
	}
	if (abConEmuOnly)
		return NULL;
	return FarHwnd;
}

HWND WINAPI _export GetFarHWND()
{
    return GetFarHWND2(FALSE);
}

BOOL WINAPI IsTerminalMode()
{
    return TerminalMode;
}

void WINAPI _export GetFarVersion ( FarVersion* pfv )
{
	if (!pfv)
		return;

	*pfv = gFarVersion;
}

BOOL LoadFarVersion()
{
    BOOL lbRc=FALSE;
    WCHAR FarPath[MAX_PATH+1];
    if (GetModuleFileName(0,FarPath,MAX_PATH)) {
		DWORD dwRsrvd = 0;
		DWORD dwSize = GetFileVersionInfoSize(FarPath, &dwRsrvd);
		if (dwSize>0) {
			void *pVerData = calloc(dwSize, 1);
			if (pVerData) {
				VS_FIXEDFILEINFO *lvs = NULL;
				UINT nLen = sizeof(lvs);
				if (GetFileVersionInfo(FarPath, 0, dwSize, pVerData)) {
					TCHAR szSlash[3]; lstrcpyW(szSlash, L"\\");
					if (VerQueryValue ((void*)pVerData, szSlash, (void**)&lvs, &nLen)) {
						gFarVersion.dwVer = lvs->dwFileVersionMS;
						gFarVersion.dwBuild = lvs->dwFileVersionLS;
						lbRc = TRUE;
					}
				}
				free(pVerData);
			}
		}
	}

	if (!lbRc) {
		gFarVersion.dwVerMajor = 2;
		gFarVersion.dwVerMinor = 0;
		gFarVersion.dwBuild = 789;
	}

	return lbRc;
}

BOOL IsKeyChanged(BOOL abAllowReload)
{
	BOOL lbKeyChanged = FALSE;
	if (ghRegMonitorEvt) {
		if (WaitForSingleObject(ghRegMonitorEvt, 0) == WAIT_OBJECT_0) {
			lbKeyChanged = CheckPlugKey();
			if (lbKeyChanged) gbPlugKeyChanged = TRUE;
		}
	}

	if (abAllowReload && gbPlugKeyChanged) {
		// ������-�� ��� �� �������� � ������� ����...
		CheckMacro(TRUE);
		gbPlugKeyChanged = FALSE;
	}
	return lbKeyChanged;
}

WARNING("����������� ������� ����������� ������������ �� ��������, ���� ������ �� ������� ������������");
// �������� ����� ������� ������� ������ ����� - ���� ��� ��� ���� ������� ���������� F11 - ������
// ���� �������� ��� �����������. ����� ����� ��� ����-���� ���������, � ������������ - �������������� �� ���������
void ProcessCommand(DWORD nCmd, BOOL bReqMainThread, LPVOID pCommandData)
{
	if (gpCmdRet) free(gpCmdRet);
	gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;

	WARNING("��� ����� ������� �������� ����������� �������");
	// ���� ���������� ���� - ������ �� ����������
	// �� ���������� ���� � ������ ������� (Ctrl-O)

	if (bReqMainThread && (gnMainThreadId != GetCurrentThreadId())) {
		_ASSERTE(ghPluginSemaphore!=NULL);
		_ASSERTE(ghServerTerminateEvent!=NULL);
		// ������������, ����� ��������� ������ ������������ �� �����...
		HANDLE hEvents[2] = {ghServerTerminateEvent, ghPluginSemaphore};
		DWORD dwWait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
		if (dwWait == WAIT_OBJECT_0) {
			// ������ �����������
			return;
		}

		gnReqCommand = nCmd; gnPluginOpenFrom = -1;
		gpReqCommandData = pCommandData;
		ResetEvent(ghReqCommandEvent);
		
		// ���������, �� ���������� �� ������� ������� �������, � ���� �� - ����������� �������
		IsKeyChanged(TRUE);



		// ����� ����� ������� � ��������� ����
		WARNING("���������� �� WriteConsoleInput");
		SendMessage(FarHwnd, WM_KEYDOWN, VK_F14, 0);
		SendMessage(FarHwnd, WM_KEYUP, VK_F14, (LPARAM)(3<<30));

		
		//HANDLE hEvents[2] = {ghReqCommandEvent, hEventCmd[CMD_EXIT]};
		hEvents[0] = ghReqCommandEvent;
		hEvents[1] = ghServerTerminateEvent;

		//DuplicateHandle(GetCurrentProcess(), ghReqCommandEvent, GetCurrentProcess(), hEvents, 0, 0, DUPLICATE_SAME_ACCESS);
		dwWait = WaitForMultipleObjects ( 2, hEvents, FALSE, CONEMUFARTIMEOUT );
		if (dwWait) ResetEvent(ghReqCommandEvent); // ����� �������, ����� �� ���������?

		ReleaseSemaphore(ghPluginSemaphore, 1, NULL);

		gpReqCommandData = NULL;
		gnReqCommand = -1; gnPluginOpenFrom = -1;
		return;
	}
	
	/*if (gbPlugKeyChanged) {
		gbPlugKeyChanged = FALSE;
		CheckMacro(TRUE);
		gbPlugKeyChanged = FALSE;
	}*/

	EnterCriticalSection(&csData);

	switch (nCmd)
	{
		case (CMD_DRAGFROM):
		{
			if (gFarVersion.dwVerMajor==1)
				ProcessDragFromA();
			else if (gFarVersion.dwBuild>=789)
				ProcessDragFrom789();
			else
				ProcessDragFrom757();
			break;
		}
		case (CMD_DRAGTO):
		{
			if (gFarVersion.dwVerMajor==1)
				ProcessDragToA();
			else if (gFarVersion.dwBuild>=789)
				ProcessDragTo789();
			else
				ProcessDragTo757();
			break;
		}
		case (CMD_SETWINDOW):
		{
			int nTab = 0;
			// ���� �� ����� ������� ������ ����:
			if (gnPluginOpenFrom == OPEN_VIEWER || gnPluginOpenFrom == OPEN_EDITOR 
				|| gnPluginOpenFrom == OPEN_PLUGINSMENU)
			{
				_ASSERTE(pCommandData!=NULL);
				if (pCommandData!=NULL)
					nTab = *((DWORD*)pCommandData);

				if (gFarVersion.dwVerMajor==1)
					SetWindowA(nTab);
				else if (gFarVersion.dwBuild>=789)
					SetWindow789(nTab);
				else
					SetWindow757(nTab);
			}
			SendTabs(gnCurTabCount, TRUE);
			break;
		}
		case (CMD_POSTMACRO):
		{
			_ASSERTE(pCommandData!=NULL);
			if (pCommandData!=NULL)
				PostMacro((wchar_t*)pCommandData);
			break;
		}
	}

	LeaveCriticalSection(&csData);

	if (ghReqCommandEvent)
		SetEvent(ghReqCommandEvent);
}

// ��� ���� ����� ��������, ����� ���� ����������� ���������� ������� ��� ������� ConEmu
DWORD WINAPI ThreadProcW(LPVOID lpParameter)
{
	//DWORD dwProcId = GetCurrentProcessId();

	DWORD dwStartTick = GetTickCount();
	
	_ASSERTE(ConEmuHwnd!=NULL);

	while (true)
	{
		DWORD dwWait = 0;
		DWORD dwTimeout = 500;
		/*#ifdef _DEBUG
		dwTimeout = INFINITE;
		#endif*/

		//dwWait = WaitForMultipleObjects(MAXCMDCOUNT, hEventCmd, FALSE, dwTimeout);
		dwWait = WaitForSingleObject(ghServerTerminateEvent, dwTimeout);
		if (dwWait == WAIT_OBJECT_0)
			break; // ���������� �������

		if (ConEmuHwnd == NULL && FarHwnd != NULL) {
		}

		// ������������, ���� ��������� ����� ����������� � ��� ConEmuHwnd (��� ��������)
	    if (ConEmuHwnd && FarHwnd && (dwWait>=(WAIT_OBJECT_0+MAXCMDCOUNT))) {

			// ��� ���� ������ Detach! (CtrlAltTab)
			HWND hConWnd = GetConsoleWindow();
		    if (!IsWindow(ConEmuHwnd) || hConWnd!=FarHwnd) {
			    ConEmuHwnd = NULL;

				if (hConWnd!=FarHwnd || !IsWindow(FarHwnd))
				{
					if (hConWnd != FarHwnd && IsWindow(hConWnd)) {
						FarHwnd = hConWnd;
						//int nBtn = ShowMessage(1, 2);
						//if (nBtn == 0) {
						//	// Create process, with flag /Attach GetCurrentProcessId()
						//	// Sleep for sometimes, try InitHWND(hConWnd); several times
						//	WCHAR  szExe[0x200];
						//	WCHAR  szCurArgs[0x600];
						//	
						//	PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
						//	STARTUPINFO si; memset(&si, 0, sizeof(si));
						//	si.cb = sizeof(si);
						//	
						//	//TODO: ConEmu.exe
						//	int nLen = 0;
						//	if ((nLen=GetModuleFileName(0, szExe, 0x190))==0) {
						//		goto closethread;
						//	}
						//	WCHAR* pszSlash = szExe+nLen-1;
			   //             while (pszSlash>szExe && *(pszSlash-1)!=L'\\') pszSlash--;
			   //             lstrcpyW(pszSlash, L"ConEmu.exe");
			   //             
						//	DWORD dwPID = GetCurrentProcessId();
						//	wsprintf(szCurArgs, L"\"%s\" /Attach %i ", szExe, dwPID);
						//	
						//	GetEnvironmentVariableW(L"ConEmuArgs", szCurArgs+lstrlenW(szCurArgs), 0x380);
			   //             
						//	
						//	if (!CreateProcess(NULL, szCurArgs, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL,
						//			NULL, &si, &pi))
						//	{
						//		// ������ �� ������ ��������?
						//		goto closethread;
						//	}
						//	
						//	// ����
						//	while (TRUE)
						//	{
						//		dwWait = WaitForSingleObject(hEventCmd[CMD_EXIT], 200);
						//		// ��� �����������
						//		if (dwWait == WAIT_OBJECT_0) {
						//			CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
						//			goto closethread;
						//		}
						//		if (!GetExitCodeProcess(pi.hProcess, &dwPID) || dwPID!=STILL_ACTIVE) {
						//			CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
						//			goto closethread;
						//		}
						//		InitHWND(hConWnd);
						//		if (ConEmuHwnd) {
						//			// ����������, �� ����� ConEmu ������ ����� �������� �� ����...
						//			SetConsoleFontSizeTo(FarHwnd, 4, 6);
						//			MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...
						//			break;
						//		}
						//	}
						//	
						//	// ������� ������
						//	CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
						//	// ������ �� ����� ���� ��������...
						//	SendTabs(gnCurTabCount, TRUE);
						//	continue;
						//} else {
						//	// ������������ ���������, ������� �� ���� ���������
						//	goto closethread;
						//}
					} else {
						// hConWnd �� �������
						MessageBox(0, L"ConEmu was abnormally termintated!\r\nExiting from FAR", L"ConEmu plugin", MB_OK|MB_ICONSTOP|MB_SETFOREGROUND);
						TerminateProcess(GetCurrentProcess(), 100);
					}
				}
				else 
				{
					//if (bWasSetParent) { -- ��� ����� ��� �� �������, ���� ��� ���� �������� - ������
					//	SetParent(FarHwnd, NULL);
					//}
				    
					ShowWindowAsync(FarHwnd, SW_SHOWNORMAL);
					EnableWindow(FarHwnd, true);
				}
			    
				//goto closethread;
		    }
	    }

		//if (dwWait == (WAIT_OBJECT_0+CMD_EXIT))
		//{
		//	goto closethread;
		//}

		//if (dwWait>=(WAIT_OBJECT_0+MAXCMDCOUNT))
		//{
		//	continue;
		//}

		if (!ConEmuHwnd) {
			// ConEmu ����� �����������
			//int nChk = 0;
			ConEmuHwnd = GetConEmuHWND ( FALSE/*abRoot*/  /*, &nChk*/ );
		}

		//SafeCloseHandle(ghMapping);
		//// �������� ������, ��� �� ���������� � ���������
		//// ���� ���� � �� ����������� �� ����, � ������������ � ������� ����, �� �������� ��� ������� �������
		////if (dwWait != (WAIT_OBJECT_0+CMD_REQTABS)) // ���������� - ������ �����. �� �����������
		//SetEvent(hEventAlive);


		//switch (dwWait)
		//{
		//	case (WAIT_OBJECT_0+CMD_REQTABS):
		//	{
		//		/*if (!gnCurTabCount || !tabs) {
		//			CreateTabs(1);
		//			int nTabs=0; --�����! ��� ������ ���� ����� CriticalSection
		//			AddTab(nTabs, false, false, WTYPE_PANELS, 0,0,1,0); 
		//		}*/
		//		SendTabs(gnCurTabCount, TRUE);
		//		// ��������� ���� ������� �������
		//		//// ���������� - ������ �����. �� �����������
		//		//continue;
		//		break;
		//	}
		//	
		//	case (WAIT_OBJECT_0+CMD_DEFFONT):
		//	{
		//		// ���������� - �����������, ��������� �� ���������
		//		ResetEvent(hEventAlive);
		//		SetConsoleFontSizeTo(FarHwnd, 4, 6);
		//		MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...
		//		continue;
		//	}
		//	
		//	case (WAIT_OBJECT_0+CMD_LANGCHANGE):
		//	{
		//		// ���������� - �����������, ��������� �� ���������
		//		ResetEvent(hEventAlive);
		//		HKL hkl = (HKL)GetWindowLong(ConEmuHwnd, GWL_LANGCHANGE);
		//		if (hkl) {
		//			DWORD dwLastError = 0;
		//			WCHAR szLoc[10]; wsprintf(szLoc, L"%08x", (DWORD)(((DWORD)hkl) & 0xFFFF));
		//			HKL hkl1 = LoadKeyboardLayout(szLoc, KLF_ACTIVATE|KLF_REORDER|KLF_SUBSTITUTE_OK|KLF_SETFORPROCESS);
		//			HKL hkl2 = ActivateKeyboardLayout(hkl1, KLF_SETFORPROCESS|KLF_REORDER);
		//			if (!hkl2) {
		//				dwLastError = GetLastError();
		//			}
		//			dwLastError = dwLastError;
		//		}
		//		continue;
		//	}
		//	
		//	default:
		//	// ��� ��������� ������� ����� ��������� � ���� FAR'�
		//	ProcessCommand(dwWait/*nCmd*/, TRUE/*bReqMainThread*/, NULL);
		//}

		//// ������������� � �������� ������
		//EnterCriticalSection(&csData);
		//wsprintf(gszDir1, CONEMUMAPPING, dwProcId);
		//gnDataSize = gpData ? (gpCursor - gpData) : 0;
		//#ifdef _DEBUG
		//int nSize = gnDataSize; // ����-�� ��� ����������...
		//#endif
		//SetLastError(0);
		//ghMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, gnDataSize+4, gszDir1);
		//#ifdef _DEBUG
		//DWORD dwCreateError = GetLastError();
		//#endif
		//if (ghMapping) {
		//	LPBYTE lpMap = (LPBYTE)MapViewOfFile(ghMapping, FILE_MAP_ALL_ACCESS, 0,0,0);
		//	if (!lpMap) {
		//		// ������
		//		SafeCloseHandle(ghMapping);
		//	} else {
		//		// copy data
		//		if (gpData && gnDataSize) {
		//			*((DWORD*)lpMap) = gnDataSize+4;
		//			#ifdef _DEBUG
		//			LPBYTE dst=lpMap+4; LPBYTE src=gpData;
		//			for (DWORD n=gnDataSize;n>0;n--)
		//				*(dst++) = *(src++);
		//			#else
		//			memcpy(lpMap+4, gpData, gnDataSize);
		//			#endif
		//		} else {
		//			*((DWORD*)lpMap) = 0;
		//		}

		//		//unmaps a mapped view of a file from the calling process's address space
		//		UnmapViewOfFile(lpMap);
		//	}
		//}
		//if (gpData) {
		//	free(gpCmdRet);
		//	gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;
		//}
		//LeaveCriticalSection(&csData);

		// �������� ������, ��� ������ ��� � ��������
		//SetEvent(hEventReady);

		//Sleep(1);
	}


//closethread:
	// ��������� ��� ������ � �������
	//for (int i=0; i<MAXCMDCOUNT; i++)
	//	SafeCloseHandle(hEventCmd[i]);
	//SafeCloseHandle(hEventAlive);
	//SafeCloseHandle(hEventReady);
	//SafeCloseHandle(ghMapping);

	return 0;
}

void WINAPI _export SetStartupInfoW(void *aInfo)
{
	if (!gFarVersion.dwVerMajor) LoadFarVersion();

	if (gFarVersion.dwBuild>=789)
		SetStartupInfoW789(aInfo);
	else
		SetStartupInfoW757(aInfo);

	gbInfoW_OK = TRUE;

	CheckMacro(TRUE);
}

#define CREATEEVENT(fmt,h) \
		wsprintf(szEventName, fmt, dwCurProcId ); \
		h = CreateEvent(NULL, FALSE, FALSE, szEventName); \
		if (h==INVALID_HANDLE_VALUE) h=NULL;
	
void InitHWND(HWND ahFarHwnd)
{
	InitializeCriticalSection(&csTabs);
	InitializeCriticalSection(&csData);
	LoadFarVersion(); // ���������� ��� �����!

	// ��������� �������������. � SetStartupInfo ��������
	wsprintfW(gszRootKey, L"Software\\%s", (gFarVersion.dwVerMajor==2) ? L"FAR2" : L"FAR");

	ConEmuHwnd = NULL;
	FarHwnd = ahFarHwnd;

	//memset(hEventCmd, 0, sizeof(HANDLE)*MAXCMDCOUNT);
	
	//int nChk = 0;
	ConEmuHwnd = GetConEmuHWND ( FALSE/*abRoot*/  /*, &nChk*/ );

	gnMsgTabChanged = RegisterWindowMessage(CONEMUTABCHANGED);

	// ���� ���� �� �� � ConEmu - ��� ����� ��������� ����, �.�. � ConEmu ������ ���� ����������� /Attach!
	//WCHAR szEventName[128];
	DWORD dwCurProcId = GetCurrentProcessId();

	if (!ghReqCommandEvent) {
		ghReqCommandEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
		_ASSERTE(ghReqCommandEvent!=NULL);
	}

	// ��������� ������ ������
	wsprintf(gszPluginServerPipe, CEPLUGINPIPENAME, L".", dwCurProcId);
	ghServerTerminateEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
	_ASSERTE(ghServerTerminateEvent!=NULL);
	if (ghServerTerminateEvent) ResetEvent(ghServerTerminateEvent);
	ghPluginSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
    gnServerThreadId = 0;
    ghServerThread = CreateThread(NULL, 0, ServerThread, (LPVOID)NULL, 0, &gnServerThreadId);
    _ASSERTE(ghServerThread!=NULL);



	
	//CREATEEVENT(CONEMUDRAGFROM, hEventCmd[CMD_DRAGFROM]);
	//CREATEEVENT(CONEMUDRAGTO, hEventCmd[CMD_DRAGTO]);
	//CREATEEVENT(CONEMUREQTABS, hEventCmd[CMD_REQTABS]);
	//CREATEEVENT(CONEMUSETWINDOW, hEventCmd[CMD_SETWINDOW]);
	//CREATEEVENT(CONEMUPOSTMACRO, hEventCmd[CMD_POSTMACRO]);
	//CREATEEVENT(CONEMUDEFFONT, hEventCmd[CMD_DEFFONT]);
	//CREATEEVENT(CONEMULANGCHANGE, hEventCmd[CMD_LANGCHANGE]);
	//CREATEEVENT(CONEMUEXIT, hEventCmd[CMD_EXIT]);
	//CREATEEVENT(CONEMUALIVE, hEventAlive);
	//CREATEEVENT(CONEMUREADY, hEventReady);

	hThread=CreateThread(NULL, 0, &ThreadProcW, 0, 0, 0);



	// ���� �� �� ��� ���������� - ������ ������ ������ �� �����
	if (ConEmuHwnd) {
		//
		DWORD dwPID, dwThread;
		dwThread = GetWindowThreadProcessId(ConEmuHwnd, &dwPID);
		typedef BOOL (WINAPI* AllowSetForegroundWindowT)(DWORD);
		HMODULE hUser32 = GetModuleHandle(L"user32.dll");
		if (hUser32) {
			AllowSetForegroundWindowT AllowSetForegroundWindowF = (AllowSetForegroundWindowT)GetProcAddress(hUser32, "AllowSetForegroundWindow");
			if (AllowSetForegroundWindowF) AllowSetForegroundWindowF(dwPID);
		}
		// ������� ����, ���� ��� �����
		int tabCount = 0;
		CreateTabs(1);
		AddTab(tabCount, true, false, WTYPE_PANELS, NULL, NULL, 0, 0);
		SendTabs(tabCount=1);
	}
}

void NotifyChangeKey()
{
	if (ghRegMonitorKey) { RegCloseKey(ghRegMonitorKey); ghRegMonitorKey = NULL; }
	if (ghRegMonitorEvt) ResetEvent(ghRegMonitorEvt);
	
	WCHAR szKeyName[MAX_PATH*2];
	lstrcpyW(szKeyName, gszRootKey);
	lstrcatW(szKeyName, L"\\PluginHotkeys");
	// ����� ����� � �� ����, ���� �� ��� ������ ������� �� ���� ���������������� ������� �������
	if (0 == RegOpenKeyEx(HKEY_CURRENT_USER, szKeyName, 0, KEY_NOTIFY, &ghRegMonitorKey)) {
		if (!ghRegMonitorEvt) ghRegMonitorEvt = CreateEvent(NULL,FALSE,FALSE,NULL);
		RegNotifyChangeKeyValue(ghRegMonitorKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, ghRegMonitorEvt, TRUE);
		return;
	}
	// ���� �� ���� ��� - ������� ����������� � ��������� �����
	if (0 == RegOpenKeyEx(HKEY_CURRENT_USER, gszRootKey, 0, KEY_NOTIFY, &ghRegMonitorKey)) {
		if (!ghRegMonitorEvt) ghRegMonitorEvt = CreateEvent(NULL,FALSE,FALSE,NULL);
		RegNotifyChangeKeyValue(ghRegMonitorKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, ghRegMonitorEvt, TRUE);
		return;
	}
}

//abCompare=TRUE ���������� ����� �������� �������, ���� ���� ������� ������� �������...
BOOL CheckPlugKey()
{
	WCHAR cCurKey = gcPlugKey;
	gcPlugKey = 0;
	BOOL lbChanged = FALSE;
	HKEY hkey=NULL;
	WCHAR szMacroKey[2][MAX_PATH], szCheckKey[32];
	
	//��������� ����������� �������� �������, � ���� ��� ConEmu.dll ������� ������� ��������� - ��������� ��
	wsprintfW(szMacroKey[0], L"%s\\PluginHotkeys", gszRootKey/*, szCheckKey*/);
	if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[0], 0, KEY_READ, &hkey))
	{
		DWORD dwIndex = 0, dwSize; FILETIME ft;
		while (0==RegEnumKeyEx(hkey, dwIndex++, szMacroKey[1], &(dwSize=MAX_PATH), NULL, NULL, NULL, &ft)) {
			WCHAR* pszSlash = szMacroKey[1]+lstrlenW(szMacroKey[1])-1;
			while (pszSlash>szMacroKey[1] && *pszSlash!=L'/') pszSlash--;
			#if !defined(__GNUC__)
			#pragma warning(disable : 6400)
			#endif
			if (lstrcmpiW(pszSlash, L"/conemu.dll")==0) {
				WCHAR lsFullPath[MAX_PATH*2];
				lstrcpy(lsFullPath, szMacroKey[0]);
				lstrcat(lsFullPath, L"\\");
				lstrcat(lsFullPath, szMacroKey[1]);

				RegCloseKey(hkey); hkey=NULL;

				if (0==RegOpenKeyExW(HKEY_CURRENT_USER, lsFullPath, 0, KEY_READ, &hkey)) {
					dwSize = sizeof(szCheckKey);
					if (0==RegQueryValueExW(hkey, L"Hotkey", NULL, NULL, (LPBYTE)szCheckKey, &dwSize)) {
						gcPlugKey = szCheckKey[0];
					}
				}
				//
				//
				break;
			}
		}

		// ���������
		if (hkey) {RegCloseKey(hkey); hkey=NULL;}
	}
	
	lbChanged = (gcPlugKey != cCurKey);
	
	return lbChanged;
}

void CheckMacro(BOOL abAllowAPI)
{
	// � �� ��� ���������� ����� ��������� �������, ����� ����� ��������� �� ���������...
	//// ���� �� �� ��� ���������� - ������ ������ ������ �� �����
	//if (!ConEmuHwnd) return;


	// �������� ������� �������
	BOOL lbMacroAdded = FALSE, lbNeedMacro = FALSE;
	HKEY hkey=NULL;
	#define MODCOUNT 4
	int n;
	char szValue[1024];
	WCHAR szMacroKey[MODCOUNT][MAX_PATH], szCheckKey[32];
	DWORD dwSize = 0;
	//bool lbMacroDontCheck = false;

	//��������� ����������� �������� �������, � ���� ��� ConEmu.dll ������� ������� ��������� - ��������� ��
	CheckPlugKey();
	//wsprintfW(szMacroKey[0], L"%s\\PluginHotkeys",
	//		gszRootKey, szCheckKey);
	//if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[0], 0, KEY_READ, &hkey))
	//{
	//	DWORD dwIndex = 0, dwSize; FILETIME ft;
	//	while (0==RegEnumKeyEx(hkey, dwIndex++, szMacroKey[1], &(dwSize=MAX_PATH), NULL, NULL, NULL, &ft)) {
	//		WCHAR* pszSlash = szMacroKey[1]+lstrlenW(szMacroKey[1])-1;
	//		while (pszSlash>szMacroKey[1] && *pszSlash!=L'/') pszSlash--;
	//		if (lstrcmpiW(pszSlash, L"/conemu.dll")==0) {
	//			WCHAR lsFullPath[MAX_PATH*2];
	//			lstrcpy(lsFullPath, szMacroKey[0]);
	//			lstrcat(lsFullPath, L"\\");
	//			lstrcat(lsFullPath, szMacroKey[1]);
	//
	//			RegCloseKey(hkey); hkey=NULL;
	//
	//			if (0==RegOpenKeyExW(HKEY_CURRENT_USER, lsFullPath, 0, KEY_READ, &hkey)) {
	//				dwSize = sizeof(szCheckKey);
	//				if (0==RegQueryValueExW(hkey, L"Hotkey", NULL, NULL, (LPBYTE)szCheckKey, &dwSize)) {
	//					if (gFarVersion.dwVerMajor==1) {
	//						char cAnsi; // ����� �� �������� ������� � ����������� � WCHAR
	//						WideCharToMultiByte(CP_OEMCP, 0, szCheckKey, 1, &cAnsi, 1, 0,0);
	//						gcPlugKey = cAnsi;
	//					} else {
	//						gcPlugKey = szCheckKey[0];
	//					}
	//				}
	//			}
	//			//
	//			//
	//			break;
	//		}
	//	}
	//
	//	// ���������
	//	if (hkey) {RegCloseKey(hkey); hkey=NULL;}
	//}


	for (n=0; n<MODCOUNT; n++) {
		switch(n){
			case 0: lstrcpyW(szCheckKey, L"F14"); break;
			case 1: lstrcpyW(szCheckKey, L"CtrlF14"); break;
			case 2: lstrcpyW(szCheckKey, L"AltF14"); break;
			case 3: lstrcpyW(szCheckKey, L"ShiftF14"); break;
		}
		wsprintfW(szMacroKey[n], L"%s\\KeyMacros\\Common\\%s", gszRootKey, szCheckKey);
	}
	//lstrcpyA(szCheckKey, "F14DontCheck2");

	//if (0==RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ConEmu", 0, KEY_ALL_ACCESS, &hkey))
	//{
	//
	//	if (RegQueryValueExA(hkey, szCheckKey, 0, 0, (LPBYTE)&lbMacroDontCheck, &(dwSize=sizeof(lbMacroDontCheck))))
	//		lbMacroDontCheck = false;
	//	RegCloseKey(hkey); hkey=NULL;
	//}

	/*if (gFarVersion.dwVerMajor==1) {
		lstrcpyA((char*)szCheckKey, "F11  ");
		((char*)szCheckKey)[4] = (char)((gcPlugKey ? gcPlugKey : 0xCC) & 0xFF);
		//lstrcpyW((wchar_t*)szCheckKey, L"F11  ");
		//szCheckKey[4] = (wchar_t)(gcPlugKey ? gcPlugKey : 0xCC);
	} else {*/
	lstrcpyW((wchar_t*)szCheckKey, L"F11  "); //TODO: ��� ANSI ����� ������ ��� �� ���������?
	szCheckKey[4] = (wchar_t)(gcPlugKey ? gcPlugKey : ((gFarVersion.dwVerMajor==1) ? 0x42C/*0xDC - ������ ��� OEM*/ : 0x2584));
	//}

	//if (!lbMacroDontCheck)
	for (n=0; n<MODCOUNT && !lbNeedMacro; n++)
	{
		if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[n], 0, KEY_READ, &hkey))
		{
			/*if (gFarVersion.dwVerMajor==1) {
				if (0!=RegQueryValueExA(hkey, "Sequence", 0, 0, (LPBYTE)szValue, &(dwSize=1022))) {
					lbNeedMacro = TRUE; // �������� ����������
				} else {
					lbNeedMacro = lstrcmpA(szValue, (char*)szCheckKey)!=0;
				}
			} else {*/
				if (0!=RegQueryValueExW(hkey, L"Sequence", 0, 0, (LPBYTE)szValue, &(dwSize=1022))) {
					lbNeedMacro = TRUE; // �������� ����������
				} else {
					//TODO: ���������, ��� ���� ����� VC & GCC �� 2� �������� ��������?
					lbNeedMacro = lstrcmpW((WCHAR*)szValue, szCheckKey)!=0;
				}
			//}
			//	szValue[dwSize]=0;
			//	#pragma message("ERROR: ����� ��������. � Ansi � Unicode ��� ������ ������!")
			//	//if (strcmpW(szValue, "F11 \xCC")==0)
			//		lbNeedMacro = TRUE; // �������� ������������
			//}
			RegCloseKey(hkey); hkey=NULL;
		} else {
			lbNeedMacro = TRUE;
		}
	}

	if (lbNeedMacro) {
		//int nBtn = ShowMessage(0, 3);
		//if (nBtn == 1) { // Don't disturb
		//	DWORD disp=0;
		//	lbMacroDontCheck = true;
		//	if (0==RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ConEmu", 0, 0, 
		//		0, KEY_ALL_ACCESS, 0, &hkey, &disp))
		//	{
		//		RegSetValueExA(hkey, szCheckKey, 0, REG_BINARY, (LPBYTE)&lbMacroDontCheck, (dwSize=sizeof(lbMacroDontCheck)));
		//		RegCloseKey(hkey); hkey=NULL;
		//	}
		//} else if (nBtn == 0) 
		for (n=0; n<MODCOUNT; n++)
		{
			DWORD disp=0;
			lbMacroAdded = TRUE;
			if (0==RegCreateKeyExW(HKEY_CURRENT_USER, szMacroKey[n], 0, 0, 
				0, KEY_ALL_ACCESS, 0, &hkey, &disp))
			{
				lstrcpyA(szValue, "ConEmu support");
				RegSetValueExA(hkey, "", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));

				//lstrcpyA(szValue, 
				//	"$If (Shell || Info || QView || Tree || Viewer || Editor) F12 $Else waitkey(100) $End");
				//RegSetValueExA(hkey, "Sequence", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));
				
				/*if (gFarVersion.dwVerMajor==1) {
					RegSetValueExA(hkey, "Sequence", 0, REG_SZ, (LPBYTE)szCheckKey, (dwSize=strlen((char*)szCheckKey)+1));
				} else {*/
					RegSetValueExW(hkey, L"Sequence", 0, REG_SZ, (LPBYTE)szCheckKey, (dwSize=2*(lstrlenW((WCHAR*)szCheckKey)+1)));
				//}

				lstrcpyA(szValue, "For ConEmu - plugin activation from listening thread");
				RegSetValueExA(hkey, "Description", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));

				RegSetValueExA(hkey, "DisableOutput", 0, REG_DWORD, (LPBYTE)&(disp=1), (dwSize=4));

				RegCloseKey(hkey); hkey=NULL;
			}
		}
	}


	// ���������� ������� � FAR?
	if (lbMacroAdded && abAllowAPI) {
		if (gFarVersion.dwVerMajor==1)
			ReloadMacroA();
		else if (gFarVersion.dwBuild>=789)
			ReloadMacro789();
		else
			ReloadMacro757();
	}

	// First call
	if (abAllowAPI) {
		NotifyChangeKey();
	}
}

void UpdateConEmuTabsW(int event, bool losingFocus, bool editorSave)
{
	if (gFarVersion.dwBuild>=789)
		UpdateConEmuTabsW789(event, losingFocus, editorSave);
	else
		UpdateConEmuTabsW757(event, losingFocus, editorSave);
}

BOOL CreateTabs(int windowCount)
{
	EnterCriticalSection(&csTabs);

	if ((tabs==NULL) || (maxTabCount <= (windowCount + 1)))
	{
		maxTabCount = windowCount + 10; // � �������
		if (tabs) {
			free(tabs); tabs = NULL;
		}
		
		tabs = (ConEmuTab*) calloc(maxTabCount, sizeof(ConEmuTab));
	}
	
	lastWindowCount = windowCount;
	
	if (!tabs)
		LeaveCriticalSection(&csTabs);
	return tabs!=NULL;
}


	#ifndef max
	#define max(a,b)            (((a) > (b)) ? (a) : (b))
	#endif

	#ifndef min
	#define min(a,b)            (((a) < (b)) ? (a) : (b))
	#endif

BOOL AddTab(int &tabCount, bool losingFocus, bool editorSave, 
			int Type, LPCWSTR Name, LPCWSTR FileName, int Current, int Modified)
{
    BOOL lbCh = FALSE;
	OutputDebugString(L"--AddTab\n");
    
	if (Type == WTYPE_PANELS) {
	    lbCh = (tabs[0].Current != (losingFocus ? 1 : 0)) ||
	           (tabs[0].Type != WTYPE_PANELS);
		tabs[0].Current = losingFocus ? 1 : 0;
		//lstrcpyn(tabs[0].Name, GetMsgW789(0), CONEMUTABMAX-1);
		tabs[0].Name[0] = 0;
		tabs[0].Pos = 0;
		tabs[0].Type = WTYPE_PANELS;
	} else
	if (Type == WTYPE_EDITOR || Type == WTYPE_VIEWER)
	{
		// when receiving saving event receiver is still reported as modified
		if (editorSave && lstrcmpi(FileName, Name) == 0)
			Modified = 0;
	
	    lbCh = (tabs[tabCount].Current != (losingFocus ? 0 : Current)) ||
	           (tabs[tabCount].Type != Type) ||
	           (tabs[tabCount].Modified != Modified) ||
	           (lstrcmp(tabs[tabCount].Name, Name) != 0);
	
		// when receiving losing focus event receiver is still reported as current
		tabs[tabCount].Type = Type;
		tabs[tabCount].Current = losingFocus ? 0 : Current;
		tabs[tabCount].Modified = Modified;

		if (tabs[tabCount].Current != 0)
		{
			lastModifiedStateW = Modified != 0 ? 1 : 0;
		}
		else
		{
			lastModifiedStateW = -1;
		}

		int nLen = min(lstrlen(Name),(CONEMUTABMAX-1));
		lstrcpyn(tabs[tabCount].Name, Name, nLen+1);
		tabs[tabCount].Name[nLen]=0;

		tabs[tabCount].Pos = tabCount;
		tabCount++;
	}
	
	return lbCh;
}

void SendTabs(int tabCount, BOOL abFillDataOnly/*=FALSE*/)
{
	//if (abWritePipe) { //2009-06-01 ������ ����� ������ ����������� �����...
	EnterCriticalSection(&csTabs);
	BOOL bReleased = FALSE;
	//}
#ifdef _DEBUG
	WCHAR szDbg[100]; wsprintf(szDbg, L"-SendTabs(%i,%s), prev=%i\n", tabCount, 
		abFillDataOnly ? L"FillDataOnly" : L"Post", gnCurTabCount);
	OUTPUTDEBUGSTRING(szDbg);
#endif
	if (tabs 
		&& (abFillDataOnly || (ConEmuHwnd && IsWindow(ConEmuHwnd)))
		)
	{
		COPYDATASTRUCT cds;
		if (tabs[0].Type == WTYPE_PANELS) {
			cds.dwData = tabCount;
			cds.lpData = tabs;
		} else {
			// ������� ��� - ��� ��� ������ � ������ ���������!
			cds.dwData = --tabCount;
			cds.lpData = tabs+1;
		}
		// ���� abFillDataOnly - ������ ���������������� ��� ������ � Pipe - ����� ��������� �������� ������ ����

		cds.cbData = cds.dwData * sizeof(ConEmuTab);
		EnterCriticalSection(&csData);
		OutDataAlloc(sizeof(cds.dwData) + cds.cbData);
		OutDataWrite(&cds.dwData, sizeof(cds.dwData));
		OutDataWrite(cds.lpData, cds.cbData);
		LeaveCriticalSection(&csData);

		LeaveCriticalSection(&csTabs); bReleased = TRUE;

		//TODO: ��������, ��� ��� ������������ ���� �������� �� ������ ����� ����� ��������� ����?
		_ASSERT(gpCmdRet!=NULL);
		if (!abFillDataOnly && tabCount && gnReqCommand==(DWORD)-1) {
			//cds.cbData = tabCount * sizeof(ConEmuTab);
			//SendMessage(ConEmuHwnd, WM_COPYDATA, (WPARAM)FarHwnd, (LPARAM)&cds);
			// ��� ����� ������ ������ ���� ������������ �����. ���� ������ ������� ConEmu - �� ��������...
			if (gnCurTabCount != tabCount || tabCount > 1) {
				gnCurTabCount = tabCount; // ����� ��������!, � �� ��� ������� ����� ���������� ��� ������ �����...
				//PostMessage(ConEmuHwnd, gnMsgTabChanged, tabCount, 0);
				gpCmdRet->nCmd = CECMD_TABSCHANGED;
				CESERVER_REQ* pOut =
					ExecuteGuiCmd(FarHwnd, gpCmdRet);
				if (pOut) free(pOut);
			}
		}
	}
	//free(tabs); - ������������� � ExitFARW
    gnCurTabCount = tabCount;
	if (!bReleased) {
		LeaveCriticalSection(&csTabs); bReleased = TRUE;
	}
}

// watch non-modified -> modified editor status change

int lastModifiedStateW = -1;

int WINAPI _export ProcessEditorInputW(void* Rec)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	if (gFarVersion.dwBuild>=789)
		return ProcessEditorInputW789((LPCVOID)Rec);
	else
		return ProcessEditorInputW757((LPCVOID)Rec);
}

int WINAPI _export ProcessEditorEventW(int Event, void *Param)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	/*if (gFarVersion.dwBuild>=789)
		return ProcessEditorEventW789(Event,Param);
	else
		return ProcessEditorEventW757(Event,Param);*/
	// ����� ���� ������� �� �����������, �� � �� ANSI �� ����������...
	switch (Event)
	{
	case EE_CLOSE:
		OUTPUTDEBUGSTRING(L"EE_CLOSE"); break;
	case EE_GOTFOCUS:
		OUTPUTDEBUGSTRING(L"EE_GOTFOCUS"); break;
	case EE_KILLFOCUS:
		OUTPUTDEBUGSTRING(L"EE_KILLFOCUS"); break;
	case EE_SAVE:
		OUTPUTDEBUGSTRING(L"EE_SAVE"); break;
	//case EE_READ: -- � ���� ������ ���������� ���� ��� �� ����������
	default:
		return 0;
	}
	// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
	//2009-06-03 EE_KILLFOCUS ��� �������� ��������� �� ��������. ������ EE_CLOSE
	UpdateConEmuTabsW(Event, (Event == EE_KILLFOCUS || Event == EE_CLOSE), Event == EE_SAVE);
	return 0;
}

int WINAPI _export ProcessViewerEventW(int Event, void *Param)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	/*if (gFarVersion.dwBuild>=789)
		return ProcessViewerEventW789(Event,Param);
	else
		return ProcessViewerEventW757(Event,Param);*/
	// ����� ���� ������� �� �����������, �� � �� ANSI �� ����������...
	switch (Event)
	{
	case VE_CLOSE:
		OUTPUTDEBUGSTRING(L"VE_CLOSE"); break;
	//case VE_READ:
	//	OUTPUTDEBUGSTRING(L"VE_CLOSE"); break;
	case VE_KILLFOCUS:
		OUTPUTDEBUGSTRING(L"VE_KILLFOCUS"); break;
	case VE_GOTFOCUS:
		OUTPUTDEBUGSTRING(L"VE_GOTFOCUS"); break;
	default:
		return 0;
	}
	// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
	//2009-06-03 VE_KILLFOCUS ��� �������� ��������� �� ��������. ������ VE_CLOSE
	UpdateConEmuTabsW(Event, (Event == VE_KILLFOCUS || Event == VE_CLOSE), false);
	return 0;
}

void StopThread(void)
{
	//if (hEventCmd[CMD_EXIT])
	//	SetEvent(hEventCmd[CMD_EXIT]); // ��������� ����

	if (ghServerTerminateEvent) {
		SetEvent(ghServerTerminateEvent);
	}

	if (ghServerThread) {
		HANDLE hPipe = INVALID_HANDLE_VALUE;
		DWORD dwWait = 0;
		// ����������� ����, ����� ���� ������� �����������
		OutputDebugString(L"Plugin: Touching our server pipe\n");
		hPipe = CreateFile(gszPluginServerPipe,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
		if (hPipe == INVALID_HANDLE_VALUE) {
			OutputDebugString(L"Plugin: All pipe instances closed?\n");
		} else {
			OutputDebugString(L"Plugin: Waiting server pipe thread\n");
			dwWait = WaitForSingleObject(ghServerThread, 300); // �������� ���������, ���� ���� ����������
			// ������ ������� ���� - ��� ����� ���� �����������
			CloseHandle(hPipe);
			hPipe = INVALID_HANDLE_VALUE;
		}
		dwWait = WaitForSingleObject(ghServerThread, 0);
		if (dwWait != WAIT_OBJECT_0) {
			#if !defined(__GNUC__)
			#pragma warning (disable : 6258)
			#endif
			TerminateThread(ghServerThread, 100);
		}
		SafeCloseHandle(ghServerThread);
	}
	SafeCloseHandle(ghPluginSemaphore);

	//CloseTabs(); -- ConEmu ���� ����������
	if (hThread) { // �������� ����-����, ��� ������������� ������� ���� ��������
		if (WaitForSingleObject(hThread,1000)) {
			#if !defined(__GNUC__)
			#pragma warning (disable : 6258)
			#endif
			TerminateThread(hThread, 100);
		}
		CloseHandle(hThread); hThread = NULL;
	}
	
    if (tabs) {
	    free(tabs);
	    tabs = NULL;
    }
    
    if (ghReqCommandEvent) {
	    CloseHandle(ghReqCommandEvent); ghReqCommandEvent = NULL;
    }

	DeleteCriticalSection(&csTabs); memset(&csTabs,0,sizeof(csTabs));
	DeleteCriticalSection(&csData); memset(&csData,0,sizeof(csData));
	
	if (ghRegMonitorKey) { RegCloseKey(ghRegMonitorKey); ghRegMonitorKey = NULL; }
	SafeCloseHandle(ghRegMonitorEvt);
	SafeCloseHandle(ghServerTerminateEvent);
}

void   WINAPI _export ExitFARW(void)
{
	StopThread();

	if (gFarVersion.dwBuild>=789)
		ExitFARW789();
	else
		ExitFARW757();
}

void CloseTabs()
{
	if (ConEmuHwnd && IsWindow(ConEmuHwnd)) {
		COPYDATASTRUCT cds;
		cds.dwData = 0;
		cds.lpData = &cds.dwData;
		cds.cbData = sizeof(cds.dwData);
		SendMessage(ConEmuHwnd, WM_COPYDATA, (WPARAM)FarHwnd, (LPARAM)&cds);
	}
}

// ���� �� �������� - ����� ������������� �������������. ������ � ������
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataAlloc(DWORD anSize)
{
	// + ������ ��������� gpCmdRet
	gpCmdRet = (CESERVER_REQ*)calloc(12+anSize,1);
	if (!gpCmdRet)
		return FALSE;

	gpCmdRet->nSize = anSize+12;
	gpCmdRet->nVersion = CESERVER_REQ_VER;

	gpData = gpCmdRet->Data;
	gnDataSize = anSize;
	gpCursor = gpData;

	return TRUE;
}

// ������ � ������. ���������� ������������� �� OutDataWrite
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataRealloc(DWORD anNewSize)
{
	if (!gpCmdRet)
		return OutDataAlloc(anNewSize);

	if (anNewSize < gnDataSize)
		return FALSE; // ������ �������� ������ ������, ��� ��� ����

	// realloc ������ �� ��������, ��� ��� ���� � �� ��������
	CESERVER_REQ* lpNewCmdRet = (CESERVER_REQ*)calloc(12+anNewSize,1);
	if (!lpNewCmdRet)
		return FALSE;
	lpNewCmdRet->nCmd = gpCmdRet->nCmd;
	lpNewCmdRet->nSize = anNewSize+12;
	lpNewCmdRet->nVersion = gpCmdRet->nVersion;

	LPBYTE lpNewData = lpNewCmdRet->Data;
	if (!lpNewData)
		return FALSE;

	// ����������� ������������ ������
	memcpy(lpNewData, gpData, gnDataSize);
	// ��������� ����� ������� �������
	gpCursor = lpNewData + (gpCursor - gpData);
	// � ����� ����� � ��������
	free(gpCmdRet);
	gpCmdRet = lpNewCmdRet;
	gpData = lpNewData;
	gnDataSize = anNewSize;

	return TRUE;
}

// ������ � ������
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataWrite(LPVOID apData, DWORD anSize)
{
	if (!gpData) {
		if (!OutDataAlloc(max(1024, (anSize+128))))
			return FALSE;
	} else if (((gpCursor-gpData)+anSize)>gnDataSize) {
		if (!OutDataRealloc(gnDataSize+max(1024, (anSize+128))))
			return FALSE;
	}

	// ����������� ������
	memcpy(gpCursor, apData, anSize);
	gpCursor += anSize;

	return TRUE;
}

int ShowMessage(int aiMsg, int aiButtons)
{
	if (gFarVersion.dwVerMajor==1)
		return ShowMessageA(aiMsg, aiButtons);
	else if (gFarVersion.dwBuild>=789)
		return ShowMessage789(aiMsg, aiButtons);
	else
		return ShowMessage757(aiMsg, aiButtons);
}

LPCWSTR GetMsgW(int aiMsg)
{
	if (gFarVersion.dwVerMajor==1)
		return L"";
	else if (gFarVersion.dwBuild>=789)
		return GetMsg789(aiMsg);
	else
		return GetMsg757(aiMsg);
}

void PostMacro(wchar_t* asMacro)
{
	if (!asMacro || !*asMacro)
		return;
		
	if (gFarVersion.dwVerMajor==1) {
		int nLen = lstrlenW(asMacro);
		char* pszMacro = (char*)calloc(nLen+1,1);
		if (pszMacro) {
			WideCharToMultiByte(CP_OEMCP,0,asMacro,nLen+1,pszMacro,nLen+1,0,0);
			PostMacroA(pszMacro);
			free(pszMacro);
		}
	} else if (gFarVersion.dwBuild>=789) {
		PostMacro789(asMacro);
	} else {
		PostMacro757(asMacro);
	}
}

DWORD WINAPI ServerThread(LPVOID lpvParam) 
{ 
    BOOL fConnected = FALSE;
    DWORD dwErr = 0;
    HANDLE hPipe = NULL; 
    //HANDLE hWait[2] = {NULL,NULL};
	//DWORD dwTID = GetCurrentThreadId();
	std::vector<HANDLE>::iterator iter;

    _ASSERTE(gszPluginServerPipe[0]!=0);
    //_ASSERTE(ghServerSemaphore!=NULL);

    // The main loop creates an instance of the named pipe and 
    // then waits for a client to connect to it. When the client 
    // connects, a thread is created to handle communications 
    // with that client, and the loop is repeated. 
    
    //hWait[0] = ghServerTerminateEvent;
    //hWait[1] = ghServerSemaphore;

    // ���� �� ����������� ���������� �������
    do {
		fConnected = FALSE; // ����� ����
        while (!fConnected)
        { 
            _ASSERTE(hPipe == NULL);

			// ���������, ����� �����-�� ��������� ���� ��� �����������
			iter = ghCommandThreads.begin();
			while (iter != ghCommandThreads.end()) {
				HANDLE hThread = *iter; dwErr = 0;
				if (WaitForSingleObject(hThread, 0) == WAIT_OBJECT_0) {
					CloseHandle ( hThread );
					iter = ghCommandThreads.erase(iter);
				} else {
					iter++;
				}
			}

            //// ��������� ���������� ��������, ��� �������� �������
            //dwErr = WaitForMultipleObjects ( 2, hWait, FALSE, INFINITE );
            //if (dwErr == WAIT_OBJECT_0) {
            //    return 0; // ������� �����������
            //}

			//for (int i=0; i<MAX_SERVER_THREADS; i++) {
			//	if (gnServerThreadsId[i] == dwTID) {
			//		ghActiveServerThread = ghServerThreads[i]; break;
			//	}
			//}

            hPipe = CreateNamedPipe( gszPluginServerPipe, PIPE_ACCESS_DUPLEX, 
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
                PIPEBUFSIZE, PIPEBUFSIZE, 0, NULL);

            _ASSERTE(hPipe != INVALID_HANDLE_VALUE);

            if (hPipe == INVALID_HANDLE_VALUE) 
            {
                //DisplayLastError(L"CreatePipe failed"); 
                hPipe = NULL;
                Sleep(50);
                continue;
            }

            // Wait for the client to connect; if it succeeds, 
            // the function returns a nonzero value. If the function
            // returns zero, GetLastError returns ERROR_PIPE_CONNECTED. 

            fConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : ((dwErr = GetLastError()) == ERROR_PIPE_CONNECTED); 

            // ������� �����������!
            if (WaitForSingleObject ( ghServerTerminateEvent, 0 ) == WAIT_OBJECT_0) {
                //FlushFileBuffers(hPipe); -- ��� �� �����, �� ������ �� ����������
                //DisconnectNamedPipe(hPipe); 
				//ReleaseSemaphore(ghServerSemaphore, 1, NULL);
                SafeCloseHandle(hPipe);
                goto wrap;
            }

            if (!fConnected)
                SafeCloseHandle(hPipe);
        }

        if (fConnected) {
            // ����� �������, ����� �� ������
            //fConnected = FALSE;
            // ��������� ������ ���� ������� �����
            //ReleaseSemaphore(ghServerSemaphore, 1, NULL);
            
            //ServerThreadCommand ( hPipe ); // ��� ������������� - ���������� � ���� ��������� ����
			DWORD dwThread = 0;
			HANDLE hThread = CreateThread(NULL, 0, ServerThreadCommand, (LPVOID)hPipe, 0, &dwThread);
			_ASSERTE(hThread!=NULL);
			if (hThread==NULL) {
				// ��� �� ������� ��������� ���� - ����� ����������� ��� ����������...
				ServerThreadCommand((LPVOID)hPipe);
			} else {
				ghCommandThreads.push_back ( hThread );
			}
			hPipe = NULL; // ��������� ServerThreadCommand
        }

		if (hPipe) {
			if (hPipe != INVALID_HANDLE_VALUE) {
				FlushFileBuffers(hPipe); 
				//DisconnectNamedPipe(hPipe); 
				CloseHandle(hPipe);
			}
			hPipe = NULL;
		}
    } // ������� � �������� ������ instance �����
    while (WaitForSingleObject ( ghServerTerminateEvent, 0 ) != WAIT_OBJECT_0);

wrap:
	// ���������� ���� ���������� �����
	iter = ghCommandThreads.begin();
	while (iter != ghCommandThreads.end()) {
		HANDLE hThread = *iter; dwErr = 0;
		if (WaitForSingleObject(hThread, 100) != WAIT_OBJECT_0) {
			TerminateThread(hThread, 100);
		}
		CloseHandle ( hThread );
		iter = ghCommandThreads.erase(iter);
	}

    return 0; 
}

DWORD WINAPI ServerThreadCommand(LPVOID ahPipe)
{
	HANDLE hPipe = (HANDLE)ahPipe;
    CESERVER_REQ *pIn=NULL;
	BYTE cbBuffer[64]; // ��� ������� ����� ������ ��� ������
    DWORD cbRead = 0, cbWritten = 0, dwErr = 0;
    BOOL fSuccess = FALSE;

    // Send a message to the pipe server and read the response. 
    fSuccess = ReadFile( hPipe, cbBuffer, sizeof(cbBuffer), &cbRead, NULL);
	dwErr = GetLastError();

    if (!fSuccess && (dwErr != ERROR_MORE_DATA)) 
    {
        _ASSERTE("ReadFile(pipe) failed"==NULL);
        CloseHandle(hPipe);
        return 0;
    }
	pIn = (CESERVER_REQ*)cbBuffer; // ���� cast, ���� ����� ������ - ������� ������
    _ASSERTE(pIn->nSize>=12 && cbRead>=12);
    _ASSERTE(pIn->nVersion == CESERVER_REQ_VER);
    if (cbRead < 12 || /*in.nSize < cbRead ||*/ pIn->nVersion != CESERVER_REQ_VER) {
        CloseHandle(hPipe);
        return 0;
    }

    int nAllSize = pIn->nSize;
    pIn = (CESERVER_REQ*)calloc(nAllSize,1);
    _ASSERTE(pIn!=NULL);
	if (!pIn) {
		CloseHandle(hPipe);
		return 0;
	}
    memmove(pIn, cbBuffer, cbRead);
    _ASSERTE(pIn->nVersion==CESERVER_REQ_VER);

    LPBYTE ptrData = ((LPBYTE)pIn)+cbRead;
    nAllSize -= cbRead;

    while(nAllSize>0)
    { 
        //_tprintf(TEXT("%s\n"), chReadBuf);

        // Break if TransactNamedPipe or ReadFile is successful
        if(fSuccess)
            break;

        // Read from the pipe if there is more data in the message.
        fSuccess = ReadFile( 
            hPipe,      // pipe handle 
            ptrData,    // buffer to receive reply 
            nAllSize,   // size of buffer 
            &cbRead,    // number of bytes read 
            NULL);      // not overlapped 

        // Exit if an error other than ERROR_MORE_DATA occurs.
        if( !fSuccess && ((dwErr = GetLastError()) != ERROR_MORE_DATA)) 
            break;
        ptrData += cbRead;
        nAllSize -= cbRead;
    }

    TODO("����� ���������� ASSERT, ���� ������� ���� ������� � �������� ������");
    _ASSERTE(nAllSize==0);
    if (nAllSize>0) {
		if (((LPVOID)cbBuffer) != ((LPVOID)pIn))
			free(pIn);
        CloseHandle(hPipe);
        return 0; // ������� ������� �� ��� ������
    }

    #ifdef _DEBUG
	UINT nDataSize = pIn->nSize - sizeof(CESERVER_REQ) + 1;
	#endif

    // ��� ������ �� ����� ��������, ������������ ������� � ���������� (���� �����) ���������
	//fSuccess = WriteFile( hPipe, pOut, pOut->nSize, &cbWritten, NULL);

	if (pIn->nCmd == CMD_LANGCHANGE) {
		_ASSERTE(nDataSize>=4);
		HKL hkl = 0;
		memmove(&hkl, pIn->Data, 4);
		if (hkl) {
			DWORD dwLastError = 0;
			WCHAR szLoc[10]; wsprintf(szLoc, L"%08x", (DWORD)(((DWORD)hkl) & 0xFFFF));
			HKL hkl1 = LoadKeyboardLayout(szLoc, KLF_ACTIVATE|KLF_REORDER|KLF_SUBSTITUTE_OK|KLF_SETFORPROCESS);
			HKL hkl2 = ActivateKeyboardLayout(hkl1, KLF_SETFORPROCESS|KLF_REORDER);
			if (!hkl2) {
				dwLastError = GetLastError();
			}
			dwLastError = dwLastError;
		}

	} else if (pIn->nCmd == CMD_DEFFONT) {
		// ���������� - �����������, ��������� �� ���������
		SetConsoleFontSizeTo(FarHwnd, 4, 6);
		MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...

	} else if (pIn->nCmd == CMD_REQTABS) {
		SendTabs(gnCurTabCount, TRUE);

	} else {
		ProcessCommand(pIn->nCmd, TRUE/*bReqMainThread*/, pIn->Data);

		if (gpCmdRet) {
			fSuccess = WriteFile( hPipe, gpCmdRet, gpCmdRet->nSize, &cbWritten, NULL);
			free(gpCmdRet);
			gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;
		}
	}


    // ���������� ������
	if (((LPVOID)cbBuffer) != ((LPVOID)pIn))
		free(pIn);

    CloseHandle(hPipe);
    return 0;
}
