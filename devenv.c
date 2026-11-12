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

	

	printf("compiling...\n");
	int r1 = system("call \"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" && cl build.c && cl devenvlib.c /LD");
	printf("compile returned: %d\n", r1);
	int r2 = system("build.exe");
	printf("build.exe returned: %d\n", r2);

	// int r3 = system("cl devenvlib.c /LD");
	// printf("compile devenvlib returned: %d\n", r2);

	HMODULE lib;
	lib = LoadLibraryA("devenvlib");
	if(lib==NULL){
		printf("didn't find the lib");
		return 1;
	}
	LOOP loopfn;
	loopfn = (LOOP)GetProcAddress(lib,"loop");
	loopfn();

	// HANDLE libFile;
	// libFile = CreateFileA(
	// 	"devenvlib.dll",		//lpFileName
	// 	GENERIC_READ,			//dwDesiredAccess
	// 	FILE_SHARE_DELETE|FILE_SHARE_READ|FILE_SHARE_WRITE,//dwShareMode
	// 	NULL,					//lpSecurityAttributes
	// 	OPEN_EXISTING,			//dwCreationDisposition
	// 	FILE_ATTRIBUTE_NORMAL,	//dwFlagsAndAttributes
	// 	NULL 					//hTemplateFile
	// 	);
	// if (libFile == INVALID_HANDLE_VALUE) {
	// 	//GetLastError();
	// 	printf("didn't open the lib");
	// 	ErrorExit();
	// }



}
