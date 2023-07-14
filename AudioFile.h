#pragma once

#include "Types.h"

struct SDecoderState;
class CAudioFile
{
public:
					CAudioFile();
					~CAudioFile();

	u32				NumSamples() { return m_nSamples; }
	u32				GetLoopStart() { return m_nLoopStart; }
	u32				GetLoopEnd() { return m_nLoopEnd; }
	bool			OpenFile(const char *pFilename);
	void			CloseFile();
	bool			WriteEncoded(const char *pFilename, bool bForceLoop);

private:
	u32				m_nSamples;
	u32				m_nLoopStart, m_nLoopEnd;
	FILE*			m_pFile;
	SDecoderState*	m_pScrubTable;

	u32				GetSampleBlock(void *pDataOut, const u32 nSamples);
	bool			OpenWAV(const char *pFilename);
};
