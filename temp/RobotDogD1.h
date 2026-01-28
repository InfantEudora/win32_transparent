// RobotDogD1.h : main header file for the RobotDogD1 application
//

//#if !defined(AFX_RobotDogD1_H__0B2AAA82_F5A9_11D3_9F47_0050048108EA__INCLUDED_)
//#define AFX_RobotDogD1_H__0B2AAA82_F5A9_11D3_9F47_0050048108EA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols


/////////////////////////////////////////////////////////////////////////////
// CRobotDogD1App:
// See RobotDogD1.cpp for the implementation of this class
//

class CRobotDogD1App : public CWinApp
{
public:
	CRobotDogD1App();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRobotDogD1App)
	public:
	virtual BOOL InitInstance();
	

	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CRobotDogD1App)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

//#endif // !defined(AFX_RobotDogD1_H__0B2AAA82_F5A9_11D3_9F47_0050048108EA__INCLUDED_)
