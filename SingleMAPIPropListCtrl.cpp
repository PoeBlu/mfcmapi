// SingleMAPIPropListCtrl.cpp : implementation file
//

#include "stdafx.h"
#include "SingleMAPIPropListCtrl.h"
#include "StreamEditor.h"
#include "BaseDialog.h"
#include "MAPIFunctions.h"
#include "ColumnTags.h"
#include "MFCUtilityFunctions.h"
#include "UIFunctions.h"
#include "MapiObjects.h"
#include "MySecInfo.h"
#include "Editor.h"
#include "PropertyEditor.h"
#include "InterpretProp2.h"
#include "TagArrayEditor.h"
#include "FileDialogEx.h"
#include "ImportProcs.h"
#include "RestrictEditor.h"
#include "PropertyTagEditor.h"
#include "MAPIProgress.h"
#include "NamedPropCache.h"
#include "SmartView.h"

static TCHAR* CLASS = _T("CSingleMAPIPropListCtrl");

// 26 columns should be enough for anybody
#define MAX_SORT_COLS 26

/////////////////////////////////////////////////////////////////////////////
// CSingleMAPIPropListCtrl

CSingleMAPIPropListCtrl::CSingleMAPIPropListCtrl(
	_In_ CWnd* pCreateParent,
	_In_ CBaseDialog *lpHostDlg,
	_In_ CMapiObjects* lpMapiObjects,
	bool bIsAB)
	:CSortListCtrl()
{
	TRACE_CONSTRUCTOR(CLASS);
	HRESULT hRes = S_OK;

	EC_H(Create(pCreateParent,LVS_SINGLESEL,IDC_LIST_CTRL,true));

	m_lpMAPIProp = NULL;

	m_bHaveEverDisplayedSomething = false;

	// We borrow our parent's Mapi objects
	m_lpMapiObjects = lpMapiObjects;
	if (m_lpMapiObjects) m_lpMapiObjects->AddRef();

	m_lpHostDlg = lpHostDlg;
	if (m_lpHostDlg) m_lpHostDlg->AddRef();

	for (ULONG i = 0;i<NUMPROPCOLUMNS;i++)
	{
		CString szHeaderName;
		EC_B(szHeaderName.LoadString(PropColumns[i].uidName));
		InsertColumn(i,szHeaderName);
	}

	CHeaderCtrl* lpMyHeader = GetHeaderCtrl();

	// Column orders are stored as lowercase letters
	// bacdefghi would mean the first two columns are swapped
	if (lpMyHeader && RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING[0] != '\0')
	{
		bool bSetCols = false;
		int nColumnCount = lpMyHeader->GetItemCount();
		size_t cchOrder = NULL;
		WC_H(StringCchLength(RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING,MAX_SORT_COLS,&cchOrder));
		if (SUCCEEDED(hRes) && nColumnCount == (int) cchOrder)
		{
			LPINT pnOrder = new int[nColumnCount];

			if (pnOrder)
			{
				int i = 0;
				for (i = 0; i<nColumnCount; i++)
				{
					pnOrder[i] = RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING[i] - 'a';
				}
				if (SetColumnOrderArray(nColumnCount,pnOrder))
				{
					bSetCols = true;
				}
			}
			delete[] pnOrder;
		}
		// If we didn't like the reg key, clear it so we don't see it again
		if (!bSetCols) RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING[0] = '\0';
	}

	AutoSizeColumns();

	m_sptExtraProps = NULL;

	m_bIsAB = bIsAB;
	m_lpMAPIProp = NULL;
	m_lpSourceData = NULL;
	m_bRowModified = false;
} // CSingleMAPIPropListCtrl::CSingleMAPIPropListCtrl

CSingleMAPIPropListCtrl::~CSingleMAPIPropListCtrl()
{
	TRACE_DESTRUCTOR(CLASS);
	MAPIFreeBuffer(m_sptExtraProps);
	if (m_lpMAPIProp) m_lpMAPIProp->Release();
	if (m_lpMapiObjects) m_lpMapiObjects->Release();
	if (m_lpHostDlg) m_lpHostDlg->Release();
} // CSingleMAPIPropListCtrl::~CSingleMAPIPropListCtrl

BEGIN_MESSAGE_MAP(CSingleMAPIPropListCtrl, CSortListCtrl)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnDblclk)
	ON_WM_KEYDOWN()
	ON_WM_CONTEXTMENU()
	ON_MESSAGE(WM_MFCMAPI_SAVECOLUMNORDERLIST, msgOnSaveColumnOrder)
END_MESSAGE_MAP()

_Check_return_ LRESULT CSingleMAPIPropListCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_ERASEBKGND:
		if (!m_lpMAPIProp && !m_lpSourceData)
		{
			return true;
		}
		break;
	case WM_PAINT:
		if (!m_lpMAPIProp && !m_lpSourceData)
		{
			DrawHelpText(m_hWnd, IDS_HELPTEXTNOPROPS);
			return true;
		}
		break;
	} // end switch
	return CSortListCtrl::WindowProc(message,wParam,lParam);
} // CSingleMAPIPropListCtrl::WindowProc

/////////////////////////////////////////////////////////////////////////////
// CSingleMAPIPropListCtrl message handlers

// WM_MFCMAPI_SAVECOLUMNORDERLIST
_Check_return_ LRESULT CSingleMAPIPropListCtrl::msgOnSaveColumnOrder(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	HRESULT hRes = S_OK;
	CHeaderCtrl* lpMyHeader = GetHeaderCtrl();

	if (lpMyHeader)
	{
		ULONG nColumnCount = lpMyHeader->GetItemCount();
		if (nColumnCount && nColumnCount <= MAX_SORT_COLS)
		{
			LPINT pnOrder = new int[nColumnCount];

			if (pnOrder)
			{
				EC_B(GetColumnOrderArray(pnOrder, nColumnCount));
				ULONG i = 0;
				for (i = 0; i<nColumnCount; i++)
					RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING[i] = (char)('a' + pnOrder[i]);
				RegKeys[regkeyPROP_COLUMN_ORDER].szCurSTRING[nColumnCount] = '\0';
			}
			delete[] pnOrder;
		}
	}
	return S_OK;
} // CSingleMAPIPropListCtrl::msgOnSaveColumnOrder

void CSingleMAPIPropListCtrl::InitMenu(_In_ CMenu* pMenu)
{
	if (pMenu)
	{
		ULONG ulPropTag = NULL;
		bool bPropSelected = false;

		GetSelectedPropTag(&ulPropTag);
		bPropSelected = (NULL != ulPropTag);

		if (m_lpMapiObjects)
		{
			ULONG ulStatus = m_lpMapiObjects->GetBufferStatus();
			LPENTRYLIST lpEIDsToCopy = m_lpMapiObjects->GetMessagesToCopy();
			pMenu->EnableMenuItem(ID_PASTE_PROPERTY,DIM(m_lpMAPIProp && (ulStatus & BUFFER_PROPTAG) && (ulStatus & BUFFER_SOURCEPROPOBJ)));
			pMenu->EnableMenuItem(ID_COPYTO,DIM(m_lpMAPIProp && (ulStatus & BUFFER_SOURCEPROPOBJ)));
			pMenu->EnableMenuItem(ID_PASTE_NAMEDPROPS, DIM(m_lpMAPIProp  && (ulStatus & BUFFER_MESSAGES) && lpEIDsToCopy && 1 == lpEIDsToCopy->cValues));
		}
		pMenu->EnableMenuItem(ID_COPY_PROPERTY,DIM(m_lpMAPIProp));

		pMenu->EnableMenuItem(ID_DELETEPROPERTY,DIM(m_lpMAPIProp && bPropSelected));
		pMenu->EnableMenuItem(ID_DISPLAYPROPERTYASSECURITYDESCRIPTORPROPSHEET,DIM(m_lpMAPIProp && bPropSelected && pfnEditSecurity));
		pMenu->EnableMenuItem(ID_EDITPROPASBINARYSTREAM,DIM(m_lpMAPIProp && bPropSelected));
		pMenu->EnableMenuItem(ID_EDITPROPERTY,DIM(bPropSelected));
		pMenu->EnableMenuItem(ID_EDITPROPERTYASASCIISTREAM,DIM(m_lpMAPIProp && bPropSelected));
		pMenu->EnableMenuItem(ID_EDITPROPERTYASUNICODESTREAM,DIM(m_lpMAPIProp && bPropSelected));
		pMenu->EnableMenuItem(ID_EDITPROPERTYASPRRTFCOMPRESSEDSTREAM,DIM(m_lpMAPIProp && bPropSelected));
		pMenu->EnableMenuItem(ID_OPEN_PROPERTY,DIM(bPropSelected));
		pMenu->EnableMenuItem(ID_PARSEPROPERTY,DIM(bPropSelected && RegKeys[regkeyDO_SMART_VIEW].ulCurDWORD));

		pMenu->EnableMenuItem(ID_SAVEPROPERTIES,DIM(m_lpMAPIProp || GetPropVals()));
		pMenu->EnableMenuItem(ID_EDITGIVENPROPERTY,DIM(m_lpMAPIProp || GetPropVals()));
		pMenu->EnableMenuItem(ID_OPENPROPERTYASTABLE,DIM(m_lpMAPIProp));
		pMenu->EnableMenuItem(ID_FINDALLNAMEDPROPS,DIM(m_lpMAPIProp));
		pMenu->EnableMenuItem(ID_COUNTNAMEDPROPS,DIM(m_lpMAPIProp));

		if (m_lpHostDlg)
		{
			ULONG ulMenu = ID_ADDINPROPERTYMENU;
			for (ulMenu = ID_ADDINPROPERTYMENU ;; ulMenu++)
			{
				LPMENUITEM lpAddInMenu = GetAddinMenuItem(m_lpHostDlg->m_hWnd,ulMenu);
				if (!lpAddInMenu) break;

				UINT uiEnable = DIM(bPropSelected);

				pMenu->EnableMenuItem(ulMenu,uiEnable);
			}
		}
	}
} // CSingleMAPIPropListCtrl::InitMenu

