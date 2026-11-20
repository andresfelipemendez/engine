#include<stdio.h>
#include <stdlib.h>

#include "platform.h"

typedef void(*LOOP)(void);

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

	Error err;
	PlatformFile dll_file;
	if(!open_file(S("devenvlib.dll"), &dll_file, &err)) {
		printf("couldn't open file: %s\n", err.message);
		return 1;
	}

	FileTimeStamp now;
	if(!get_file_time(&dll_file, &now, &err)){
		printf("get_file_tile failed: %s\n", err.message);
		close_file(&dll_file, &err);
		return 1;
	}
	close_file(&dll_file, &err);

	PlatformLibrary lib;
	if(load_library("devenvlib", &lib,	&err)){
		printf("didn't find the lib: %s\n", err.message);
	};



	// LOOP loopfn;
	// loopfn = (LOOP)GetProcAddress(lib.native.win_module,"loop");
	// loopfn();

}
