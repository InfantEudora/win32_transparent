// RobotDogD1_DLG.cpp : implementation file
//

#include "stdafx.h"
#include "RobotDogD1DLG.h"
#include "hid_esensor.h"
#include "hidusage.h"
#include "RDDefault.h"
#include "RobotDogD1.h"
#include "UsbHidIO.h"



// CRobotDogD1DLG dialog

IMPLEMENT_DYNAMIC(CRobotDogD1DLG, CDialog)

CRobotDogD1DLG::CRobotDogD1DLG(CWnd* pParent /*=NULL*/)
	: CDialog(CRobotDogD1DLG::IDD, pParent)
	, m_ebAdquiredAt(_T(""))
	, m_strOutputReport(_T(""))
{

}

CRobotDogD1DLG::~CRobotDogD1DLG()
{
}

void CRobotDogD1DLG::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_ROBOT_ON, m_Robot_On);
	DDX_Control(pDX, IDC_LISTDEVICES, m_ctlLoadHIDList);
	DDX_Control(pDX, IDC_LISTCAP, m_ctlDeviceCapList);
	DDX_Control(pDX, IDC_LISTVALUES, m_ctlDeviceValueList);
	DDX_Control(pDX, IDC_EDREPORT, m_ebOutputReport);
	DDX_Text(pDX, IDC_ADQUIRED, m_ebAdquiredAt);
	DDX_Control(pDX, IDC_ADQUIRED, m_ctlAdquiredAt);
	DDX_Control(pDX, IDC_MILLISCND, m_ctlAdquiredmScnd);
	DDX_Text(pDX, IDC_EDREPORT, m_strOutputReport);
}
BOOL CRobotDogD1DLG::OnInitDialog()
{
	CDialog::OnInitDialog();

		NumHIDDevices = 0;
		StrUsageTableInit = 20000;
		MyDevPath = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(MAXDEVICEPATH);
		MyDevPath->cbSize = 0;

	return TRUE;  // return TRUE  unless you set the focus to a control

}

void CRobotDogD1DLG::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

BEGIN_MESSAGE_MAP(CRobotDogD1DLG, CDialog)
	ON_BN_CLICKED(IDC_ROBOT_ON, &CRobotDogD1DLG::OnBnClickedRobotOn)
	ON_BN_CLICKED(IDC_READDEVVAL, &CRobotDogD1DLG::OnBnClickedReadDevVal)
	ON_BN_CLICKED(IDC_READDEVBUTTONS, &CRobotDogD1DLG::OnBnClickedReadDevButtons)
	ON_BN_CLICKED(IDC_WRITEDEVLED, &CRobotDogD1DLG::OnBnClickedWriteDevLED)
	ON_BN_CLICKED(IDC_WRITEDEVREPORT, &CRobotDogD1DLG::OnBnClickedWriteDevReport)
	ON_LBN_SELCHANGE(IDC_LISTDEVICES, &CRobotDogD1DLG::OnLbnSelchangeListDevices)
	ON_BN_CLICKED(IDOK, &CRobotDogD1DLG::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CRobotDogD1DLG::OnBnClickedCancel)
	ON_WM_CLOSE()
			//ON_WM_DEVICECHANGE()
	ON_MESSAGE(WM_DEVICECHANGE, Main_OnDeviceChange)

	ON_BN_CLICKED(IDC_BUTTON2, &CRobotDogD1DLG::OnBnClickedAbout)
	ON_BN_CLICKED(IDC_SENDON, &CRobotDogD1DLG::OnBnClickedSendon)
END_MESSAGE_MAP()


CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CRobotDogD1DLG::OnBnClickedRobotOn()
{
			LONG				Result;
			CString csVendorID;
			CString csProductID;
			CString csDescription, csProductName;
			wchar_t strProduct[125], strSerialNum[125], strManufacturer[125]; // maximun 125 This string is a wide-character string
			char str2[125];
			int i, count, tmpint;
			size_t tmpSizeT;
			HANDLE AuxDeviceHandle;


			NumHIDDevices = 0;

	// getting the list of HID devices
	Result = mUsbHidIO.GetHIDCollectionDevices( NumHIDDevices, arrayDetailData, arrayAttributes, arrayValueCaps);

	// RegisterForDeviceNotifications(m_hWnd);
	m_ctlLoadHIDList.ResetContent();

	for (i=0; i<NumHIDDevices;i++)
	{
	for (count=0; count< 125;count++) strProduct[count]=0;
	for (count=0; count< 125;count++) str2[count]=0;
	csDescription.Empty();
	csProductName.Empty();

	csVendorID.Format("%02X", arrayAttributes[i].VendorID);
	csVendorID = csVendorID.Right(4);
	csProductID.Format("%02X", arrayAttributes[i].ProductID);
	csProductID = csProductID.Right(4);
	csDescription = _T("VID:0x") + csVendorID + _T(" PID:0x") + csProductID;

	tmpint = (int)(StrUsageTableInit + 1000 + arrayValueCaps[i].Usage);
	LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
	csDescription = csDescription + _T(" - ") + _T(str2);

	//BEGIN: getting some interesting information about the device -------------
		AuxDeviceHandle=CreateFile (arrayDetailData[i]->DevicePath,
				0, FILE_SHARE_READ|FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)NULL,
				OPEN_EXISTING, 	0, NULL);
		// This function retrieves the product string
		HidD_GetProductString(AuxDeviceHandle,strProduct,sizeof(strProduct));
		// This function retrieves the serial number string
		HidD_GetSerialNumberString (AuxDeviceHandle,strSerialNum,sizeof(strSerialNum) );
		//This function retrieves the manufacturer string
		HidD_GetManufacturerString (AuxDeviceHandle,strManufacturer,sizeof(strManufacturer) );
		CloseHandle(AuxDeviceHandle);
	//END: getting some interesting information about the device -------------

	//
		if (wcstombs_s (&tmpSizeT, str2,(size_t) 125,strProduct,_TRUNCATE) != -1)
		{
			if (tmpSizeT > 2)
			{
				csProductName.Format(" - Product:  %s",str2);
				csDescription = csDescription + csProductName;
			}
			else
			{
				csDescription = csDescription + _T(" - NO DESCRIPTION ");
			}
		}
	m_ctlLoadHIDList.AddString(csDescription);
	}
}

void CRobotDogD1DLG::OnBnClickedReadDevVal()
{
	LONG				Result;

	PHIDP_VALUE_CAPS	mValueCaps;
	USHORT              numValues = 0;
	PULONG				aUsageValue;

	DWORD				WaitForMsc;
	__timeb64			AdquiredAt;
	char				timeline[125];

	CString csDescription, csUsageID, csUsageValue;
	CString csAdquiredAt, csMilliseconds;
	char str2[125];
	int i, count, tmpint;

	WaitForMsc = 100; // Change this value or use "Thread"

	// cheking if the HID device list was loaded
	if (NumHIDDevices <1)
	{
		OnBnClickedRobotOn();
		if (NumHIDDevices < 1)
		{
			MessageBox("Device List Not Ready");
			return;
		}
		else
		{
			OnLbnSelchangeListDevices();
		}
	}

// cleaning the listbox in the dialog
	m_ctlDeviceCapList.ResetContent();
	m_ctlDeviceValueList.ResetContent();

// Begin: Reading Analog Values from CUsbHidIO Class -----------------------------
	if (MyDevCaps.NumberInputValueCaps > 0)
	{

	// allocate memory for the list of capabilities
	mValueCaps = (PHIDP_VALUE_CAPS)
        calloc (MyDevCaps.NumberInputValueCaps, sizeof (HIDP_VALUE_CAPS));

	// calling the class to get all the capabilities of the device selected
	Result = mUsbHidIO.GetHIDValueCaps (MyDevPath ,HidP_Input,mValueCaps,numValues,NULL);

	// allocate memory for the list of values
	aUsageValue =(PULONG)calloc (numValues, sizeof (ULONG));

	// calling the class to get all the values of the device selected
		Result = mUsbHidIO.GetHIDUsagesValues (MyDevPath,
				HidP_Input, mValueCaps, numValues, aUsageValue, WaitForMsc, &AdquiredAt,NULL);
		if (Result == HIDP_STATUS_SUCCESS)
			{
			ctime_s( timeline, 26, & (AdquiredAt.time ) );
			csAdquiredAt = _T(timeline);
			csMilliseconds.Format("%u", AdquiredAt.millitm );
			}
		else
			{
			csAdquiredAt = _T("-:-");
			csMilliseconds = _T("-");
			}

// End: Reading Analog Values --------------------------------


// Begin: Displaying parameters on Dialog  -----------------------------

	for (i=0; i<MyDevCaps.NumberInputValueCaps;i++)
	{
	// Cleaning parameters
		for (count=0; count< 125;count++) str2[count]=0;
		csDescription.Empty();
		csUsageID.Empty();
		csUsageValue.Empty();

	// Reading parameters
		csUsageID.Format("%02X", mValueCaps[i].NotRange.Usage );
		csUsageID = csUsageID.Right(4);

		tmpint = (int)(StrUsageTableInit + (mValueCaps[i].UsagePage *1000) + mValueCaps[i].NotRange.Usage);
		LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
		csDescription = _T("Usage: 0x") + csUsageID + _T(" - ") + _T(str2);
		m_ctlDeviceCapList.AddString(csDescription);

		csUsageValue.Format("%u", aUsageValue[i] );

		csUsageValue = _T("Usage: 0x") + csUsageID + _T(" - Value: ") + csUsageValue;
		m_ctlDeviceValueList.AddString(csUsageValue);
	}

	// sending parameters to Dialog controls
	m_ctlAdquiredAt.SetWindowTextA(csAdquiredAt);
	m_ctlAdquiredmScnd.SetWindowTextA(csMilliseconds);

// End: Displaying parameters on Dialog  -----------------------------


// Cleaning memory --------------------------------
	free(aUsageValue);
	free(mValueCaps);

} // if NumberInputValueCaps > 0

}