_Check_return_ bool CSingleMAPIPropListCtrl::HandleMenu(WORD wMenuSelect)
{
	DebugPrint(DBGMenu,_T("CSingleMAPIPropListCtrl::HandleMenu wMenuSelect = 0x%X = %d\n"),wMenuSelect,wMenuSelect);
	switch (wMenuSelect)
	{
	case ID_COPY_PROPERTY: OnCopyProperty(); return true;
	case ID_COPYTO: OnCopyTo(); return true;
	case ID_DELETEPROPERTY: OnDeleteProperty(); return true;
	case ID_DISPLAYPROPERTYASSECURITYDESCRIPTORPROPSHEET: OnDisplayPropertyAsSecurityDescriptorPropSheet(); return true;
	case ID_PARSEPROPERTY: OnParseProperty(); return true;
	case ID_EDITGIVENPROPERTY: OnEditGivenProperty(); return true;
	case ID_EDITPROPERTY: OnEditProp(); return true;
	case ID_EDITPROPASBINARYSTREAM: OnEditPropAsStream(PT_BINARY,false); return true;
	case ID_EDITPROPERTYASASCIISTREAM: OnEditPropAsStream(PT_STRING8,false); return true;
	case ID_EDITPROPERTYASUNICODESTREAM: OnEditPropAsStream(PT_UNICODE,false); return true;
	case ID_EDITPROPERTYASPRRTFCOMPRESSEDSTREAM: OnEditPropAsStream(PT_BINARY,true); return true;
	case ID_FINDALLNAMEDPROPS: FindAllNamedProps(); return true;
	case ID_COUNTNAMEDPROPS: CountNamedProps(); return true;
	case ID_MODIFYEXTRAPROPS: OnModifyExtraProps(); return true;
	case ID_OPEN_PROPERTY: OnOpenProperty(); return true;
	case ID_OPENPROPERTYASTABLE: OnOpenPropertyAsTable(); return true;
	case ID_PASTE_NAMEDPROPS: OnPasteNamedProps(); return true;
	case ID_PASTE_PROPERTY: OnPasteProperty(); return true;
	case ID_SAVEPROPERTIES: SavePropsToXML(); return true;
	}

	return HandleAddInMenu(wMenuSelect);
} // CSingleMAPIPropListCtrl::HandleMenu

void CSingleMAPIPropListCtrl::GetSelectedPropTag(_Out_ ULONG* lpPropTag)
{
	int	iItem = NULL;

	if (lpPropTag) *lpPropTag = NULL;

	iItem = GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);

	if (-1 != iItem)
	{
		SortListData* lpListData = ((SortListData*)GetItemData(iItem));
		if (lpListData)
		{
			if (lpPropTag)
				*lpPropTag = lpListData->data.Prop.ulPropTag;
		}
	}

	if (lpPropTag)
	{
		DebugPrintEx(DBGGeneric,CLASS,_T("GetSelectedPropTag"),_T("returning lpPropTag = 0x%X\n"),*lpPropTag);
	}
} // CSingleMAPIPropListCtrl::GetSelectedPropTag

// Call GetProps with NULL to get a list of (almost) all properties.
// Parse this list and render them in the control.
// Add any extra props we've asked for through the UI
_Check_return_ HRESULT CSingleMAPIPropListCtrl::LoadMAPIPropList()
{
	HRESULT			hRes = S_OK;
	ULONG			ulCurListBoxRow = 0;
	ULONG			ulCurPropRow = 0;
	CWaitCursor		Wait; // Change the mouse to an hourglass while we work.
	ULONG			ulProps = 0;
	LPSPropValue	lpPropsFromGetProps = NULL;
	LPSPropValue	lpPropsToAdd = NULL;

	LPSPropValue	lpPropVals = GetPropVals(); // do not free!
	if (!m_lpMAPIProp && !lpPropVals) return MAPI_E_INVALID_PARAMETER;

	// Determine if we have a set of properties to add to the view
	// Either from the row or from GetProps
	if (GetCountPropVals() &&
		lpPropVals &&
		(!m_lpMAPIProp || RegKeys[regkeyUSE_ROW_DATA_FOR_SINGLEPROPLIST].ulCurDWORD))
	{
		if (m_lpHostDlg)
			m_lpHostDlg->UpdateStatusBarText(STATUSINFOTEXT, IDS_PROPSFROMROW, 0, 0, 0);

		ulProps = GetCountPropVals();
		lpPropsToAdd = lpPropVals;
	}
	else if (m_lpMAPIProp)
	{
		if (m_lpHostDlg)
			m_lpHostDlg->UpdateStatusBarText(STATUSINFOTEXT, IDS_PROPSFROMGETPROPS, 0, 0, 0);
		if (RegKeys[regkeyDO_GETPROPS].ulCurDWORD)
		{
			// Can't use EC_H_GETPROPS because I want to suppress MAPI_E_CALL_FAILED as well
			hRes = GetPropsNULL(m_lpMAPIProp,
				fMapiUnicode,
				&ulProps,
				&lpPropsFromGetProps);
			if (MAPI_E_CALL_FAILED == hRes)
			{
				// Some stores, like public folders, don't support properties on the root folder
				DebugPrint(DBGGeneric,_T("Failed to get call GetProps on this object!\n"));
			}
			else if (FAILED(hRes)) // only report errors, not warnings
			{
				CHECKHRESMSG(hRes,IDS_GETPROPSNULLFAILED);
			}
			else if (lpPropsFromGetProps)
			{
				lpPropsToAdd = lpPropsFromGetProps;
			}
		}
	}

	// If we got some properties and PR_MAPPING_SIGNATURE is among them, grab it now
	LPSPropValue lpMappingSig = PpropFindProp(lpPropsToAdd, ulProps, PR_MAPPING_SIGNATURE);

	// Add our props to the view
	if (lpPropsToAdd)
	{
		// If we don't pass named property information to AddPropToListBox, it will look it up for us
		// But this costs a GetNamesFromIDs call for each property we add
		// As a speed up, we put together a single GetNamesFromIDs call here and pass its results to AddPropToListBox
		// The speed up in non-cached mode is enormous
		ULONG ulPropNames = 0;
		LPMAPINAMEID* lppPropNames = 0;
		ULONG ulCurTag = 0;
		if (m_lpMAPIProp)
		{
			ULONG ulNamedProps = 0;

			// First, count how many props to look up
			if (RegKeys[regkeyGETPROPNAMES_ON_ALL_PROPS].ulCurDWORD)
			{
				ulNamedProps = ulProps;
			}
			else
			{
				for (ulCurPropRow = 0; ulCurPropRow < ulProps; ulCurPropRow++)
				{
					if (PROP_ID(lpPropsToAdd[ulCurPropRow].ulPropTag) >= 0x8000) ulNamedProps++;
				}
			}

			// Allocate our tag array
			LPSPropTagArray	lpTag = NULL;
			if (ulNamedProps)
			{
				EC_H(MAPIAllocateBuffer(
					CbNewSPropTagArray(ulNamedProps),
					(LPVOID*) &lpTag));
				if (lpTag)
				{
					// Populate the array
					lpTag->cValues = ulNamedProps;
					if (RegKeys[regkeyGETPROPNAMES_ON_ALL_PROPS].ulCurDWORD)
					{
						for (ulCurPropRow = 0; ulCurPropRow < ulProps; ulCurPropRow++)
						{
							lpTag->aulPropTag[ulCurPropRow] = lpPropsToAdd[ulCurPropRow].ulPropTag;
						}
					}
					else
					{
						for (ulCurPropRow = 0; ulCurPropRow < ulProps; ulCurPropRow++)
						{
							if (PROP_ID(lpPropsToAdd[ulCurPropRow].ulPropTag) >= 0x8000)
							{
								lpTag->aulPropTag[ulCurTag] = lpPropsToAdd[ulCurPropRow].ulPropTag;
								ulCurTag++;
							}
						}
					}

					// Get the names
					WC_H_GETPROPS(GetNamesFromIDs(m_lpMAPIProp,
						lpMappingSig?&lpMappingSig->Value.bin:NULL,
						&lpTag,
						NULL,
						NULL,
						&ulPropNames,
						&lppPropNames));
					hRes = S_OK;

					MAPIFreeBuffer(lpTag);
				}
			}
		}

		// Is this worth it?
		// Set the item count to speed up the addition of items
		ULONG ulTotalRowCount = ulProps;
		if (m_lpMAPIProp && m_sptExtraProps) ulTotalRowCount += m_sptExtraProps->cValues;
		SetItemCount(ulTotalRowCount);

		// get each property in turn and add it to the list
		ulCurTag = 0;
		for (ulCurPropRow = 0; ulCurPropRow < ulProps; ulCurPropRow++)
		{
			LPMAPINAMEID lpNameIDInfo = NULL;
			// We shouldn't need to check ulCurTag < ulPropNames, but I fear bad GetNamesFromIDs implementations
			if (lppPropNames && ulCurTag < ulPropNames)
			{
				if (RegKeys[regkeyGETPROPNAMES_ON_ALL_PROPS].ulCurDWORD ||
					PROP_ID(lpPropsToAdd[ulCurPropRow].ulPropTag) >= 0x8000)
				{
					lpNameIDInfo = lppPropNames[ulCurTag];
					ulCurTag++;
				}
			}

			AddPropToListBox(
				ulCurListBoxRow,
				lpPropsToAdd[ulCurPropRow].ulPropTag,
				lpNameIDInfo,
				lpMappingSig?&lpMappingSig->Value.bin:NULL,
				&lpPropsToAdd[ulCurPropRow]);

			ulCurListBoxRow++;
		}
		MAPIFreeBuffer(lppPropNames);
	}

	// Now check if the user has given us any other properties to add and get them one at a time
	if (m_lpMAPIProp && m_sptExtraProps)
	{
		// Let's get each extra property one at a time
		ULONG			cExtraProps = 0;
		LPSPropValue	pExtraProps = NULL;
		SPropValue		ExtraPropForList;
		ULONG			iCurExtraProp = 0;
		SPropTagArray	pNewTag;
		pNewTag.cValues = 1;

		for (iCurExtraProp = 0 ; iCurExtraProp < m_sptExtraProps->cValues ; iCurExtraProp++)
		{
			hRes = S_OK; // clear the error flag before each run
			pNewTag.aulPropTag[0] = m_sptExtraProps->aulPropTag[iCurExtraProp];

			// Let's add some extra properties
			// Don't need to report since we're gonna put show the error in the UI
			WC_MAPI(m_lpMAPIProp->GetProps(
				&pNewTag,
				fMapiUnicode,
				&cExtraProps,
				&pExtraProps));

			if (pExtraProps)
			{
				ExtraPropForList.dwAlignPad = pExtraProps[0].dwAlignPad;

				if (PROP_TYPE(pNewTag.aulPropTag[0]) == NULL)
				{
					// In this case, we started with a NULL tag, but we got a property back - let's 'fix' our tag for the UI
					pNewTag.aulPropTag[0]  =
						CHANGE_PROP_TYPE(pNewTag.aulPropTag[0],PROP_TYPE(pExtraProps[0].ulPropTag));
				}

				// We want to give our parser the tag that came back from GetProps
				ExtraPropForList.ulPropTag = pExtraProps[0].ulPropTag;

				ExtraPropForList.Value = pExtraProps[0].Value;
			}
			else
			{
				ExtraPropForList.dwAlignPad = NULL;
				ExtraPropForList.ulPropTag = CHANGE_PROP_TYPE(pNewTag.aulPropTag[0],PT_ERROR);
				ExtraPropForList.Value.err = hRes;
			}

			// Add the property to the list
			AddPropToListBox(
				ulCurListBoxRow,
				pNewTag.aulPropTag[0], // Tag to use in the UI
				NULL, // Let AddPropToListBox look up any named prop information it needs
				lpMappingSig?&lpMappingSig->Value.bin:NULL,
				&ExtraPropForList); // Tag + Value to parse - may differ in case of errors or NULL type.

			ulCurListBoxRow++;

			MAPIFreeBuffer(pExtraProps);
			pExtraProps = NULL;
		}
	}

	// lpMappingSig might come from lpPropsFromGetProps, so don't free this until here
	MAPIFreeBuffer(lpPropsFromGetProps);

	DebugPrintEx(DBGGeneric,CLASS,_T("LoadMAPIPropList"),_T("added %d properties\n"),ulCurListBoxRow);

	SortClickedColumn();

	// Don't report any errors from here - don't care at this point
	return S_OK;
} // CSingleMAPIPropListCtrl::LoadMAPIPropList

