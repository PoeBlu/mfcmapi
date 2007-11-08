// MAPIProfileFunctions.h : Stand alone MAPI profile functions

#pragma once

#include <MapiX.h>
#include <MapiUtil.h>
#include <MAPIform.h>
#include <MSPST.h>

#include <edkmdb.h>
#include <exchform.h>

void LaunchProfileWizard(
	HWND			hParentWnd,
	ULONG			ulFlags,
	LPCSTR FAR *	lppszServiceNameToAdd,
	ULONG			cbBufferMax,
	LPTSTR			lpszNewProfileName);

HRESULT HrMAPIProfileExists(
							LPPROFADMIN lpProfAdmin,
							LPSTR lpszProfileName);

HRESULT HrCreateProfile(
						IN LPSTR lpszProfileName);// profile name

HRESULT HrAddServiceToProfile(
							  IN LPSTR lpszServiceName, // Service Name
							  IN ULONG_PTR ulUIParam, // hwnd for CreateMsgService
							  IN ULONG ulFlags, // Flags for CreateMsgService
							  IN ULONG cPropVals, // Count of properties for ConfigureMsgService
							  IN LPSPropValue lpPropVals, // Properties for ConfigureMsgService
							  IN LPSTR lpszProfileName); // profile name

HRESULT HrAddExchangeToProfile(
							   IN ULONG_PTR ulUIParam, // hwnd for CreateMsgService
							   IN LPSTR lpszServerName,
							   IN LPSTR lpszMailboxName,
							   IN LPSTR lpszProfileName);

HRESULT HrAddPSTToProfile(
						   IN ULONG_PTR ulUIParam, // hwnd for CreateMsgService
						   IN BOOL bUnicode,
						   IN LPSTR lpszPSTPath,
						   IN LPSTR lpszProfileName);

HRESULT HrRemoveProfile(
						LPSTR lpszProfileName);

HRESULT OpenProfileSection(LPSERVICEADMIN lpServiceAdmin, LPSBinary lpServiceUID, LPPROFSECT* lppProfSect);

HRESULT OpenProfileSection(LPPROVIDERADMIN lpProviderAdmin, LPSBinary lpProviderUID, LPPROFSECT* lppProfSect);

void AddServicesToMapiSvcInf();
void RemoveServicesFromMapiSvcInf();
void GetMAPISVCPath(LPTSTR szMAPIDir, ULONG cchMAPIDir);
void DisplayMAPISVCPath(CWnd* pParentWnd);

// http://msdn2.microsoft.com/en-us/library/bb820969.aspx
typedef struct
{
	WORD	wMajorVersion;
	WORD	wMinorVersion;
	WORD 	wBuild;
	WORD 	wMinorBuild;
} EXCHANGE_STORE_VERSION_NUM;

HRESULT GetProfileServiceVersion(LPSTR lpszProfileName,
								 ULONG* lpulServerVersion,
								 EXCHANGE_STORE_VERSION_NUM* lpStoreVersion,
								 BOOL* lpbFoundServerVersion,
								 BOOL* lpbFoundServerFullVersion);