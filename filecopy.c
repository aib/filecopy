/* File copy utility with resume support - aib - 20050731 */

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <winioctl.h>
#include <time.h>

#ifndef IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX    CTL_CODE(IOCTL_DISK_BASE, 0x0028, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef DISK_GEOMETRY_EX
typedef struct _DISK_GEOMETRY_EX {
        DISK_GEOMETRY Geometry;                                 // Standard disk geometry: may be faked by driver.
        LARGE_INTEGER DiskSize;                                 // Must always be correct
        BYTE  Data[1];                                                  // Partition, Detect info
} DISK_GEOMETRY_EX, *PDISK_GEOMETRY_EX;
#endif

#define VERSION "1.23.1"

char *GetFilePart(char *fullpath);

int main(int argc, char *argv[])
{
	HANDLE src, dest;
	char destName[MAX_PATH];
	LARGE_INTEGER pos;
	LARGE_INTEGER srcSize;
	DISK_GEOMETRY_EX diskgeo;
	DWORD readbytes;
	DWORD writtenbytes;
	unsigned long blocksize = 0;
	unsigned long skipcount = 0;
	char *buffer;
	WIN32_FILE_ATTRIBUTE_DATA fileAttr;
	int dest_exists;


	clock_t dstart, dend;
	DWORD bytespertick;

	double in_percent;
	double in_speed;

	printf("*** aib's filecopy v" VERSION " ***\n\n");

	if ((argc != 3) && (argc != 4) && (argc != 5)) {
		printf("Usage:\n\t%s <source file> <destination file or dir> [<blocksize>[ <blocks to skip>]]\n", argv[0]);
		return 0;
	}

	if (argc >= 4) {
		blocksize = strtoul(argv[3], (char **) NULL, 10);
	}

	if (argc >= 5) {
		skipcount = strtoul(argv[4], (char **) NULL, 10);
	}

	if (blocksize == 0) blocksize = 512;

	if ((buffer = malloc(blocksize)) == NULL) {
		printf("Unable to allocate memory for read/write buffer.\n");
		return -10;
	}

	dest_exists = 1;
	if (GetFileAttributesEx(argv[2], GetFileExInfoStandard, &fileAttr) == 0) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			dest_exists = 0;
		} else {
			printf("GetFileAttributesEx() failed on destination file.\n");
			return -11;
		}
	}

	if (dest_exists && (fileAttr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		strncpy(destName, argv[2], MAX_PATH);
		strncat(destName, "\\", MAX_PATH-strlen(destName)-1);
		strncat(destName, GetFilePart(argv[1]), MAX_PATH-strlen(destName)-1);
//		printf("Creating \"%s\" in \"%s\"...\n", GetFilePart(argv[1]), argv[2]);
	} else {
		strncpy(destName, argv[2], MAX_PATH);
	}

	if ((dest = CreateFile(destName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
		printf("Unable to create or open destination file: \"%s\".\n", destName);
		return -1;
	}

	if ((src = CreateFile(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
		printf("Unable to open source file.\n");
		return -2;
	}

	if (GetFileSizeEx(src, &srcSize) == 0) {
		printf("GetFileSizeEx() failed on source file, trying ioctl...\n");

		if ((DeviceIoControl(src, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, (LPVOID) &diskgeo, sizeof(diskgeo), &writtenbytes, NULL) == 0) || (writtenbytes == 0)) {
			printf("ioctl failed as well.\n");
			return -5;
		} else {
			srcSize.QuadPart = diskgeo.DiskSize.QuadPart;
		}
	}

	xprintf("SRC file:       %s\n", argv[1]);
	xprintf("DST file:       %s\n", destName);
	xprintf("SRC size:       %llu bytes\n", srcSize.QuadPart);
	xprintf("Block size:     %u bytes\n", blocksize);
	xprintf("Blocks to skip: %u (%llu bytes)\n", skipcount, ((unsigned long long) (skipcount))*blocksize);

	pos.QuadPart = 0;

	if (SetFilePointerEx(dest, pos, &pos, FILE_END) == 0) {
		printf("Unable to seek destination file.\n");
		return -3;
	}

//	original = pos;

	if (pos.QuadPart)
		xprintf("Destination file is at %llu bytes, resuming...\n", pos.QuadPart);

	if (SetFilePointerEx(src, pos, NULL, FILE_BEGIN) == 0) {
		printf("Unable to seek source file.\n");
		return -4;
	}

	dstart = dend = clock();
	bytespertick = 0;
	in_speed = 0;

	while (1) {
		if (skipcount) {
			ZeroMemory(buffer, blocksize);
			readbytes = blocksize;
			skipcount--;
		} else {
			if (ReadFile(src, buffer, blocksize, &readbytes, NULL) == 0) {
				xprintf("\nError reading file at %llu bytes.\n", pos.QuadPart);
				goto end;
			}
		}

		pos.QuadPart += readbytes;

		if ((WriteFile(dest, buffer, readbytes, &writtenbytes, NULL) == 0) || (readbytes != writtenbytes)) {
			printf("\nError writing file.\n");
			goto end;
		}

		bytespertick += readbytes;
		dend = clock();

		if ((dend - dstart) >= CLOCKS_PER_SEC) {
			in_speed = (((double) bytespertick)) * (((double) CLOCKS_PER_SEC) / 1024.0) / ((double) (dend - dstart));
			dstart = clock();
			bytespertick = 0;
		}

		in_percent = (((double) pos.QuadPart) * 100) / ((double) srcSize.QuadPart);

		xprintf("Position:       %llu bytes [%5.2f%%] Speed: %.3f kb/s          \r", pos.QuadPart, in_percent, in_speed);

		if (readbytes != blocksize) {
			xprintf("\nFinished reading at %llu bytes.\n", pos.QuadPart);
			break;
		}
	}

end:
//	printf("\nBytes this session: %u\n", pos-original);

	free(buffer);
	CloseHandle(src);
	CloseHandle(dest);

	return 0;
}

char *GetFilePart(char *fullpath)
{
	char *c;

	c = strrchr(fullpath, '\\');

	if (c == NULL)
		return fullpath;
	else
		return (c+1);
}