_Check_return_ HRESULT CSingleMAPIPropListCtrl::RefreshMAPIPropList()
{
	HRESULT hRes = S_OK;
	int iSelectedItem;
	DebugPrintEx(DBGGeneric,CLASS,_T("RefreshMAPIPropList"),_T("\n"));

	// Turn off redraw while we work on the window
	MySetRedraw(false);
	POSITION MyPos = GetFirstSelectedItemPosition();

	iSelectedItem = GetNextSelectedItem(MyPos);

	EC_B(DeleteAllItems());

	if (m_lpMAPIProp || GetPropVals())
		EC_H(LoadMAPIPropList());

	SetItemState(iSelectedItem,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);

	EnsureVisible(iSelectedItem,false);

	// Turn redraw back on to update our view
	MySetRedraw(true);

	if (m_lpHostDlg)
		m_lpHostDlg->UpdateStatusBarText(STATUSDATA2, IDS_STATUSTEXTNUMPROPS, GetItemCount(), 0, 0);

	return hRes;
} // CSingleMAPIPropListCtrl::RefreshMAPIPropList

_Check_return_ HRESULT CSingleMAPIPropListCtrl::AddPropToExtraProps(ULONG ulPropTag, bool bRefresh)
{
	HRESULT			hRes = S_OK;
	SPropTagArray	sptSingleProp;

	DebugPrintEx(DBGGeneric,CLASS,_T("AddPropToExtraProps"),_T("adding proptag 0x%X\n"),ulPropTag);

	// Cache this proptag so we continue to request it in this view
	// We've got code to refresh any props cached in m_sptExtraProps...let's add to that.

	sptSingleProp.cValues = 1;
	sptSingleProp.aulPropTag[0] = ulPropTag;

	EC_H(AddPropsToExtraProps(&sptSingleProp,bRefresh));

	return hRes;
} // CSingleMAPIPropListCtrl::AddPropToExtraProps

_Check_return_ HRESULT CSingleMAPIPropListCtrl::AddPropsToExtraProps(_In_ LPSPropTagArray lpPropsToAdd, bool bRefresh)
{
	HRESULT			hRes = S_OK;
	LPSPropTagArray lpNewExtraProps = NULL;

	DebugPrintEx(DBGGeneric,CLASS,_T("AddPropsToExtraProps"),_T("adding prop array %p\n"),lpPropsToAdd);

	EC_H(ConcatSPropTagArrays(
		m_sptExtraProps,
		lpPropsToAdd,
		&lpNewExtraProps));

	MAPIFreeBuffer(m_sptExtraProps);
	m_sptExtraProps = lpNewExtraProps;

	if (bRefresh)
	{
		WC_H(RefreshMAPIPropList());
	}

	return hRes;
} // CSingleMAPIPropListCtrl::AddPropsToExtraProps

#define NUMPROPTYPES 31
static ULONG _PropTypeIcons[NUMPROPTYPES][2] =
{
	{PT_UNSPECIFIED,slIconUNSPECIFIED},
	{PT_NULL,slIconNULL},
	{PT_I2,slIconI2},
	{PT_LONG,slIconLONG},
	{PT_R4,slIconR4},
	{PT_DOUBLE,slIconDOUBLE},
	{PT_CURRENCY,slIconCURRENCY},
	{PT_APPTIME,slIconAPPTIME},
	{PT_ERROR,slIconERROR},
	{PT_BOOLEAN,slIconBOOLEAN},
	{PT_OBJECT,slIconOBJECT},
	{PT_I8,slIconI8},
	{PT_STRING8,slIconSTRING8},
	{PT_UNICODE,slIconUNICODE},
	{PT_SYSTIME,slIconSYSTIME},
	{PT_CLSID,slIconCLSID},
	{PT_BINARY,slIconBINARY},
	{PT_MV_I2,slIconMV_I2},
	{PT_MV_LONG,slIconMV_LONG},
	{PT_MV_R4,slIconMV_R4},
	{PT_MV_DOUBLE,slIconMV_DOUBLE},
	{PT_MV_CURRENCY,slIconMV_CURRENCY},
	{PT_MV_APPTIME,slIconMV_APPTIME},
	{PT_MV_SYSTIME,slIconMV_SYSTIME},
	{PT_MV_STRING8,slIconMV_STRING8},
	{PT_MV_BINARY,slIconMV_BINARY},
	{PT_MV_UNICODE,slIconMV_UNICODE},
	{PT_MV_CLSID,slIconMV_CLSID},
	{PT_MV_I8,slIconMV_I8},
	{PT_SRESTRICTION,slIconSRESTRICTION},
	{PT_ACTIONS,slIconACTIONS},
};

// Crack open the given SPropValue and render it to the given row in the list.
void CSingleMAPIPropListCtrl::AddPropToListBox(
	int iRow,
	ULONG ulPropTag,
	_In_opt_ LPMAPINAMEID lpNameID,
	_In_opt_ LPSBinary lpMappingSignature, // optional mapping signature for object to speed named prop lookups
	_In_ LPSPropValue lpsPropToAdd)
{
	ULONG ulImage = slIconDefault;
	if (lpsPropToAdd)
	{
		int i = 0;
		for (i=0;i<NUMPROPTYPES;i++)
		{
			if (_PropTypeIcons[i][0] == PROP_TYPE(lpsPropToAdd->ulPropTag))
			{
				ulImage = _PropTypeIcons[i][1];
				break;
			}
		}
	}

	SortListData* lpData = NULL;
	lpData = InsertRow(iRow,_T(""),0,ulImage);
	// Data used to refer to specific property tags. See GetSelectedPropTag.
	if (lpData)
	{
		lpData->data.Prop.ulPropTag = ulPropTag;
		lpData->bItemFullyLoaded = true;
		lpData->ulSortDataType = SORTLIST_PROP;
	}

	CString PropTag;
	CString PropString;
	CString AltPropString;
	CString PropType;
	LPTSTR szExactMatches = NULL;
	LPTSTR szPartialMatches = NULL;
	LPWSTR szSmartView = NULL;
	LPTSTR szNamedPropName = 0;
	LPTSTR szNamedPropGUID = 0;

	// If we have lpNameID, we don't need to ask InterpretProp to look up named property information

	InterpretProp(
		lpsPropToAdd,
		ulPropTag,
		m_lpMAPIProp,
		lpNameID,
		lpMappingSignature,
		m_bIsAB,
		&szExactMatches, // Built from ulPropTag & bIsAB
		&szPartialMatches, // Built from ulPropTag & bIsAB
		&PropType, // Built from ulPropTag
		&PropTag, // Built from ulPropTag
		&PropString, // Built from lpProp
		&AltPropString, // Built from lpProp
		&szNamedPropName, // Built from lpProp & lpMAPIProp
		&szNamedPropGUID, // Built from lpProp & lpMAPIProp
		NULL);

	InterpretPropSmartView(
		lpsPropToAdd,
		m_lpMAPIProp,
		lpNameID,
		lpMappingSignature,
		false,
		&szSmartView); // Built from lpProp & lpMAPIProp

	SetItemText(iRow,pcPROPEXACTNAMES,szExactMatches?szExactMatches:(LPCTSTR) PropTag);
	SetItemText(iRow,pcPROPPARTIALNAMES,szPartialMatches?szPartialMatches:_T(""));
	SetItemText(iRow,pcPROPTAG,(LPCTSTR) PropTag);
	SetItemText(iRow,pcPROPTYPE,(LPCTSTR) TypeToString(ulPropTag));
	SetItemText(iRow,pcPROPVAL,(LPCTSTR) PropString);
	SetItemText(iRow,pcPROPVALALT,(LPCTSTR) AltPropString);
	if (szSmartView) SetItemTextW(iRow,pcPROPSMARTVIEW,(LPCWSTR) szSmartView);
	if (szNamedPropName) SetItemText(iRow,pcPROPNAMEDNAME,szNamedPropName);
	if (szNamedPropGUID) SetItemText(iRow,pcPROPNAMEDIID,szNamedPropGUID);

	delete[] szPartialMatches;
	delete[] szExactMatches;
	delete[] szSmartView;
	FreeNameIDStrings(szNamedPropName, szNamedPropGUID, NULL);
} // CSingleMAPIPropListCtrl::AddPropToListBox

// to get the count of source properties
_Check_return_ ULONG CSingleMAPIPropListCtrl::GetCountPropVals()
{
	if (m_lpSourceData)
	{
		return m_lpSourceData->cSourceProps;
	}
	return 0;
} // CSingleMAPIPropListCtrl::IsModifiedPropVals

_Check_return_ bool CSingleMAPIPropListCtrl::IsModifiedPropVals()
{
	if (m_lpSourceData && m_bRowModified) return true;
	return false;
} // CSingleMAPIPropListCtrl::IsModifiedPropVals

_Check_return_ LPSPropValue CSingleMAPIPropListCtrl::GetPropVals()
{
	if (m_lpSourceData && m_lpSourceData->lpSourceProps)
	{
		return m_lpSourceData->lpSourceProps;
	}
	return NULL;
} // CSingleMAPIPropListCtrl::GetPropVals

