#pragma once
#include "SmartViewParser.h"
#include "EntryIdStruct.h"

struct EntryListEntryStruct
{
	DWORD EntryLength;
	DWORD EntryLengthPad;
	EntryIdStruct EntryId;
};

class EntryList : public SmartViewParser
{
public:
	EntryList();

private:
	void Parse() override;
	_Check_return_ wstring ToStringInternal() override;

	DWORD m_EntryCount;
	DWORD m_Pad;

	vector<EntryListEntryStruct> m_Entry;
};