void CRobotDogD1DLG::OnBnClickedReadDevButtons()
{
	LONG				Result;

	PHIDP_BUTTON_CAPS	mButtonCaps;
	USHORT				mButtonCapsLength = 0;
	USHORT				numButtons = 0;
	PUSAGE				UsageList;
	PULONG				UsageLength;

	DWORD				WaitForMsc;
	__timeb64			AdquiredAt;
	char				timeline[125];

	CString csDescription, csUsageID, csUsageValue;
	CString csAdquiredAt, csMilliseconds;
	char str2[125];
	int i, count;

	WaitForMsc = 10000;

	if (NumHIDDevices <1)
	{
		OnBnClickedRobotOn();
		if (NumHIDDevices < 1)
		{
			MessageBox("Device List Not Ready");
			return;
		}
		else
		{
			OnLbnSelchangeListDevices();
		}
	}

	m_ctlDeviceCapList.ResetContent();
	m_ctlDeviceValueList.ResetContent();


// Begin: Reading Buttons ------------------------------------
	if (MyDevCaps.NumberInputButtonCaps > 0)
	{

	// allocate memory for the list of capabilities
	mButtonCaps = (PHIDP_BUTTON_CAPS)calloc (MyDevCaps.NumberInputButtonCaps, sizeof (HIDP_BUTTON_CAPS));

	// calling the class to get all the capabilities of the device selected
	Result = mUsbHidIO.GetHIDButtonCaps (MyDevPath,HidP_Input,mButtonCaps,mButtonCapsLength,NULL);

	UsageLength = (PULONG)calloc (MyDevCaps.NumberInputButtonCaps, sizeof (ULONG));

		if (mButtonCaps->IsRange)
			{
            numButtons = mButtonCaps->Range.UsageMax - mButtonCaps->Range.UsageMin + 1;
			}
		else
			{
			numButtons = 1;
			}

	// allocate memory for the list of button states
	UsageList = (PUSAGE)calloc (numButtons, sizeof (USAGE));

	// calling the class to get all the button set to ON of the device selected
		Result = mUsbHidIO.GetHIDButtonState (MyDevPath, HidP_Input, mButtonCaps->UsagePage, UsageList, UsageLength, WaitForMsc, &AdquiredAt,NULL);

		if (Result == HIDP_STATUS_SUCCESS)
			{
			ctime_s( timeline, 26, & (AdquiredAt.time ) );
			csAdquiredAt = _T(timeline);
			csMilliseconds.Format("%u", AdquiredAt.millitm );
			}
		else
			{
			csAdquiredAt = _T("-:-");
			csMilliseconds = _T("-");
			}

// End: Reading Buttons -----------------------------


// Begin: Displaying parameters on Dialog  -----------------------------

	for (i=0; i<MyDevCaps.NumberInputButtonCaps;i++)
	{
	// Cleaning parameters
		for (count=0; count< 125;count++) str2[count]=0;
		csDescription.Empty();
		csUsageID.Empty();

	// Reading parameters
		if (mButtonCaps[i].IsRange)
		{
			csUsageID.Format("%u", mButtonCaps[i].Range.UsageMin);
			csDescription = _T("UsageMin: N�: ") + csUsageID;
			m_ctlDeviceCapList.AddString(csDescription);

			csUsageID.Format("%u", mButtonCaps[i].Range.UsageMax);
			csDescription = _T("UsageMax: N�: ") + csUsageID;
			m_ctlDeviceCapList.AddString(csDescription);

		}
		else
		{
			csUsageID.Format("%u", mButtonCaps[i].NotRange.Usage);
			csDescription = _T("Usage: N�: ") + csUsageID;
			m_ctlDeviceCapList.AddString(csDescription);

		}

	}
	int tmpLegnth = UsageLength[0];
	for (i=0; i<tmpLegnth;i++)
	{
	// Cleaning parameters
		csDescription.Empty();
		csUsageID.Empty();

	// Reading buttons set to ON
		csUsageID.Format("%u", UsageList[i]);
		csDescription = _T("Usage: N�: ") + csUsageID;
		m_ctlDeviceValueList.AddString(csDescription);
	}

	// sending parameters to Dialog controls
	m_ctlAdquiredAt.SetWindowTextA(csAdquiredAt);
	m_ctlAdquiredmScnd.SetWindowTextA(csMilliseconds);

// End: Displaying parameters on Dialog  -----------------------------


// Cleaning all memory blocks
	free(UsageList);
	free(UsageLength);
	free(mButtonCaps);

} // if NumberInputButtonCaps > 0

}