// Clear the current property list from the control.
// Load a new list from the IMAPIProp or lpSourceProps object passed in
// Most calls to this will come through CBaseDialog::OnUpdateSingleMAPIPropListCtrl, which will preserve the current bIsAB
// Exceptions will be where we need to set a specific bIsAB
_Check_return_ HRESULT CSingleMAPIPropListCtrl::SetDataSource(_In_opt_ LPMAPIPROP lpMAPIProp, _In_opt_ SortListData* lpListData, bool bIsAB)
{
	HRESULT hRes = S_OK;
	// if nothing to do...do nothing
	if (lpMAPIProp == m_lpMAPIProp && m_lpSourceData == lpListData) return S_OK;

	DebugPrintEx(DBGGeneric,CLASS,_T("SetDataSource"),_T("clearing %p and adding %p\n"),m_lpMAPIProp, lpMAPIProp);

	// Turn off redraw while we work on the window
	MySetRedraw(false);

	m_bIsAB = bIsAB;
	m_lpSourceData = lpListData;

	if (m_lpMAPIProp) m_lpMAPIProp->Release();
	m_lpMAPIProp = lpMAPIProp;

	if (m_lpMAPIProp) m_lpMAPIProp->AddRef();

	WC_H(RefreshMAPIPropList());

	// Reset our header widths if weren't showing anything before and are now
	if (S_OK == hRes && !m_bHaveEverDisplayedSomething && m_lpMAPIProp && GetItemCount())
	{
		int iCurCol;
		m_bHaveEverDisplayedSomething = true;

		CHeaderCtrl* lpMyHeader = GetHeaderCtrl();

		if (lpMyHeader)
		{
			// This fixes a ton of flashing problems
			lpMyHeader->SetRedraw(true);
			for (iCurCol = 0;iCurCol<NUMPROPCOLUMNS;iCurCol++)
			{
				SetColumnWidth(iCurCol,LVSCW_AUTOSIZE_USEHEADER);
				if (GetColumnWidth(iCurCol) > 200) SetColumnWidth(iCurCol,200);
			}
			lpMyHeader->SetRedraw(false);
		}
	}

	// Turn redraw back on to update our view
	MySetRedraw(true);
	return hRes;
} // CSingleMAPIPropListCtrl::SetDataSource

void CSingleMAPIPropListCtrl::SavePropsToXML()
{
	WCHAR *szFileName = NULL;
	HRESULT hRes = S_OK;
	INT_PTR iDlgRet = 0;

	CStringW szFileSpec;
	EC_B(szFileSpec.LoadString(IDS_XMLFILES));

	CFileDialogExW dlgFilePicker;

	EC_D_DIALOG(dlgFilePicker.DisplayDialog(
		false, // Save As dialog
		L"xml", // STRING_OK
		L"props.xml", // STRING_OK
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		szFileSpec,
		this));
	if (IDOK == iDlgRet)
	{
		szFileName = dlgFilePicker.GetFileName();

		if (szFileName)
		{
			FILE* fProps = NULL;
			fProps = MyOpenFile(szFileName, true);
			if (fProps)
			{
				DebugPrintEx(DBGGeneric,CLASS,_T("SavePropsToXML"),_T("saving to %ws\n"),szFileName);

				// Force a sort on the tag column to make output consistent
				FakeClickColumn(pcPROPTAG,false);

				int iRow;
				int iItemCount = GetItemCount();

				OutputToFile(fProps, g_szXMLHeader);
				OutputToFile(fProps,_T("<propertypane>\n"));
				for (iRow = 0; iRow<iItemCount; iRow++)
				{
					CString szTemp1;
					CString szTemp2;
					szTemp1 = GetItemText(iRow,pcPROPTAG);
					szTemp2 = GetItemText(iRow,pcPROPTYPE);
					OutputToFilef(fProps,_T("\t<property tag = \"%s\" type = \"%s\">\n"),(LPCTSTR) szTemp1,(LPCTSTR) szTemp2);

					szTemp1 = GetItemText(iRow,pcPROPEXACTNAMES);
					OutputXMLValueToFile(fProps,PropXMLNames[pcPROPEXACTNAMES].uidName,(LPCTSTR) szTemp1,2);

					szTemp1 = GetItemText(iRow,pcPROPPARTIALNAMES);
					OutputXMLValueToFile(fProps,PropXMLNames[pcPROPPARTIALNAMES].uidName,(LPCTSTR) szTemp1,2);

					szTemp1 = GetItemText(iRow,pcPROPNAMEDIID);
					OutputXMLValueToFile(fProps,PropXMLNames[pcPROPNAMEDIID].uidName,(LPCTSTR) szTemp1,2);

					szTemp1 = GetItemText(iRow,pcPROPNAMEDNAME);
					OutputXMLValueToFile(fProps,PropXMLNames[pcPROPNAMEDNAME].uidName,(LPCTSTR) szTemp1,2);

					SortListData* lpListData = ((SortListData*)GetItemData(iRow));
					ULONG ulPropType = PT_NULL;
					if (lpListData)
						ulPropType = PROP_TYPE(lpListData->data.Prop.ulPropTag);

					szTemp1 = GetItemText(iRow,pcPROPVAL);
					szTemp2 = GetItemText(iRow,pcPROPVALALT);
					switch (ulPropType)
					{
					case PT_STRING8:
					case PT_UNICODE:
						{
							OutputXMLCDataValueToFile(fProps,PropXMLNames[pcPROPVAL].uidName,(LPTSTR) (LPCTSTR) szTemp1,2);
							OutputXMLValueToFile(fProps,PropXMLNames[pcPROPVALALT].uidName,(LPCTSTR) szTemp2,2);
							break;
						}
					case PT_BINARY:
						{
							OutputXMLValueToFile(fProps,PropXMLNames[pcPROPVAL].uidName,(LPCTSTR) szTemp1,2);
							OutputXMLCDataValueToFile(fProps,PropXMLNames[pcPROPVALALT].uidName,(LPTSTR) (LPCTSTR) szTemp2,2);
							break;
						}
					default:
						{
							OutputXMLValueToFile(fProps,PropXMLNames[pcPROPVAL].uidName,(LPCTSTR) szTemp1,2);
							OutputXMLValueToFile(fProps,PropXMLNames[pcPROPVALALT].uidName,(LPCTSTR) szTemp2,2);
							break;
						}
					}

					szTemp1 = GetItemText(iRow,pcPROPSMARTVIEW);
					OutputXMLCDataValueToFile(fProps,PropXMLNames[pcPROPSMARTVIEW].uidName,(LPTSTR) (LPCTSTR) szTemp1,2);

					OutputToFile(fProps,_T("\t</property>\n"));
				}
				OutputToFile(fProps,_T("</propertypane>"));
				CloseFile(fProps);
			}
		}
	}
} // CSingleMAPIPropListCtrl::SavePropsToXML

void CSingleMAPIPropListCtrl::OnDblclk(_In_ NMHDR* /*pNMHDR*/, _In_ LRESULT* pResult)
{
	DebugPrintEx(DBGGeneric,CLASS,_T("OnDblclk"),_T("calling OnEditProp\n"));
	OnEditProp();
	*pResult = 0;
} // CSingleMAPIPropListCtrl::OnDblclk

void CSingleMAPIPropListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	DebugPrintEx(DBGMenu,CLASS,_T("OnKeyDown"),_T("0x%X\n"),nChar);

	HRESULT hRes = S_OK;
	bool bCtrlPressed = GetKeyState(VK_CONTROL) <0;
	bool bShiftPressed = GetKeyState(VK_SHIFT) <0;
	bool bMenuPressed = GetKeyState(VK_MENU) <0;

	if (!bMenuPressed)
	{
		if ('X' == nChar && bCtrlPressed && !bShiftPressed)
		{
			OnDeleteProperty();
		}
		else if (VK_DELETE == nChar)
		{
			OnDeleteProperty();
		}
		else if ('S' == nChar && bCtrlPressed)
		{
			SavePropsToXML();
		}
		else if ('E' == nChar && bCtrlPressed)
		{
			OnEditProp();
		}
		else if ('C' == nChar && bCtrlPressed && !bShiftPressed)
		{
			OnCopyProperty();
		}
		else if ('V' == nChar && bCtrlPressed && !bShiftPressed)
		{
			OnPasteProperty();
		}
		else if (VK_F5 == nChar)
		{
			WC_H(RefreshMAPIPropList());
		}
		else if (VK_RETURN == nChar)
		{
			if (!bCtrlPressed)
			{
				DebugPrintEx(DBGMenu,CLASS,_T("OnKeyDown"),_T("calling OnEditProp\n"));
				OnEditProp();
			}
			else
			{
				DebugPrintEx(DBGMenu,CLASS,_T("OnKeyDown"),_T("calling OnOpenProperty\n"));
				OnOpenProperty();
			}
		}
		else if (!m_lpHostDlg || !m_lpHostDlg->HandleKeyDown(nChar,bShiftPressed,bCtrlPressed,bMenuPressed))
		{
			CSortListCtrl::OnKeyDown(nChar,nRepCnt,nFlags);
		}
	}
} // CSingleMAPIPropListCtrl::OnKeyDown

void CSingleMAPIPropListCtrl::OnContextMenu(_In_ CWnd* /*pWnd*/, CPoint pos)
{
	DisplayContextMenu(IDR_MENU_PROPERTY_POPUP,NULL,m_lpHostDlg->m_hWnd,pos.x, pos.y);
} // CSingleMAPIPropListCtrl::OnContextMenu

