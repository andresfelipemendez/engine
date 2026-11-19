#include<stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
typedef void(*LOOP)();

#if _MSC_VER
#include "msvc.c"
#else
#endif

int main() {

	load_env();

	printf("compiling...\n");
	int r1 = system("cl build.c");
	printf("compile returned: %d\n", r1);
	int r2 = system("cl devenvlib.c /LD");
	printf("compile devenvlib returned: %d\n", r2);
	int r3 = system("build.exe");
	printf("build.exe returned: %d\n", r3);

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
