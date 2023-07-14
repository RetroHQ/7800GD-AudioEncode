#include <stdio.h>
#include <string.h>
#include "AudioFile.h"
#include "Types.h"
#include "ADPCMEncode.h"

#ifdef _MSC_VER 
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

////////////////////////////////////////////////////////////////////////////////
// Internal struct definition
////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)
struct SDecoderState
{
	s16		nSampleLeft;
	u8		nIndexLeft;
	s16		nSampleRight;
	u8		nIndexRight;
};
#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////
// Initialise empty
////////////////////////////////////////////////////////////////////////////////

CAudioFile::CAudioFile()
{
	m_nLoopEnd = 0;
	m_nLoopStart = 0;
	m_pFile = 0;
	m_nSamples = 0;
	m_pScrubTable = 0;
}

CAudioFile::~CAudioFile()
{
	CloseFile();
}

////////////////////////////////////////////////////////////////////////////////
// Close any open stream
////////////////////////////////////////////////////////////////////////////////

void CAudioFile::CloseFile()
{
	if (m_pScrubTable)
	{
		delete [] m_pScrubTable;
	}
	m_pScrubTable = 0;

	if (m_pFile)
	{
		fclose(m_pFile);
	}
	m_pFile = 0;
}

////////////////////////////////////////////////////////////////////////////////
// All little endian, would needs to be swapped for big endian
////////////////////////////////////////////////////////////////////////////////

#define MAKE4CC(a, b, c, d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

static const u32 ID_RIFF = MAKE4CC('R','I','F','F');
static const u32 ID_WAVE = MAKE4CC('W','A','V','E');
static const u32 ID_FMT  = MAKE4CC('f','m','t',' ');
static const u32 ID_DATA = MAKE4CC('d','a','t','a');
static const u32 ID_CUE  = MAKE4CC('c','u','e',' ');

////////////////////////////////////////////////////////////////////////////////
// WAVE file headers
////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)
struct SWAVChunk
{
	u32		nID;
	u32		nSize;
	u32		nType;
};

struct SWAVSubChunk
{
	u32		nID;
	u32		nSize;
};

struct SWAVFmt
{
	u32		nID;
	u32		nSize;
	u16		nFormat;
	u16		nChannels;
	u32		nSamplesPerSec;
	u32		nAvgBytesPerSec;
	u16		nBlockAlign;
	u16		nBitsPerSample;
};

struct SWAVCue
{
	u32		nID;
	u32		nSize;
	u32		nCuePoints;
	// cue data here
};

struct SWAVCuePoint
{
	u32		nID;
	u32		nPosition;
	u32		nChunkID;
	u32		nChunkStart;
	u32		nBlockStart;
	u32		nSampleStart;
};

struct SWAVData
{
	u32		nID;
	u32		nSize;
	// data follows
};

struct SBupChipStream
{
	u32		nID;
	u32		nScrubTablePos;
	u32		nLoopPos;
	s16		nSampleLeft;
	u8		nIndexLeft;
	s16		nSampleRight;
	u8		nIndexRight;
};
#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////
// Open a WAVE file
////////////////////////////////////////////////////////////////////////////////

