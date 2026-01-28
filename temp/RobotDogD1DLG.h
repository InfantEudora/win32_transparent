#pragma once

#include "resource.h"		// main symbols
#include "afxwin.h"
#include "RDDefault.h"

#include "hidusage.h"
#include "UsbHidIO.h"


extern "C" {

// This file is in the Windows DDK available from Microsoft.
#include "api\hidsdi.h"
#include "api\hidpi.h"
#include <setupapi.h>
#include <dbt.h>
}
// CRobotDogD1DLG dialog

class CRobotDogD1DLG : public CDialog
{
	DECLARE_DYNAMIC(CRobotDogD1DLG)

public:
	CRobotDogD1DLG(CWnd* pParent = NULL);   // standard constructor
	virtual ~CRobotDogD1DLG();
	// HID
	USHORT								NumHIDDevices;						// Number of HID Devices plugged into the system
	PSP_DEVICE_INTERFACE_DETAIL_DATA	arrayDetailData[RD_MAXHIDDEVICES];	// Array of "Path structures" with every HID Device
	HIDD_ATTRIBUTES						arrayAttributes[RD_MAXHIDDEVICES];	// Array of "Attributes structures" with every HID Device
	HIDP_CAPS							arrayValueCaps[RD_MAXHIDDEVICES];	// Array of "Capabilities structures" with every HID Device
	PSP_DEVICE_INTERFACE_DETAIL_DATA	MyDevPath;							// Path structure of My HID Device
	HIDD_ATTRIBUTES						MyDevAttrib;						// Attributes structure of My HID Device
	HIDP_CAPS							MyDevCaps;							// Capabilities structure of My HID Device
	
	CUsbHidIO							mUsbHidIO;							// The class
	USHORT								StrUsageTableInit;					// This is the pointer to a table of "usage-strings"
	
	// only for the demo
	USAGE tmpUsageList[16];
	ULONG tmpUsageLength;
	USAGE tmpUsageListAUX[64];
	BOOL MyDevPathAux;


// Dialog Data
	enum { IDD = IDD_ROBOTDOGD1_DLG };

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	void CloseHandles();
	void RegisterForDeviceNotifications(HWND myWnd);
	LRESULT Main_OnDeviceChange(WPARAM wParam, LPARAM lParam);


	DECLARE_MESSAGE_MAP()
public:
	CButton m_Robot_On;
	CListBox m_ctlLoadHIDList;
	CListBox m_ctlDeviceCapList;
	CListBox m_ctlDeviceValueList;
	CEdit m_ebOutputReport;
	afx_msg void OnBnClickedRobotOn();
	afx_msg void OnBnClickedReadDevVal();
	afx_msg void OnBnClickedReadDevButtons();
	afx_msg void OnBnClickedWriteDevLED();
	afx_msg void OnBnClickedWriteDevReport();
//	afx_msg void OnClose();
	afx_msg void OnLbnSelchangeListDevices();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	CString m_ebAdquiredAt;
	CEdit m_ctlAdquiredAt;
	CEdit m_ctlAdquiredmScnd;
	CString m_strOutputReport;
	afx_msg void OnBnClickedAbout();
	afx_msg void OnBnClickedSendon();
};

class CRDSensorCfgDlg : public CDialog
{
	DECLARE_DYNAMIC(CRDSensorCfgDlg)

public:
	CRDSensorCfgDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CRDSensorCfgDlg();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedFileUpdate();
	CStatic m_JPCurveImg;
};

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

};