void CRobotDogD1DLG::OnBnClickedWriteDevLED()
{
	LONG				Result;

	USAGE				myLEDUsagePage;
	PUSAGE				myLEDUsageList;
	PULONG				myLEDUsageListLength;
	ULONG				numLED, numLEDON;
	PHIDP_BUTTON_CAPS	mLEDCaps;
	USHORT				mLEDCapsLength = 0;

	CString csDescription, csUsageID, csUsageValue;
	CString csAdquiredAt, csMilliseconds;
	char str2[125];
	int i, usg, count;
	int tmpint;

	myLEDUsagePage = 0x08;
	numLED = 1;

	if (NumHIDDevices <1)
	{
		OnBnClickedRobotOn();
		if (NumHIDDevices < 1)
		{
			MessageBox("Device List Not Ready");
			return;
		}
		else
		{
			OnLbnSelchangeListDevices();
		}
	}
	m_ctlDeviceCapList.ResetContent();
	m_ctlDeviceValueList.ResetContent();

// Begin: Getting list of LEDs ------------------------------------

	if (MyDevCaps.NumberOutputButtonCaps > 0)
	{
	mLEDCaps = (PHIDP_BUTTON_CAPS)calloc (MyDevCaps.NumberOutputButtonCaps, sizeof (HIDP_BUTTON_CAPS));

	Result = mUsbHidIO.GetHIDButtonCaps (MyDevPath,HidP_Output,mLEDCaps,mLEDCapsLength,NULL);

		if (mLEDCaps->IsRange)
			{
            numLED = mLEDCaps->Range.UsageMax - mLEDCaps->Range.UsageMin + 1;
			}
		else
			{
			numLED = 1;
			}

		numLEDON = 1;
		myLEDUsageList = (PUSAGE)calloc (numLEDON, sizeof (USAGE));
		myLEDUsageList[0] = 0x02;

		myLEDUsageListLength = (PULONG)calloc (1, sizeof (ULONG));
		myLEDUsageListLength[0] = 1;

		Result = mUsbHidIO.SetHIDLEDState (MyDevPath, HidP_Output, myLEDUsagePage, myLEDUsageList, myLEDUsageListLength,NULL);

// End: Getting list LEDs -----------------------------


// Begin: Displaying parameters on Dialog  -----------------------------
int Aux = 0; // Auxiliar. Will be needed for "OnBnClickedSendon"

	for (i=0; i<MyDevCaps.NumberOutputButtonCaps;i++)
	{
	// Cleaning parameters
		for (count=0; count< 125;count++) str2[count]=0;
		csDescription.Empty();
		csUsageID.Empty();

	// Reading parameters
		if (mLEDCaps[i].IsRange)
		{
			csUsageID.Format("%02X", mLEDCaps[i].Range.UsageMin);
			csUsageID = csUsageID.Right(4);

			tmpint = (int)(StrUsageTableInit + (mLEDCaps[i].UsagePage *1000) + mLEDCaps[i].Range.UsageMin);
			LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
			csDescription = _T("UsageMin: 0x") + csUsageID + _T(" - ") + _T(str2);
			m_ctlDeviceCapList.AddString(csDescription);
			tmpUsageListAUX[Aux] = mLEDCaps[i].Range.UsageMin; // Auxiliar. Will be needed for "OnBnClickedSendon"
			Aux++;
			for (usg = (mLEDCaps[i].Range.UsageMin) +1; usg< (mLEDCaps[i].Range.UsageMax); usg++)
			{
				csUsageID.Format("%02X", usg);
				csUsageID = csUsageID.Right(4);

				tmpint = (int)(StrUsageTableInit + (mLEDCaps[i].UsagePage *1000) + usg);
				LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
				csDescription = _T("Usage: 0x") + csUsageID + _T(" - ") + _T(str2);
				m_ctlDeviceCapList.AddString(csDescription);
				tmpUsageListAUX[Aux] = usg; // Auxiliar. Will be needed for "OnBnClickedSendon"
				Aux++;

			}
			csUsageID.Format("%02X", mLEDCaps[i].Range.UsageMax);
			csUsageID = csUsageID.Right(4);

			tmpint = (int)(StrUsageTableInit + (mLEDCaps[i].UsagePage *1000) + mLEDCaps[i].Range.UsageMax);
			LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
			csDescription = _T("UsageMax: 0x") + csUsageID + _T(" - ") + _T(str2);
			m_ctlDeviceCapList.AddString(csDescription);
			tmpUsageListAUX[Aux] = mLEDCaps[i].Range.UsageMax; // Auxiliar. Will be needed for "OnBnClickedSendon"
			Aux++;

		}
		else
		{
			csUsageID.Format("%02X", mLEDCaps[i].NotRange.Usage);
			csUsageID = csUsageID.Right(4);

			tmpint = (int)(StrUsageTableInit + (mLEDCaps[i].UsagePage *1000) + mLEDCaps[i].NotRange.Usage);
			LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
			csDescription = _T("Usage: 0x") + csUsageID + _T(" - ") + _T(str2);
			m_ctlDeviceCapList.AddString(csDescription);
			tmpUsageListAUX[Aux] = mLEDCaps[i].NotRange.Usage; // Auxiliar. Will be needed for "OnBnClickedSendon"
			Aux++;
		}
	}

	int tmpLegnth = myLEDUsageListLength[0];

	for (i=0; i<tmpLegnth;i++)
	{
	// Cleaning parameters
		for (count=0; count< 125;count++) str2[count]=0;
		csDescription.Empty();
		csUsageID.Empty();

	// Reading buttons set to ON
		csUsageID.Format("%02X", myLEDUsageList[i] );
		csUsageID = csUsageID.Right(4);

		tmpint = (int)(StrUsageTableInit + (mLEDCaps[i].UsagePage *1000) + myLEDUsageList[i]);
		LoadString(GetModuleHandle(NULL), tmpint, str2, sizeof(str2)/sizeof(char));
		csDescription = _T("Usage: 0x") + csUsageID + _T(" - ") + _T(str2);

		m_ctlDeviceValueList.AddString(csUsageValue);
	}

	// sending parameters to Dialog controls
	m_ctlAdquiredAt.SetWindowTextA(csAdquiredAt);
	m_ctlAdquiredmScnd.SetWindowTextA(csMilliseconds);

// End: Displaying parameters on Dialog  -----------------------------

	// Cleaning all memory blocks
		free(myLEDUsageList);
		free(myLEDUsageListLength);
} // if NumberOutputButtonCaps > 0

}
void CRobotDogD1DLG::OnBnClickedSendon()
{
	int numItemsSelected;
	int n, tmp;
	LPINT ListOfIndexSelected;
	DWORD Result;
	CString LedListTxt;



// Begin: getting list of elements selected from listbox ------------------------------------
	/*
	in order to keep this example as much simple as possible
	I used a little trick to get the usage value selected: get it from "OnBnClickedWriteDevLED"
	*/
		numItemsSelected = m_ctlDeviceCapList.GetSelCount();
		ListOfIndexSelected =  (LPINT) calloc (numItemsSelected,sizeof(INT));
		m_ctlDeviceCapList.GetSelItems(numItemsSelected,ListOfIndexSelected);

		tmpUsageLength = numItemsSelected;
		for (n = 0; n<numItemsSelected; n++)
		{
			tmp = (int)ListOfIndexSelected[n];
			tmpUsageList[n] = tmpUsageListAUX[tmp];
		}
// End: getting list of elements selected from listbox ------------------------------------

// Sending to the HID Device
		Result = mUsbHidIO.SetHIDLEDState (MyDevPath, HidP_Output, 0x08, tmpUsageList, &tmpUsageLength, NULL );

		free(ListOfIndexSelected);
}


