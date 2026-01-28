// RobotDogD1.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "RobotDogD1.h"
#include "RobotDogD1DLG.h"
#include <wtypes.h>
#include <setupapi.h>

extern "C" {

#include "api\hidsdi.h"
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CRobotDogD1App

BEGIN_MESSAGE_MAP(CRobotDogD1App, CWinApp)
	//{{AFX_MSG_MAP(CRobotDogD1App)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRobotDogD1App construction

CRobotDogD1App::CRobotDogD1App()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CRobotDogD1App object

CRobotDogD1App theApp;

/////////////////////////////////////////////////////////////////////////////
// CRobotDogD1App initialization

BOOL CRobotDogD1App::InitInstance()
{
	AfxOleInit();
	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

	
	//call this to enable using a rich text box.
	//AfxEnableControlContainer();
	


	
	CRobotDogD1DLG dlg;
	m_pMainWnd = &dlg;

	//AfxInitRichEdit();
	int nResponse = dlg.DoModal();

	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;

	
}