bool CAudioFile::OpenWAV(const char *pFilename)
{
	// try and open the file
	if (fopen_s(&m_pFile, pFilename, "rb") != 0)
	{
		printf("ERROR: Unable to open for reading.");
		return false;
	}

	// see if the header looks valid, we want a RIFF first
	bool bSuccess = false;
	u8 anHeader[32];
	u32 nDataLocation = 0;
	u32 nChunkStart;
	bool bParseFile = true;
	
	if (	fread(anHeader, 1, sizeof(SWAVChunk), m_pFile) == sizeof(SWAVChunk) 
		&&	(((SWAVChunk*)anHeader)->nID == ID_RIFF)
		&&	(((SWAVChunk*)anHeader)->nType == ID_WAVE))
	{
		// work through subchunks and see what we have
		SWAVSubChunk *pSubChunk = (SWAVSubChunk *) anHeader;
		while (bParseFile && fread(anHeader, 1, sizeof(SWAVSubChunk), m_pFile) == sizeof(SWAVSubChunk))
		{
			nChunkStart = ftell(m_pFile);
			switch (pSubChunk->nID)
			{
			// cue chunk, used for markers, we use the first as the loop start and
			// optionally the last as the loop end (if not the end of the sample)
			case ID_CUE:
				if (fread(anHeader+sizeof(SWAVSubChunk), 1, sizeof(SWAVCue)-sizeof(SWAVSubChunk), m_pFile) == sizeof(SWAVCue)-sizeof(SWAVSubChunk))
				{
					SWAVCue *pCue = (SWAVCue*)anHeader;
					if (pCue->nCuePoints > 0)
					{
						// setup loop points to be invalid
						m_nLoopStart = m_nSamples;
						m_nLoopEnd = 0;

						// loop through loop points
						u32 nCuePoints = pCue->nCuePoints;
						while (nCuePoints--)
						{
							SWAVCuePoint *pCuePoint = (SWAVCuePoint *)anHeader;
							fread(anHeader, 1, sizeof(SWAVCuePoint), m_pFile);

							// if the cue point looks valid, update loop positions
							if (pCuePoint->nChunkID == ID_DATA)
							{
								if (pCuePoint->nPosition < m_nLoopStart)
								{
									m_nLoopStart = pCuePoint->nPosition;
								}
								if (pCuePoint->nPosition > m_nLoopEnd)
								{
									m_nLoopEnd = pCuePoint->nPosition;
								}
							}
						}

						// if there's just the one marker, use the end of the file as the end
						if (m_nLoopStart == m_nLoopEnd)
						{
							m_nLoopEnd = m_nSamples;
						}
					}

					// sanitise incase of invalid cue points
					if (m_nLoopStart > m_nLoopEnd)
					{
						m_nLoopStart = 0;
						m_nLoopEnd = 0;
					}
				}
				break;

			// format chunk, tells us about the file format
			case ID_FMT:
				if (fread(anHeader+sizeof(SWAVSubChunk), 1, sizeof(SWAVFmt)-sizeof(SWAVSubChunk), m_pFile) == sizeof(SWAVFmt)-sizeof(SWAVSubChunk))
				{
					SWAVFmt *pFmt = (SWAVFmt*)anHeader;

					// check if the finer details of this file are good
					if (pFmt->nID == ID_FMT && pFmt->nChannels == 2 && pFmt->nBitsPerSample == 16 && pFmt->nSamplesPerSec == 48000)
					{
						// make sure we take note of any padding
						fseek(m_pFile, (pFmt->nSize + 8) - sizeof(SWAVFmt) , SEEK_CUR);

						SWAVData *pData = (SWAVData*)anHeader;
						// all seems good, check for the data chunk
						if (	fread(anHeader, 1, sizeof(SWAVData), m_pFile) == sizeof(SWAVData) 
							&&	(pData->nID == ID_DATA))
						{
							nChunkStart = ftell(m_pFile);
							// now we should be in the correct position for data read
							// with 2 bytes per channel and 2 channels for sample
							m_nSamples = pData->nSize / (2 * 2);
							nDataLocation = ftell(m_pFile);
							m_pScrubTable = new SDecoderState[(m_nSamples+4799)/4800];
							bSuccess = true;
						}
						else
						{
							printf("ERRPR: File corrupt, unable to find data chunk.");
							bParseFile = false;
						}
					}
					else
					{
						printf("ERROR: Wrong sample format, expected 48Khz stereo at 16 bits per channel.");
						bParseFile = false;
					}
				}
				break;
			}

			fseek(m_pFile, nChunkStart + pSubChunk->nSize, SEEK_SET);
		}
	}

	// leave file open for reading
	fseek(m_pFile, nDataLocation, SEEK_SET);
	if (!bSuccess)
	{
		fclose(m_pFile);
		m_pFile = 0;
	}
	return bSuccess;
}

////////////////////////////////////////////////////////////////////////////////
// Read a file
////////////////////////////////////////////////////////////////////////////////

