#include<stdio.h>
#include <windows.h>

typedef void(*LOOP)();


void ErrorExit() 
{ 
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    DWORD dw = GetLastError(); 

    if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL) == 0) {
    	printf("FormatMessage failed");

        // MessageBox(NULL, TEXT("FormatMessage failed"), TEXT("Error"), MB_OK);
        ExitProcess(dw);
    }
	printf("error %s\n",(LPCTSTR)lpMsgBuf);
    // MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    ExitProcess(dw); 
}

int main() {
	printf("%s\n", "hi");

	HMODULE lib;
	lib = LoadLibraryA("devenvlib");
	if(lib==NULL){
		printf("didn't find the lib");
		return;
	}
	LOOP loopfn;
	loopfn = (LOOP)GetProcAddress(lib,"loop");
	loopfn();

	HANDLE libFile;
	libFile = CreateFileA(
		"devenvlib.dll",		//lpFileName
		GENERIC_READ,			//dwDesiredAccess
		FILE_SHARE_DELETE|FILE_SHARE_READ|FILE_SHARE_WRITE,//dwShareMode
		NULL,					//lpSecurityAttributes
		OPEN_EXISTING,			//dwCreationDisposition
		FILE_ATTRIBUTE_NORMAL,	//dwFlagsAndAttributes
		NULL 					//hTemplateFile
		);
	if (libFile == INVALID_HANDLE_VALUE) {
		//GetLastError();
		printf("didn't open the lib");
		ErrorExit();
	}
}