void CSingleMAPIPropListCtrl::FindAllNamedProps()
{
	HRESULT hRes = S_OK;
	LPSPropTagArray lptag = NULL;

	if (!m_lpMAPIProp) return;

	// Exchange can return MAPI_E_NOT_ENOUGH_MEMORY when I call this - give it a try - PSTs support it
	DebugPrintEx(DBGNamedProp,CLASS,_T("FindAllNamedProps"),_T("Calling GetIDsFromNames with a NULL\n"));
	WC_H(GetIDsFromNames(m_lpMAPIProp,
		NULL,
		NULL,
		NULL,
		&lptag));
	if (S_OK == hRes && lptag && lptag->cValues)
	{
		// Now we have an array of tags - add them in:
		EC_H(AddPropsToExtraProps(lptag,false));
		MAPIFreeBuffer(lptag);
		lptag = NULL;
	}
	else
	{
		hRes = S_OK;

		DebugPrintEx(DBGNamedProp,CLASS,_T("FindAllNamedProps"),_T("Exchange didn't support GetIDsFromNames(NULL).\n"));

#define __LOWERBOUND 0x8000
#define __UPPERBOUNDDEFAULT 0x8FFF
#define __UPPERBOUND 0xFFFF

		CEditor MyData(
			this,
			IDS_FINDNAMEPROPSLIMIT,
			IDS_FINDNAMEPROPSLIMITPROMPT,
			2,
			CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);
		MyData.InitSingleLine(0,IDS_LOWERBOUND,NULL,false);
		MyData.SetHex(0,__LOWERBOUND);
		MyData.InitSingleLine(1,IDS_UPPERBOUND,NULL,false);
		MyData.SetHex(1,__UPPERBOUNDDEFAULT);

		WC_H(MyData.DisplayDialog());

		if (S_OK == hRes)
		{
			ULONG ulLowerBound = MyData.GetHex(0);
			ULONG ulUpperBound = MyData.GetHex(1);

			DebugPrintEx(DBGNamedProp,CLASS,_T("FindAllNamedProps"),_T("Walking through all IDs from 0x%X to 0x%X, looking for mappings to names\n"),ulLowerBound,ulUpperBound);
			if (ulLowerBound < __LOWERBOUND)
			{
				ErrDialog(__FILE__,__LINE__,IDS_EDLOWERBOUNDTOOLOW,ulLowerBound,__LOWERBOUND);
				hRes = MAPI_E_INVALID_PARAMETER;
			}
			else if (ulUpperBound > __UPPERBOUND)
			{
				ErrDialog(__FILE__,__LINE__,IDS_EDUPPERBOUNDTOOHIGH,ulUpperBound,__UPPERBOUND);
				hRes = MAPI_E_INVALID_PARAMETER;
			}
			else if (ulLowerBound > ulUpperBound)
			{
				ErrDialog(__FILE__,__LINE__,IDS_EDLOWEROVERUPPER,ulLowerBound,ulUpperBound);
				hRes = MAPI_E_INVALID_PARAMETER;
			}
			else
			{
				SPropTagArray tag = {0};
				tag.cValues = 1;
				lptag = &tag;
				ULONG iTag = 0;
				for (iTag = ulLowerBound ; iTag <= ulUpperBound ; iTag++)
				{
					LPMAPINAMEID* lppPropNames = 0;
					ULONG ulPropNames = 0;
					hRes = S_OK;
					tag.aulPropTag[0] = PROP_TAG(NULL,iTag);

					WC_H(GetNamesFromIDs(m_lpMAPIProp,
						&lptag,
						NULL,
						NULL,
						&ulPropNames,
						&lppPropNames));
					if (S_OK == hRes && ulPropNames == 1 && lppPropNames && *lppPropNames)
					{
						DebugPrintEx(DBGNamedProp,CLASS,_T("FindAllNamedProps"),_T("Found an ID with a name (0x%X). Adding to extra prop list.\n"),iTag);
						EC_H(AddPropToExtraProps(PROP_TAG(NULL,iTag),false));
					}
					MAPIFreeBuffer(lppPropNames);
					lppPropNames = NULL;
				}
			}
		}
	}

	// Refresh the display
	WC_H(RefreshMAPIPropList());
} // CSingleMAPIPropListCtrl::FindAllNamedProps

void CSingleMAPIPropListCtrl::CountNamedProps()
{
	if (!m_lpMAPIProp) return;

	DebugPrintEx(DBGNamedProp,CLASS,_T("CountNamedProps"),_T("Searching for the highest named prop mapping\n"));

	HRESULT hRes = S_OK;
	ULONG ulLower = 0x8000;
	ULONG ulUpper = 0xFFFF;
	ULONG ulHighestKnown = 0;
	ULONG ulCurrent = (ulUpper+ulLower)/2;

	LPSPropTagArray lptag = NULL;
	SPropTagArray tag = {0};
	LPMAPINAMEID* lppPropNames = 0;
	ULONG ulPropNames = 0;
	lptag = &tag;
	tag.cValues = 1;

	while (ulUpper - ulLower > 1)
	{
		hRes = S_OK;
		tag.aulPropTag[0] = PROP_TAG(NULL,ulCurrent);

		WC_H(GetNamesFromIDs(m_lpMAPIProp,
			&lptag,
			NULL,
			NULL,
			&ulPropNames,
			&lppPropNames));
		if (S_OK == hRes && ulPropNames == 1 && lppPropNames && *lppPropNames)
		{
			// Found a named property, reset lower bound

			// Avoid NameIDToStrings call if we're not debug printing
			if (fIsSet(DBGNamedProp))
			{
				DebugPrintEx(DBGNamedProp,CLASS,_T("CountNamedProps"),_T("Found a named property at 0x%04X.\n"),ulCurrent);
				LPTSTR lpszNameID = NULL;
				LPTSTR lpszNameGUID = NULL;
				NameIDToStrings(lppPropNames[0],tag.aulPropTag[0],&lpszNameID,&lpszNameGUID,NULL);
				DebugPrintEx(DBGNamedProp,CLASS,_T("CountNamedProps"),_T("Name = %s, GUID = %s\n"),lpszNameID, lpszNameGUID);
				FreeNameIDStrings(lpszNameID, lpszNameGUID, NULL);
			}
			ulHighestKnown = ulCurrent;
			ulLower = ulCurrent;
		}
		else
		{
			// Did not find a named property, reset upper bound
			ulUpper = ulCurrent;
		}
		MAPIFreeBuffer(lppPropNames);
		lppPropNames = NULL;

		ulCurrent = (ulUpper+ulLower)/2;
	}

	CEditor MyResult(
		this,
		IDS_COUNTNAMEDPROPS,
		IDS_COUNTNAMEDPROPSPROMPT,
		(ULONG) ulHighestKnown?2:1,
		CEDITOR_BUTTON_OK);
	if (ulHighestKnown)
	{
		tag.aulPropTag[0] = PROP_TAG(NULL,ulHighestKnown);

		hRes = S_OK;
		WC_H(GetNamesFromIDs(m_lpMAPIProp,
			&lptag,
			NULL,
			NULL,
			&ulPropNames,
			&lppPropNames));
		if (S_OK == hRes && ulPropNames == 1 && lppPropNames && *lppPropNames)
		{
			DebugPrintEx(DBGNamedProp,CLASS,_T("CountNamedProps"),_T("Found a named property at 0x%04X.\n"),ulCurrent);
			ulHighestKnown = ulCurrent;
			ulLower = ulCurrent;
		}
		MyResult.InitSingleLine(0,IDS_HIGHESTNAMEDPROPTOTAL,NULL,true);
		MyResult.SetDecimal(0,ulHighestKnown-0x8000);

		MyResult.InitMultiLine(1,IDS_HIGHESTNAMEDPROPNUM,NULL,true);

		if (S_OK == hRes && ulPropNames == 1 && lppPropNames && *lppPropNames)
		{
			CString szNamedProp;
			LPTSTR lpszNameID = NULL;
			LPTSTR lpszNameGUID = NULL;
			NameIDToStrings(lppPropNames[0],tag.aulPropTag[0],&lpszNameID,&lpszNameGUID,NULL);
			szNamedProp.FormatMessage(IDS_HIGHESTNAMEDPROPNAME, ulHighestKnown, lpszNameID, lpszNameGUID);
			FreeNameIDStrings(lpszNameID, lpszNameGUID, NULL);
			MyResult.SetString(1,szNamedProp);

			MAPIFreeBuffer(lppPropNames);
			lppPropNames = NULL;
		}
	}
	else
	{
		MyResult.InitSingleLine(0,IDS_HIGHESTNAMEDPROPTOTAL,NULL,true);
		MyResult.LoadString(0,IDS_HIGHESTNAMEDPROPNOTFOUND);
	}
	hRes = S_OK;
	WC_H(MyResult.DisplayDialog());
} // CSingleMAPIPropListCtrl::CountNamedProps

// Delete the selected property
void CSingleMAPIPropListCtrl::OnDeleteProperty()
{
	HRESULT		hRes = S_OK;
	ULONG		ulPropTag = NULL;

	if (!m_lpMAPIProp) return;

	GetSelectedPropTag(&ulPropTag);
	if (!ulPropTag) return;

	CEditor Query(
		this,
		IDS_DELETEPROPERTY,
		IDS_DELETEPROPERTYPROMPT,
		(ULONG) 0,
		CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);
	WC_H(Query.DisplayDialog());
	if (S_OK == hRes)
	{
		DebugPrintEx(DBGGeneric,CLASS,_T("OnDeleteProperty"),_T("deleting property 0x%08X from %p\n"),ulPropTag,m_lpMAPIProp);

		EC_H(DeleteProperty(m_lpMAPIProp,ulPropTag));

		// Refresh the display
		WC_H(RefreshMAPIPropList());
	}
} // CSingleMAPIPropListCtrl::OnDeleteProperty

// Display the selected property as a security dscriptor using a property sheet
void CSingleMAPIPropListCtrl::OnDisplayPropertyAsSecurityDescriptorPropSheet()
{
	HRESULT					hRes = S_OK;
	ULONG					ulPropTag = NULL;
	CMySecInfo*				MySecInfo = NULL;

	if (!m_lpMAPIProp || !pfnEditSecurity) return;

	GetSelectedPropTag(&ulPropTag);
	if (!ulPropTag) return;

	DebugPrintEx(DBGGeneric,CLASS,_T("OnDisplayPropertyAsSecurityDescriptorPropSheet"),_T("interpreting 0x%X on %p as Security Descriptor\n"), ulPropTag, m_lpMAPIProp);

	MySecInfo = new CMySecInfo(m_lpMAPIProp,ulPropTag);

	if (MySecInfo)
	{
		EC_B(pfnEditSecurity(m_hWnd,MySecInfo));

		MySecInfo->Release();
	}
} // CSingleMAPIPropListCtrl::OnDisplayPropertyAsSecurityDescriptorPropSheet

void CSingleMAPIPropListCtrl::OnEditProp()
{
	ULONG ulPropTag = NULL;

	if (!m_lpMAPIProp && !GetPropVals()) return;

	GetSelectedPropTag(&ulPropTag);
	if (!ulPropTag) return;

	OnEditGivenProp(ulPropTag);
} // CSingleMAPIPropListCtrl::OnEditProp

