#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include "AudioFile.h"
#include "Types.h"

#ifdef _MSC_VER 
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

////////////////////////////////////////////////////////////////////////////////
// Usage
////////////////////////////////////////////////////////////////////////////////

void ShowUsage(const char *pCmd)
{
	printf("Usage: %s [filename.ext] {-o path} {-loop}\n\nValid file formats are WAV at 48Khz stereo with 16 bits per sample.\n\n", pCmd);
}

////////////////////////////////////////////////////////////////////////////////
// Command line parsing
////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char **argv)
{
	bool bForceLoop = false;
	bool bUseOutDir = false;
	char szOutputDirectory[MAX_PATH];

	// check we have enough options
	if (argc < 2)
	{
		ShowUsage(argv[0]);
		return -1;
	}

	// check for extra options
	for (int i = 2; i < argc; i++)
	{
		// loop whole file
		if (strcasecmp(argv[i], "-loop") == 0)
		{
			bForceLoop = true;
		}

		// sepcify output directory
		else if (strcasecmp(argv[i], "-o") == 0)
		{
			i++;
			if (i < argc)
			{
				strcpy_s(szOutputDirectory, argv[i]);

				// terminate directory
				if (szOutputDirectory[strlen(szOutputDirectory) -1] != '\\')
				{
					strcat(szOutputDirectory, "\\");
				}
				bUseOutDir = true;
			}
			else
			{
				printf("Missing directory for -o, ignoring.\n\n");
			}
		}

		// unknown
		else
		{
			printf("ERROR: Unknown option '%s'\n\n", argv[2]);
			ShowUsage(argv[0]);
			return -1;
		}
	}

	// do wildcard search on the filename so we can cope with encoding lots of files if needed
	WIN32_FIND_DATAA sFileSearch;
	HANDLE h = FindFirstFileExA(argv[1], FindExInfoStandard, &sFileSearch, FindExSearchNameMatch, NULL, 0);
	if (h == INVALID_HANDLE_VALUE)
	{
		printf("Unable to find file matching '%s'.\n\n", argv[1]);
		return -1;
	}

	// we have at least one file, convert it
	do
	{
		// make sure the file found has any path passed in included
		char szOutFile[MAX_PATH];
		char szInFile[MAX_PATH];

		// handle input file
		strcpy(szInFile, argv[1]);
		char *pPath = strrchr(szInFile, '\\');
		if (!pPath++)
		{
			pPath = szInFile;
		}
		strcpy(pPath, sFileSearch.cFileName);

		// handle output file, just change extension to .bup
		if (bUseOutDir)
		{
			strcpy(szOutFile, szOutputDirectory);
			strcat(szOutFile, sFileSearch.cFileName);
		}
		else
		{
			strcpy(szOutFile, szInFile);
		}
		pPath = strrchr(szOutFile, '.');
		if (!pPath)
		{
			pPath = szOutFile + strlen(szOutFile);
		}
		strcpy(pPath, ".bup");

		// now convert the PCM stream into ADPCM
		printf("Converting '%s'... ", sFileSearch.cFileName);

		CAudioFile cAudioFile;
		if (cAudioFile.OpenFile(szInFile))
		{
			if (cAudioFile.WriteEncoded(szOutFile, bForceLoop))
			{
				printf("OK!");
			}
		}
		printf("\n");
	}
	while (FindNextFileA(h, &sFileSearch));

	FindClose(h);
	
	return 0;
}