void CRobotDogD1DLG::OnBnClickedWriteDevReport()
{

	LONG				Result;

	char OutputReport[32];
	char chrTmp[3];
	UCHAR valor;
	char *stopstring;
	int i;

	if (NumHIDDevices <1)
	{
		OnBnClickedRobotOn();
		if (NumHIDDevices < 1)
		{
			MessageBox("Device List Not Ready");
			return;
		}
		else
		{
			OnLbnSelchangeListDevices();
		}
	}

	m_ebOutputReport.GetWindowTextA(m_strOutputReport);
	m_strOutputReport.MakeUpper();
	memset (OutputReport, (char) 0, 32);

		for (i=0; i<m_strOutputReport.GetLength();i+=2)
		{
			chrTmp[0] = m_strOutputReport.GetAt(i);
			chrTmp[1] = m_strOutputReport.GetAt(i+1);
			chrTmp[2] = 0;
			valor = (char)strtoul(chrTmp,&stopstring,16);
			OutputReport[(i/2)+1]= valor;
		}

// Writing Report ------------------------------------
	Result = mUsbHidIO.SendHIDReport (MyDevPath, OutputReport, NULL);

// Cleaning all memory blocks
}

void CRobotDogD1DLG::CloseHandles()
	{

//	if (DeviceHandle != INVALID_HANDLE_VALUE)
//		{
//		CloseHandle(DeviceHandle);
//		}
		if(MyDevPath->cbSize > 0 && MyDevPath->cbSize <255) free(MyDevPath);
}