// Concatenate two property arrays without duplicates
// Entries in the first array trump entries in the second
// Will also eliminate any duplicates already existing within the arrays
_Check_return_ HRESULT ConcatLPSPropValue(
						   ULONG ulVal1,
						   _In_count_(ulVal1) LPSPropValue lpVal1,
						   ULONG ulVal2,
						   _In_count_(ulVal2) LPSPropValue lpVal2,
						   _Out_ ULONG* lpulRetVal,
						   _Deref_out_opt_ LPSPropValue* lppRetVal)
{
	if (!lpulRetVal || !lppRetVal) return MAPI_E_INVALID_PARAMETER;
	if (ulVal1 && !lpVal1) return MAPI_E_INVALID_PARAMETER;
	if (ulVal2 && !lpVal2) return MAPI_E_INVALID_PARAMETER;
	*lpulRetVal = NULL;
	*lppRetVal = NULL;
	HRESULT hRes = S_OK;

	ULONG ulSourceArray = 0;
	ULONG ulTargetArray = 0;
	ULONG ulNewArraySize = NULL;
	LPSPropValue lpNewArray = NULL;

	// Add the sizes of the passed in arrays
	if (ulVal2 && ulVal1)
	{
		ulNewArraySize = ulVal1;
		// Only count props in the second array if they're not in the first
		for (ulSourceArray = 0; ulSourceArray < ulVal2; ulSourceArray++)
		{
			if (!PpropFindProp(lpVal1, ulVal1, CHANGE_PROP_TYPE(lpVal2[ulSourceArray].ulPropTag,PT_UNSPECIFIED)))
			{
				ulNewArraySize++;
			}
		}
	}
	else
	{
		ulNewArraySize = ulVal1 + ulVal2;
	}

	if (ulNewArraySize)
	{
		// Allocate the base array - MyPropCopyMore will allocmore as needed for string/bin/etc
		EC_H(MAPIAllocateBuffer(ulNewArraySize*sizeof(SPropValue),(LPVOID*) &lpNewArray));

		if (SUCCEEDED(hRes) && lpNewArray)
		{
			if (ulVal1)
			{
				for (ulSourceArray = 0;ulSourceArray<ulVal1;ulSourceArray++)
				{
					if (!ulTargetArray || // if it's NULL, we haven't added anything yet
						!PpropFindProp(
						lpNewArray,
						ulTargetArray,
						CHANGE_PROP_TYPE(lpVal1[ulSourceArray].ulPropTag,PT_UNSPECIFIED)))
					{
						EC_H(MyPropCopyMore(
							&lpNewArray[ulTargetArray],
							&lpVal1[ulSourceArray],
							MAPIAllocateMore,
							lpNewArray));
						if (SUCCEEDED(hRes))
						{
							ulTargetArray++;
						}
						else break;
					}
				}
			}

			if (SUCCEEDED(hRes) && ulVal2)
			{
				for (ulSourceArray = 0;ulSourceArray<ulVal2;ulSourceArray++)
				{
					if (!ulTargetArray || // if it's NULL, we haven't added anything yet
						!PpropFindProp(
						lpNewArray,
						ulTargetArray,
						CHANGE_PROP_TYPE(lpVal2[ulSourceArray].ulPropTag,PT_UNSPECIFIED)))
					{
						// make sure we don't overrun.
						if (ulTargetArray >= ulNewArraySize)
						{
							hRes = MAPI_E_CALL_FAILED;
							break;
						}
						EC_H(MyPropCopyMore(
							&lpNewArray[ulTargetArray],
							&lpVal2[ulSourceArray],
							MAPIAllocateMore,
							lpNewArray));
						if (SUCCEEDED(hRes))
						{
							ulTargetArray++;
						}
						else break;
					}
				}
			}

			if (FAILED(hRes))
			{
				MAPIFreeBuffer(lpNewArray);
			}
			else
			{
				*lpulRetVal = ulTargetArray;
				*lppRetVal = lpNewArray;
			}
		}
	}

	return hRes;
} // ConcatLPSPropValue

_Check_return_ HRESULT CSingleMAPIPropListCtrl::SetNewProp(_In_ LPSPropValue lpNewProp)
{
	// Merge the new prop with the old array
	// Pass new prop first so its value will replace old value if it existed

	HRESULT hRes = S_OK;
	ULONG ulNewArray = NULL;
	LPSPropValue lpNewArray = NULL;

	EC_H(ConcatLPSPropValue(
		1,
		lpNewProp,
		m_lpSourceData->cSourceProps,
		m_lpSourceData->lpSourceProps,
		&ulNewArray,
		&lpNewArray));
	if (SUCCEEDED(hRes))
	{
		MAPIFreeBuffer(m_lpSourceData->lpSourceProps);
		m_lpSourceData->cSourceProps = ulNewArray;
		m_lpSourceData->lpSourceProps = lpNewArray;
		m_bRowModified = true;
	}
	return hRes;
} // CSingleMAPIPropListCtrl::SetNewProp

void CSingleMAPIPropListCtrl::OnEditPropAsRestriction(ULONG ulPropTag)
{
	HRESULT hRes = S_OK;

	if (!ulPropTag || PT_SRESTRICTION != PROP_TYPE(ulPropTag)) return;

	LPSPropValue lpEditProp = NULL;
	lpEditProp = PpropFindProp(
		GetPropVals(),
		GetCountPropVals(),
		ulPropTag);

	LPSRestriction lpResIn = NULL;
	if (lpEditProp)
	{
		lpResIn = (LPSRestriction) lpEditProp->Value.lpszA;
	}

	DebugPrint(DBGGeneric,_T("Source restriction before editing:\n"));
	DebugPrintRestriction(DBGGeneric,lpResIn,m_lpMAPIProp);
	CRestrictEditor MyResEditor(
		this,
		NULL, // No alloc parent - we must MAPIFreeBuffer the result
		lpResIn);
	WC_H(MyResEditor.DisplayDialog());

	if (S_OK == hRes)
	{
		LPSRestriction lpModRes = MyResEditor.DetachModifiedSRestriction();
		if (lpModRes)
		{
			DebugPrint(DBGGeneric,_T("Modified restriction:\n"));
			DebugPrintRestriction(DBGGeneric,lpModRes,m_lpMAPIProp);

			// need to merge the data we got back from the CRestrictEditor with our current prop set
			// so that we can free lpModRes
			SPropValue ResProp = {0};
			ResProp.ulPropTag = lpEditProp->ulPropTag;
			ResProp.Value.lpszA = (LPSTR) lpModRes;

			EC_H(SetNewProp(&ResProp));

			// Remember, we had no alloc parent - this is safe to free
			MAPIFreeBuffer(lpModRes);

			// refresh
			WC_H(RefreshMAPIPropList());
		}
	}
} // CSingleMAPIPropListCtrl::OnEditPropAsRestriction

void CSingleMAPIPropListCtrl::OnEditGivenProp(ULONG ulPropTag)
{
	HRESULT hRes = S_OK;
	LPSPropValue lpEditProp = NULL;

	if (!m_lpMAPIProp && !GetPropVals()) return;

	// Explicit check since TagToString is expensive
	if (fIsSet(DBGGeneric))
	{
		DebugPrintEx(DBGGeneric,CLASS,_T("OnEditGivenProp"),_T("editing property 0x%X (==%s) from %p\n"),ulPropTag,(LPCTSTR) TagToString(ulPropTag,m_lpMAPIProp,m_bIsAB,true),m_lpMAPIProp);
	}

	if (PT_SRESTRICTION == PROP_TYPE(ulPropTag))
	{
		OnEditPropAsRestriction(ulPropTag);
		return;
	}

	LPSPropValue lpSourceArray = GetPropVals();
	// if we have m_lpMAPIProp and don't have both lpSourceArray and the reg key,
	// then don't use lpSourceArray
	if (m_lpMAPIProp &&
		(!RegKeys[regkeyUSE_ROW_DATA_FOR_SINGLEPROPLIST].ulCurDWORD || !lpSourceArray))
	{
		lpSourceArray = NULL;
	}
	else if (lpSourceArray)
	{
		// We'll be using lpSourceArray, get the source prop from the array
		lpEditProp = PpropFindProp(
			lpSourceArray,
			GetCountPropVals(),
			ulPropTag);
	}

	LPSPropValue lpModProp = NULL;
	WC_H(DisplayPropertyEditor(
		this,
		IDS_PROPEDITOR,
		NULL,
		m_bIsAB,
		lpSourceArray,
		m_lpMAPIProp,
		ulPropTag,
		false,
		lpEditProp,
		lpSourceArray?&lpModProp:NULL));

	// If we had a source array, we need to shove our results back in to it
	if (S_OK == hRes && lpSourceArray && lpModProp)
	{
		EC_H(SetNewProp(lpModProp));
		// At this point, we're done with lpModProp - it was allocated off of lpSourceArray
		// and freed when a new source array was allocated. Nothing to free here. Move along.
	}
	WC_H(RefreshMAPIPropList());
} // CSingleMAPIPropListCtrl::OnEditGivenProp

// Display the selected property as a stream using CStreamEditor
void CSingleMAPIPropListCtrl::OnEditPropAsStream(ULONG ulType, bool bEditAsRTF)
{
	HRESULT			hRes = S_OK;
	ULONG			ulPropTag = NULL;

	if (!m_lpMAPIProp) return;

	GetSelectedPropTag(&ulPropTag);
	if (!ulPropTag) return;

	// Explicit check since TagToString is expensive
	if (fIsSet(DBGGeneric))
	{
		DebugPrintEx(DBGGeneric,CLASS,_T("OnEditPropAsStream"),_T("editing property 0x%X (== %s) from %p as stream, ulType = 0x%08X, bEditAsRTF = 0x%X\n"),
			ulPropTag,
			(LPCTSTR) TagToString(ulPropTag,m_lpMAPIProp,m_bIsAB,true),
			m_lpMAPIProp,
			ulType,
			bEditAsRTF);
	}

	ulPropTag = CHANGE_PROP_TYPE(ulPropTag,ulType);

	bool bUseWrapEx = false;
	ULONG ulRTFFlags = NULL;
	ULONG ulInCodePage = NULL;
	ULONG ulOutCodePage = CP_ACP; // Default to ANSI - check if this is valid for UNICODE builds

	if (bEditAsRTF)
	{
		CEditor MyPrompt(
			this,
			IDS_USEWRAPEX,
			IDS_USEWRAPEXPROMPT,
			1,
			CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);
		MyPrompt.InitCheck(0,IDS_USEWRAPEX,true,false);

		WC_H(MyPrompt.DisplayDialog());
		if (S_OK == hRes)
		{
			if (MyPrompt.GetCheck(0))
			{
				bUseWrapEx = true;
				SPropTagArray pTag = {0};
				pTag.cValues = 1;
				pTag.aulPropTag[0] = PR_INTERNET_CPID;
				ULONG ulPropVal = NULL;
				LPSPropValue lpProp = NULL;

				WC_MAPI(m_lpMAPIProp->GetProps(
					&pTag,
					fMapiUnicode,
					&ulPropVal,
					&lpProp));
				if (lpProp && 1 == ulPropVal && PT_LONG == PROP_TYPE(lpProp[0].ulPropTag))
				{
					ulInCodePage = lpProp[0].Value.l;
				}

				MAPIFreeBuffer(lpProp);

				CEditor MyPrompt2(
					this,
					IDS_WRAPEXFLAGS,
					IDS_WRAPEXFLAGSPROMPT,
					3,
					CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);
				MyPrompt2.InitSingleLine(0,IDS_WRAPEXFLAGS,NULL,false);
				MyPrompt2.SetHex(0,MAPI_NATIVE_BODY);
				MyPrompt2.InitSingleLine(1,IDS_ULINCODEPAGE,NULL,false);
				MyPrompt2.SetDecimal(1,ulInCodePage);
				MyPrompt2.InitSingleLine(2,IDS_ULOUTCODEPAGE,NULL,false);
				MyPrompt2.SetDecimal(2,0);

				WC_H(MyPrompt2.DisplayDialog());

				if (S_OK == hRes)
				{
					ulRTFFlags = MyPrompt2.GetHex(0);
					ulInCodePage = MyPrompt2.GetDecimal(1);
					ulOutCodePage = MyPrompt2.GetDecimal(2);
				}
			}
		}
	}

	if (S_OK == hRes)
	{
		CStreamEditor MyEditor(
			this,
			IDS_PROPEDITOR,
			IDS_STREAMEDITORPROMPT,
			m_lpMAPIProp,
			ulPropTag,
			m_bIsAB,
			bEditAsRTF,
			bUseWrapEx,
			ulRTFFlags,
			ulInCodePage,
			ulOutCodePage);

		WC_H(MyEditor.DisplayDialog());

		WC_H(RefreshMAPIPropList());
	}
} // CSingleMAPIPropListCtrl::OnEditPropAsStream

