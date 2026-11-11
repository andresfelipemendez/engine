#define export __declspec(dllexport)

#include <stdio.h>

export void loop() { 
	printf("hello from dll");
}
