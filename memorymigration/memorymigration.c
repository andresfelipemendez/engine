#include "memorymigration.h"
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void parse(const wchar_t* path) {
	SourceBuffer sb = {0};
	if (!load_source(path, &sb)) {
		printf("the file was not opened");
		return;
	}

}

bool load_source(const wchar_t* path, SourceBuffer* out) {
	*out = (SourceBuffer){ 0 };
	int fd = _wopen(path, _O_RDONLY | _O_BINARY);
	if (fd == -1)
		return false;

	struct __stat64 info;
	if (_fstat64(fd, &info) != 0 || 
		info.st_size < 0 || 
		(uintmax_t)info.st_size >= SIZE_MAX) {
		_close(fd);
		return false;
	}

	size_t length = (size_t)info.st_size;
	char* data = VirtualAlloc(NULL, length + 1,
		MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	if (data == NULL) {
		_close(fd);
		return false;
	}

	size_t offset = 0;
	while (offset < length) {
		size_t remaining = length - offset;
		unsigned int chunk = remaining > 1024 * 1024
			? 1024 * 1024
			: (unsigned int)remaining;
		int count = _read(fd, data + offset, chunk);
		if (count <= 0) {
			VirtualFree(data, 0, MEM_RELEASE);
			_close(fd);
			return 0;
		}

		offset += (size_t)count;
	}

	_close(fd);
	data[length] = '\0';
	*out = (SourceBuffer){ data,length };
	return true;
}