void CSingleMAPIPropListCtrl::OnCopyProperty()
{
	// for now, we only copy from objects - copying from rows would be difficult to generalize
	if (!m_lpMapiObjects || !m_lpMAPIProp) return;

	ULONG ulPropTag = NULL;
	GetSelectedPropTag(&ulPropTag);

	m_lpMapiObjects->SetPropertyToCopy(ulPropTag,m_lpMAPIProp);
} // CSingleMAPIPropListCtrl::OnCopyProperty

void CSingleMAPIPropListCtrl::OnParseProperty()
{
	HRESULT			hRes = S_OK;
	ULONG			ulPropTag = NULL;
	LPSPropValue	lpsPropFromGetProps = NULL;
	LPSPropValue	lpsPropToDisplay = NULL;

	if (!m_lpMAPIProp && !GetPropVals()) return;

	GetSelectedPropTag(&ulPropTag);
	// We're going to allow any type of property to come in here, though we'll be interpreting the property as PT_BINARY
	if (!ulPropTag) return;
	DebugPrintEx(DBGGeneric,CLASS,_T("OnParseProperty"),_T("interpreting 0x%X on %p\n"), ulPropTag, m_lpMAPIProp);

	// Get the data to interpret
	if (m_lpMAPIProp && !RegKeys[regkeyUSE_ROW_DATA_FOR_SINGLEPROPLIST].ulCurDWORD)
	{
		// GetLargeBinaryProp will try GetProps first, but will fall back to OpenProperty if it has to
		// This will be most useful for large props
		WC_H(GetLargeBinaryProp(m_lpMAPIProp,ulPropTag,&lpsPropFromGetProps));
		lpsPropToDisplay = lpsPropFromGetProps;
	}
	else if (GetPropVals())
	{
		lpsPropToDisplay = PpropFindProp(GetPropVals(),GetCountPropVals(),ulPropTag);
	}

	if (lpsPropToDisplay && PT_BINARY == PROP_TYPE(lpsPropToDisplay->ulPropTag))
	{
		// Find out how to interpret the data
		DWORD_PTR iStructType = NULL;
		CEditor MyStructurePicker(
			this,
			IDS_SMARTVIEW,
			NULL,
			1,
			CEDITOR_BUTTON_OK | CEDITOR_BUTTON_CANCEL);

		// Skip the first entry in g_uidParsingTypesDropDown, which is 'No Parsing'
		MyStructurePicker.InitDropDownArray(0,IDS_STRUCTURES,g_cuidParsingTypes-1,&g_uidParsingTypes[1],true);
		WC_H(MyStructurePicker.DisplayDialog());
		if (S_OK == hRes)
		{
			iStructType = MyStructurePicker.GetDropDownValue(0);
		}

		if (iStructType)
		{
			// Get the string interpretation
			LPWSTR szString = NULL;
			InterpretBinaryAsString(lpsPropToDisplay->Value.bin,iStructType,m_lpMAPIProp,ulPropTag,&szString);

			// Display our dialog
			if (szString)
			{
				CEditor MyResults(
					this,
					IDS_SMARTVIEW,
					NULL,
					2,
					CEDITOR_BUTTON_OK);
				MyResults.InitSingleLine(0,IDS_PROPTAG,NULL,true);
				MyResults.SetHex(0,ulPropTag);
				MyResults.InitMultiLine(1,IDS_SMARTVIEW,NULL,true);
				MyResults.SetStringW(1,szString);

				WC_H(MyResults.DisplayDialog());

				delete[] szString;
			}
		}
	}

	MAPIFreeBuffer(lpsPropFromGetProps);
} // CSingleMAPIPropListCtrl::OnParseProperty

void CSingleMAPIPropListCtrl::OnPasteProperty()
{
	// for now, we only paste to objects - copying to rows would be difficult to generalize
	if (!m_lpHostDlg || !m_lpMapiObjects || !m_lpMAPIProp) return;

	ULONG ulSourcePropTag = m_lpMapiObjects->GetPropertyToCopy();
	LPMAPIPROP lpSourcePropObj = m_lpMapiObjects->GetSourcePropObject();
	if (!lpSourcePropObj) return;

	HRESULT hRes = S_OK;
	LPSPropProblemArray lpProblems = NULL;
	SPropTagArray TagArray = {0};
	TagArray.cValues = 1;
	TagArray.aulPropTag[0] = ulSourcePropTag;

	CEditor MyData(
		this,
		IDS_PASTEPROP,
		IDS_PASTEPROPPROMPT,
		3,
		CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);

	UINT uidDropDown[] = {
		IDS_DDCOPYPROPS,
		IDS_DDGETSETPROPS,
		IDS_DDCOPYSTREAM
	};
	MyData.InitDropDown(0,IDS_COPYSTYLE,_countof(uidDropDown),uidDropDown,true);
	MyData.InitSingleLine(1,IDS_SOURCEPROP,NULL,false);
	MyData.SetHex(1,ulSourcePropTag);
	MyData.InitSingleLine(2,IDS_TARGETPROP,NULL,false);
	MyData.SetHex(2,ulSourcePropTag);

	WC_H(MyData.DisplayDialog());
	if (S_OK == hRes)
	{
		ULONG ulSourceTag = MyData.GetHex(1);
		ULONG ulTargetTag = MyData.GetHex(2);
		TagArray.aulPropTag[0] = ulSourceTag;

		if (PROP_TYPE(ulTargetTag) != PROP_TYPE(ulSourceTag))
			ulTargetTag = CHANGE_PROP_TYPE(ulTargetTag,PROP_TYPE(ulSourceTag));

		switch (MyData.GetDropDown(0))
		{
		case 0:
			{
				CEditor MyCopyData(
					this,
					IDS_PASTEPROP,
					IDS_COPYPASTEPROMPT,
					2,
					CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);

				LPTSTR szGuid = GUIDToStringAndName(&IID_IMAPIProp);
				MyCopyData.InitSingleLineSz(0,IDS_INTERFACE,szGuid,false);
				delete[] szGuid;
				szGuid = NULL;
				MyCopyData.InitSingleLine(1,IDS_FLAGS,NULL,false);
				MyCopyData.SetHex(1,MAPI_DIALOG);

				WC_H(MyCopyData.DisplayDialog());
				if (S_OK == hRes)
				{
					GUID	MyGUID = {0};
					CString szTemp = MyCopyData.GetString(0);
					EC_H(StringToGUID((LPCTSTR) szTemp,&MyGUID));

					LPMAPIPROGRESS lpProgress = GetMAPIProgress(_T("IMAPIProp::CopyProps"), m_lpHostDlg->m_hWnd); // STRING_OK

					ULONG ulCopyFlags = MyCopyData.GetHex(1);

					if (lpProgress)
						ulCopyFlags |= MAPI_DIALOG;

					EC_MAPI(lpSourcePropObj->CopyProps(
						&TagArray,
						lpProgress ? (ULONG_PTR)m_lpHostDlg->m_hWnd : NULL, // ui param
						lpProgress, // progress
						&MyGUID,
						m_lpMAPIProp,
						ulCopyFlags,
						&lpProblems));

					if (lpProgress)
						lpProgress->Release();

					lpProgress = NULL;
				}
			}
			break;
		case 1:
			{
				ULONG			ulValues = NULL;
				LPSPropValue	lpSourceProp = NULL;
				EC_MAPI(lpSourcePropObj->GetProps(
					&TagArray,
					fMapiUnicode,
					&ulValues,
					&lpSourceProp));
				if (!FAILED(hRes) && ulValues && lpSourceProp && PT_ERROR != lpSourceProp->ulPropTag)
				{
					lpSourceProp->ulPropTag = ulTargetTag;
					EC_MAPI(m_lpMAPIProp->SetProps(
						ulValues,
						lpSourceProp,
						&lpProblems));
				}
			}
			break;
		case 2:
			EC_H(CopyPropertyAsStream(lpSourcePropObj,m_lpMAPIProp,ulSourceTag,ulTargetTag));
			break;
		}
	}

	EC_PROBLEMARRAY(lpProblems);
	MAPIFreeBuffer(lpProblems);

	if (!FAILED(hRes))
	{
		EC_MAPI(m_lpMAPIProp->SaveChanges(KEEP_OPEN_READWRITE));

		// refresh
		WC_H(RefreshMAPIPropList());
	}

	lpSourcePropObj->Release();
} // CSingleMAPIPropListCtrl::OnPasteProperty

void CSingleMAPIPropListCtrl::OnCopyTo()
{
	// for now, we only copy from objects - copying from rows would be difficult to generalize
	if (!m_lpHostDlg || !m_lpMapiObjects || !m_lpMAPIProp) return;

	LPMAPIPROP lpSourcePropObj = m_lpMapiObjects->GetSourcePropObject();
	if (!lpSourcePropObj) return;

	HRESULT hRes = S_OK;
	LPSPropProblemArray lpProblems = NULL;

	CEditor MyData(
		this,
		IDS_COPYTO,
		IDS_COPYPASTEPROMPT,
		2,
		CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);

	LPTSTR szGuid = GUIDToStringAndName(&IID_IMAPIProp);
	MyData.InitSingleLineSz(0,IDS_INTERFACE,szGuid,false);
	delete[] szGuid;
	szGuid = NULL;
	MyData.InitSingleLine(1,IDS_FLAGS,NULL,false);
	MyData.SetHex(1,MAPI_DIALOG);

	WC_H(MyData.DisplayDialog());
	if (S_OK == hRes)
	{
		GUID	MyGUID = {0};
		CString szTemp = MyData.GetString(0);
		EC_H(StringToGUID((LPCTSTR) szTemp,&MyGUID));

		CTagArrayEditor TagEditor(
			this,
			IDS_TAGSTOEXCLUDE,
			IDS_TAGSTOEXCLUDEPROMPT,
			NULL,
			false,
			m_lpMAPIProp);
		WC_H(TagEditor.DisplayDialog());

		if (S_OK == hRes)
		{
			LPSPropTagArray lpTagArray = TagEditor.DetachModifiedTagArray();

			LPMAPIPROGRESS lpProgress = GetMAPIProgress(_T("IMAPIProp::CopyTo"), m_lpHostDlg->m_hWnd); // STRING_OK

			ULONG ulCopyFlags = MyData.GetHex(1);

			if (lpProgress)
				ulCopyFlags |= MAPI_DIALOG;

			EC_MAPI(lpSourcePropObj->CopyTo(
				0,
				NULL, // TODO: exclude interfaces?
				lpTagArray,
				lpProgress ? (ULONG_PTR)m_lpHostDlg->m_hWnd : NULL, // UI param
				lpProgress, // progress
				&MyGUID,
				m_lpMAPIProp,
				ulCopyFlags, // flags
				&lpProblems));
			MAPIFreeBuffer(lpTagArray);

			if (lpProgress)
				lpProgress->Release();

			lpProgress = NULL;
		}
	}
	if (lpProblems)
	{
		EC_PROBLEMARRAY(lpProblems);
		MAPIFreeBuffer(lpProblems);
	}

	if (!FAILED(hRes))
	{
		EC_MAPI(m_lpMAPIProp->SaveChanges(KEEP_OPEN_READWRITE));

		// refresh
		WC_H(RefreshMAPIPropList());
	}

	lpSourcePropObj->Release();
} // CSingleMAPIPropListCtrl::OnCopyTo

