

#include <wtypes.h>
#include <initguid.h>


// This file is in the Windows DDK available from Microsoft.


#include <setupapi.h>
#include <dbt.h>
extern "C"
{
	#include "hidusage.h"
	#include "hidpi.h"
    #include "hidsdi.h"
}
#include <minwindef.h>
#include <sys/timeb.h>



#define CONF_SUBDIR "\\AINECCCFG\\"
#define CONF_FILE_NAME "RDHIDCFGt.ini"
#define RD_MAXESENSORS	64
#define RD_MAXHIDDEVICES	64
#define RD_MAXDRIVERS	64
#define MAXDEVICEPATH 255


class CUsbHidIO  // CWnd Provides the functionality of ON_MESSAGE(WM_DEVICECHANGE, Main_OnDeviceChange)
{
public:
	CUsbHidIO(void);
	~CUsbHidIO(void);


	// HID
	GUID						HidGuid;
	USHORT						DeviceDetected;
	USHORT						DetailDataFlagDone;

public:

	void CloseHandles();
	DWORD GetHIDCollectionDevices(USHORT &NumHIDDevices,
		PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData[RD_MAXHIDDEVICES],
		PHIDD_ATTRIBUTES aAttributes,
		HIDP_CAPS aNumValueCaps[RD_MAXHIDDEVICES]);
	DWORD GetHIDValueCaps (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		HIDP_REPORT_TYPE ReportType,
		PHIDP_VALUE_CAPS ValueCaps, USHORT &ValueCapsLength,
		HANDLE DeviceHandle = NULL);
	DWORD GetHIDButtonCaps (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		HIDP_REPORT_TYPE ReportType,
		PHIDP_BUTTON_CAPS ButtonCaps, USHORT &ButtonCapsLength,
		HANDLE DeviceHandle = NULL);
	DWORD GetHIDUsagesValues (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		HIDP_REPORT_TYPE ReportType,
		PHIDP_VALUE_CAPS ValueCaps, USHORT ValueCapsLength,
		PULONG UsageValue,DWORD WaitForMsc, __timeb64* pAdquiredAt,
		HANDLE ReadHandle = NULL);
	DWORD GetHIDButtonState (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		HIDP_REPORT_TYPE ReportType,
		USAGE UsagePage, PUSAGE UsageList, PULONG UsageLength,
		DWORD WaitForMsc, __timeb64* AdquiredAt,
		HANDLE ReadHandle = NULL);
	DWORD SetHIDLEDState (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		HIDP_REPORT_TYPE ReportType,
		USAGE UsagePage, PUSAGE UsageList, PULONG UsageLength,
		HANDLE WriteHandle = NULL);
	DWORD SendHIDReport (PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		PCHAR pReport,
		HANDLE tmpWriteHandle = NULL);

	DWORD SendData(PSP_DEVICE_INTERFACE_DETAIL_DATA aDetailData,
		PCHAR pData,
		USHORT len,
		HANDLE tmpWriteHandle = NULL);

};
