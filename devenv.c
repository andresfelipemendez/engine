#include<stdio.h>
#include <windows.h>

typedef void(*LOOP)();

int main() {
	printf("%s\n", "hi");

	HMODULE lib;
	lib = LoadLibraryA("devenvlib");
	if(lib==NULL){
		printf("didn't find the lib");
	}
	LOOP loopfn;
	loopfn = (LOOP)GetProcAddress(lib,"loop");
	loopfn();
}