void CSingleMAPIPropListCtrl::OnOpenProperty()
{
	HRESULT			hRes = S_OK;
	ULONG			ulPropTag = NULL;

	if (!m_lpHostDlg) return;

	GetSelectedPropTag(&ulPropTag);
	if (!ulPropTag) return;

	DebugPrintEx(DBGGeneric,CLASS,_T("OnOpenProperty"),_T("asked to open 0x%X on %p\n"), ulPropTag, m_lpMAPIProp);
	LPSPropValue lpProp = NULL;
	if (m_lpMAPIProp)
	{
		EC_MAPI(HrGetOneProp(m_lpMAPIProp,ulPropTag,&lpProp));
	}
	else if (GetPropVals())
	{
		lpProp = PpropFindProp(
			GetPropVals(),
			GetCountPropVals(),
			ulPropTag);
	}

	if (!FAILED(hRes) && lpProp)
	{
		if (m_lpMAPIProp && PT_OBJECT == PROP_TYPE(lpProp->ulPropTag))
		{
			EC_H(DisplayTable(
				m_lpMAPIProp,
				lpProp->ulPropTag,
				otDefault,
				m_lpHostDlg));
		}
		else if (PT_BINARY == PROP_TYPE(lpProp->ulPropTag) || PT_MV_BINARY == PROP_TYPE(lpProp->ulPropTag) )
		{
			switch (PROP_TYPE(lpProp->ulPropTag))
			{
			case (PT_BINARY):
				{
					DebugPrintEx(DBGGeneric,CLASS,_T("OnOpenProperty"),_T("property is PT_BINARY\n"));
					m_lpHostDlg->OnOpenEntryID(&lpProp->Value.bin);
					break;
				}
			case (PT_MV_BINARY):
				{
					ULONG i = 0;

					DebugPrintEx(DBGGeneric,CLASS,_T("OnOpenProperty"),_T("property is PT_MV_BINARY\n"));
					if (S_OK == hRes && lpProp && PT_MV_BINARY == PROP_TYPE(lpProp->ulPropTag))
					{
						DebugPrintEx(DBGGeneric,CLASS,_T("OnOpenProperty"),_T("opened MV structure. There are 0x%X binaries in it.\n"),lpProp->Value.MVbin.cValues);
						for (i = 0; i<lpProp->Value.MVbin.cValues; i++)
						{
							m_lpHostDlg->OnOpenEntryID(&lpProp->Value.MVbin.lpbin[i]);
						}
					}
					break;
				}
			}
		}
	}
	if (m_lpMAPIProp)
	{
		MAPIFreeBuffer(lpProp);
	}
} // CSingleMAPIPropListCtrl::OnOpenProperty

void CSingleMAPIPropListCtrl::OnModifyExtraProps()
{
	if (!m_lpMapiObjects) return;
	m_lpMapiObjects->MAPIInitialize(NULL);

	HRESULT hRes = S_OK;

	CTagArrayEditor MyTagArrayEditor(
		this,
		IDS_EXTRAPROPS,
		NULL,
		m_sptExtraProps,
		m_bIsAB,
		m_lpMAPIProp);

	WC_H(MyTagArrayEditor.DisplayDialog());
	if (S_OK != hRes) return;

	LPSPropTagArray lpNewTagArray = MyTagArrayEditor.DetachModifiedTagArray();
	if (lpNewTagArray)
	{
		MAPIFreeBuffer(m_sptExtraProps);
		m_sptExtraProps = lpNewTagArray;
	}

	WC_H(RefreshMAPIPropList());
} // CSingleMAPIPropListCtrl::OnModifyExtraProps

void CSingleMAPIPropListCtrl::OnEditGivenProperty()
{
	if (!m_lpMAPIProp && !GetPropVals()) return;

	// Display a dialog to get a property number.
	HRESULT		hRes = S_OK;

	CPropertyTagEditor MyPropertyTag(
		IDS_EDITGIVENPROP,
		NULL, // prompt
		NULL,
		m_bIsAB,
		m_lpMAPIProp,
		this);

	WC_H(MyPropertyTag.DisplayDialog());
	if (S_OK == hRes)
	{
		OnEditGivenProp(MyPropertyTag.GetPropertyTag());
	}
} // CSingleMAPIPropListCtrl::OnEditGivenProperty

void CSingleMAPIPropListCtrl::OnOpenPropertyAsTable()
{
	if (!m_lpMAPIProp) return;

	// Display a dialog to get a property number.
	HRESULT		hRes = S_OK;
	CPropertyTagEditor MyPropertyTag(
		IDS_OPENPROPASTABLE,
		NULL, // prompt
		NULL,
		m_bIsAB,
		m_lpMAPIProp,
		this);

	WC_H(MyPropertyTag.DisplayDialog());
	if (S_OK == hRes)
	{
		CEditor MyData(
			this,
			IDS_OPENPROPASTABLE,
			IDS_OPENPROPASTABLEPROMPT,
			1,
			CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);

		MyData.InitCheck(0,IDS_OPENASEXTABLE,false,false);

		WC_H(MyData.DisplayDialog());
		if (S_OK == hRes)
		{
			if (MyData.GetCheck(0))
			{
				EC_H(DisplayExchangeTable(
					m_lpMAPIProp,
					CHANGE_PROP_TYPE(MyPropertyTag.GetPropertyTag(),PT_OBJECT),
					otDefault,
					m_lpHostDlg));
			}
			else
			{
				EC_H(DisplayTable(
					m_lpMAPIProp,
					CHANGE_PROP_TYPE(MyPropertyTag.GetPropertyTag(),PT_OBJECT),
					otDefault,
					m_lpHostDlg));
			}
		}
	}
} // CSingleMAPIPropListCtrl::OnOpenPropertyAsTable

void CSingleMAPIPropListCtrl::OnPasteNamedProps()
{
	if (!m_lpMAPIProp || !m_lpMapiObjects) return;

	HRESULT		hRes = S_OK;
	LPENTRYLIST	lpSourceMsgEID = m_lpMapiObjects->GetMessagesToCopy();

	if (m_lpMapiObjects->GetBufferStatus() & BUFFER_MESSAGES
		&& lpSourceMsgEID
		&& 1 == lpSourceMsgEID->cValues)
	{
		CEditor MyData(
			this,
			IDS_PASTENAMEDPROPS,
			IDS_PASTENAMEDPROPSPROMPT,
			3,
			CEDITOR_BUTTON_OK|CEDITOR_BUTTON_CANCEL);

		LPTSTR szGuid = GUIDToStringAndName(&PS_PUBLIC_STRINGS);
		MyData.InitSingleLineSz(0,IDS_GUID,szGuid,false);
		delete[] szGuid;
		szGuid = NULL;
		MyData.InitCheck(1,IDS_MAPIMOVE,false,false);
		MyData.InitCheck(2,IDS_MAPINOREPLACE,false,false);

		WC_H(MyData.DisplayDialog());

		if (S_OK == hRes)
		{
			ULONG		ulObjType = 0;
			LPMAPIPROP	lpSource = NULL;
			GUID		propSetGUID = {0};

			EC_H(StringToGUID(MyData.GetString(0), &propSetGUID));

			if (S_OK == hRes)
			{
				EC_H(CallOpenEntry(
					NULL,
					NULL,
					m_lpMapiObjects->GetSourceParentFolder(),
					NULL,
					lpSourceMsgEID->lpbin,
					NULL,
					MAPI_BEST_ACCESS,
					&ulObjType,
					(LPUNKNOWN*)&lpSource));

				if (S_OK == hRes && MAPI_MESSAGE == ulObjType && lpSource)
				{
					EC_H(CopyNamedProps(
						lpSource,
						&propSetGUID,
						MyData.GetCheck(1),
						MyData.GetCheck(2),
						m_lpMAPIProp,
						m_lpHostDlg->m_hWnd));

					EC_MAPI(m_lpMAPIProp->SaveChanges(KEEP_OPEN_READWRITE));

					WC_H(RefreshMAPIPropList());

					lpSource->Release();
				}
			}
		}
	}
} // CSingleMAPIPropListCtrl::OnPasteNamedProps

_Check_return_ bool CSingleMAPIPropListCtrl::HandleAddInMenu(WORD wMenuSelect)
{
	if (wMenuSelect < ID_ADDINPROPERTYMENU) return false;
	CWaitCursor	Wait; // Change the mouse to an hourglass while we work.

	LPMENUITEM lpAddInMenu = GetAddinMenuItem(m_lpHostDlg->m_hWnd,wMenuSelect);
	if (!lpAddInMenu) return false;

	_AddInMenuParams MyAddInMenuParams = {0};
	MyAddInMenuParams.lpAddInMenu = lpAddInMenu;
	MyAddInMenuParams.ulAddInContext = MENU_CONTEXT_PROPERTY;
	MyAddInMenuParams.hWndParent = m_hWnd;
	MyAddInMenuParams.lpMAPIProp = m_lpMAPIProp;
	if (m_lpMapiObjects)
	{
		MyAddInMenuParams.lpMAPISession = m_lpMapiObjects->GetSession(); // do not release
		MyAddInMenuParams.lpMDB = m_lpMapiObjects->GetMDB(); // do not release
		MyAddInMenuParams.lpAdrBook = m_lpMapiObjects->GetAddrBook(false); // do not release
	}

	SRow MyRow = {0};
	MyRow.cValues = GetCountPropVals();
	MyRow.lpProps = GetPropVals();
	MyAddInMenuParams.lpRow = &MyRow;
	MyAddInMenuParams.ulCurrentFlags |= MENU_FLAGS_ROW;

	GetSelectedPropTag(&MyAddInMenuParams.ulPropTag);

	InvokeAddInMenu(&MyAddInMenuParams);
	return true;
} // CSingleMAPIPropListCtrl::HandleAddInMenu