void CRobotDogD1DLG::OnLbnSelchangeListDevices()
{
int tmpIndex;
int PathLeght;

	tmpIndex = m_ctlLoadHIDList.GetCurSel();

	if (tmpIndex == CB_ERR) tmpIndex=0; // CB_ERR if no item is selected.

	MyDevPath->cbSize = arrayDetailData[tmpIndex]->cbSize;
	PathLeght = arrayDetailData[tmpIndex]->DevicePath[0]; //First byt is the leght
	memcpy(MyDevPath->DevicePath,
			arrayDetailData[tmpIndex]->DevicePath, PathLeght);

	MyDevAttrib = arrayAttributes[tmpIndex];
	MyDevCaps = arrayValueCaps[tmpIndex];

	MyDevPathAux = true;

}

LRESULT CRobotDogD1DLG::Main_OnDeviceChange(WPARAM wParam, LPARAM lParam)
	{

	PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;

	switch(wParam)
		{
		// Find out if a device has been attached or removed.
		// If yes, see if the name matches the device path name of the device we want to access.

		case DBT_DEVICEARRIVAL:
		//A device has been attached.
			mUsbHidIO.DeviceDetected = TRUE;
			MessageBox("My Device has been attached",_T("HID Device"),MB_ICONINFORMATION);
			return TRUE;

		case DBT_DEVICEREMOVECOMPLETE:
		// Look for the device on the next transfer attempt.
			mUsbHidIO.DeviceDetected = FALSE;
			MessageBox("My Device has been disconnected",_T("HID Device"),MB_ICONWARNING);
			return TRUE;

		default:
			return TRUE;
		}
}
void CRobotDogD1DLG::RegisterForDeviceNotifications(HWND myWnd)
{

	// Request to receive messages when a device is attached or removed.
	// Also see WM_DEVICECHANGE in BEGIN_MESSAGE_MAP(CUsbhidiocDlg, CDialog).

	DEV_BROADCAST_DEVICEINTERFACE DevBroadcastDeviceInterface;
	HDEVNOTIFY DeviceNotificationHandle;

	DevBroadcastDeviceInterface.dbcc_size = sizeof(DevBroadcastDeviceInterface);
	DevBroadcastDeviceInterface.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	DevBroadcastDeviceInterface.dbcc_classguid = mUsbHidIO.HidGuid;

	DeviceNotificationHandle =
		RegisterDeviceNotification(myWnd, &DevBroadcastDeviceInterface, DEVICE_NOTIFY_WINDOW_HANDLE);

}

void CRobotDogD1DLG::OnBnClickedOk()
{
	CloseHandles();
	OnOK();
}

void CRobotDogD1DLG::OnBnClickedCancel()
{
	CloseHandles();
	OnCancel();
}

void CRobotDogD1DLG::OnBnClickedAbout()
{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
}