bool CAudioFile::OpenFile(const char *pFilename)
{
	CloseFile();
	
	const char *pExt = strrchr(pFilename, '.');
	if (pExt++ && strcasecmp(pExt, "wav") == 0)
	{
		return OpenWAV(pFilename);
	}
	else
	{
		printf("ERROR: Unrecognised file format, please supply a WAV file.");
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////
// Read audio data
////////////////////////////////////////////////////////////////////////////////

u32 CAudioFile::GetSampleBlock(void *pDataOut, const u32 nSamples)
{
	return fread(pDataOut, 4, nSamples, m_pFile);
}

////////////////////////////////////////////////////////////////////////////////
// Write ADPCM encoded audio data
////////////////////////////////////////////////////////////////////////////////

static const u32 ID_STREAM = MAKE4CC('S','T','R',0);

bool CAudioFile::WriteEncoded(const char *pFilename, bool bForceLoop)
{
	// try and open the file
	FILE *f;
	if (fopen_s(&f, pFilename, "wb") != 0)
	{
		printf("ERROR: Unable to open '%s' for writing.", pFilename);
		return false;
	}

	// leave space for header
	SBupChipStream sHeader;
	memset(&sHeader, 0, sizeof(sHeader));
	sHeader.nID = ID_STREAM;

	fseek(f, sizeof(SBupChipStream), SEEK_SET);

	// do some encoding
	ADPCMEncodeStereo mADPCM;
	u32 aSampleBlock[1024];
	u8 aADPCMBlock[1024];
	u32 nLength = NumSamples();
	u32 nPosition = 0, nScrub = 0;
	bool bLooping = (GetLoopStart() + GetLoopEnd()) != 0;
	if (bLooping)
	{
		nLength = GetLoopEnd();
	}

	// if forcing the loop it will be the whole file, so looping at 0
	bLooping |= bForceLoop;
	if (bLooping)
	{
		printf("LOOPED... ");
	}

	while (nLength)
	{
		// read upto 1024 samples at a time
		u32 nBlock = nLength > 1024 ? 1024 : nLength;
		u32 nScrubPos = nScrub * 4800;

		// see if we need to store another scrub entry
		if (nPosition == nScrubPos)
		{
			SDecoderState *p = &m_pScrubTable[nScrub++];
			mADPCM.GetState(&p->nSampleLeft, &p->nIndexLeft, &p->nSampleRight, &p->nIndexRight);
			nScrubPos += 4800;
		}

		// if we're looping we want to grab the decoder state at the loop point
		if (bLooping)
		{
			// if at the loop point, grab our info
			if (nPosition == GetLoopStart())
			{
				// mark the loop position in the file
				sHeader.nLoopPos = ftell(f);

				// and grab the encoder state at this point in the stream
				mADPCM.GetState(&sHeader.nSampleLeft, &sHeader.nIndexLeft, &sHeader.nSampleRight, &sHeader.nIndexRight);
				
				// loop data added, done with looping
				bLooping = false;
			}

			// otherwise if looping make sure we dont skip over the loop point
			else if ((nPosition + nBlock) > GetLoopStart())
			{
				nBlock = GetLoopStart() - nPosition;
			}
		}

		// make sure we grab the decoder state every 1/10th of a second for
		// scrubbing purposes
		if ((nPosition + nBlock) > nScrubPos)
		{
			nBlock = nScrubPos - nPosition;
		}

		// read in sample block
		GetSampleBlock(aSampleBlock, nBlock);
		
		// encode to adpcm and write to stream
		mADPCM.EncodeBlock(aADPCMBlock, (s16 *) aSampleBlock, nBlock);
		fwrite(aADPCMBlock, 1, nBlock, f);

		nLength -= nBlock;
		nPosition += nBlock;
	}

	// mark location of scrub table in header and write it out
	sHeader.nScrubTablePos = ftell(f);
	fwrite(m_pScrubTable, 6, nScrub, f);

	// skip back and write the header
	fseek(f, 0, SEEK_SET);
	fwrite(&sHeader, 1, sizeof(sHeader), f);

	fclose(f);

	return true;
}
