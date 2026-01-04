#include "FileWatcher.h"

static Debugger* debug = new Debugger("FileWatcher", DEBUG_ALL);

FileWatcher::FileWatcher()
	: m_watching(false)
	, m_watchThread(nullptr)
	, m_dirHandle(INVALID_HANDLE_VALUE)
{
}

FileWatcher::~FileWatcher()
{
	Stop();
}

void FileWatcher::ParseFilePath(const std::string& filePath)
{
	m_filePath = filePath;

	// Find the last backslash or forward slash
	size_t lastSlash = filePath.find_last_of("\\/");

	if (lastSlash != std::string::npos)
	{
		m_directory = filePath.substr(0, lastSlash);
		m_fileName = filePath.substr(lastSlash + 1);
	}
	else
	{
		// No directory separator, assume current directory
		m_directory = ".";
		m_fileName = filePath;
	}

	debug->Info("Watching file: %s in directory: %s\n", m_fileName.c_str(), m_directory.c_str());
}

bool FileWatcher::WatchFile(const std::string& filePath, std::function<void(const std::string&)> onChange)
{
	if (m_watching)
	{
		debug->Warn("Already watching a file\n");
		return false;
	}

	ParseFilePath(filePath);
	m_onChange = onChange;

	// Open directory handle for monitoring
	m_dirHandle = CreateFileA(
		m_directory.c_str(),
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if (m_dirHandle == INVALID_HANDLE_VALUE)
	{
		debug->Err("Failed to open directory for watching: %d\n", GetLastError());
		return false;
	}

	m_watching = true;

	// Start watch thread
	m_watchThread = CreateThread(
		NULL,
		0,
		WatchThreadProc,
		(LPVOID)this,
		0,
		NULL
	);

	if (!m_watchThread)
	{
		debug->Err("Failed to create watch thread: %d\n", GetLastError());
		CloseHandle(m_dirHandle);
		m_dirHandle = INVALID_HANDLE_VALUE;
		m_watching = false;
		return false;
	}

	debug->Info("Started watching file: %s\n", m_filePath.c_str());
	return true;
}

void FileWatcher::Stop()
{
	if (!m_watching)
		return;

	m_watching = false;

	// Close directory handle to signal thread
	if (m_dirHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_dirHandle);
		m_dirHandle = INVALID_HANDLE_VALUE;
	}

	// Wait for thread to finish
	if (m_watchThread)
	{
		WaitForSingleObject(m_watchThread, INFINITE);
		CloseHandle(m_watchThread);
		m_watchThread = nullptr;
	}

	debug->Info("Stopped watching file: %s\n", m_filePath.c_str());
}

DWORD WINAPI FileWatcher::WatchThreadProc(LPVOID param)
{
	FileWatcher* pThis = (FileWatcher*)param;
	pThis->WatchLoop();
	return 0;
}

void FileWatcher::WatchLoop()
{
	char buffer[4096];
	DWORD bytesReturned;

	debug->Info("File watch loop started\n");

	while (m_watching && m_dirHandle != INVALID_HANDLE_VALUE)
	{
		// Wait for changes in the directory
		BOOL result = ReadDirectoryChangesW(
			m_dirHandle,
			buffer,
			sizeof(buffer),
			FALSE,  // Don't watch subdirectories
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
			&bytesReturned,
			NULL,
			NULL
		);

		if (!result)
		{
			DWORD error = GetLastError();
			if (error != ERROR_INVALID_HANDLE)
			{
				debug->Err("ReadDirectoryChangesW failed: %d\n", error);
			}
			break;
		}

		if (!m_watching)
			break;

		// Process the notifications
		FILE_NOTIFY_INFORMATION* pNotify = (FILE_NOTIFY_INFORMATION*)buffer;

		while (true)
		{
			// Convert wide string filename to regular string
			int fileNameLength = pNotify->FileNameLength / sizeof(WCHAR);
			std::wstring wideFileName(pNotify->FileName, fileNameLength);

			// Convert to narrow string
			int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideFileName.c_str(), -1, NULL, 0, NULL, NULL);
			std::string fileName(sizeNeeded, 0);
			WideCharToMultiByte(CP_UTF8, 0, wideFileName.c_str(), -1, &fileName[0], sizeNeeded, NULL, NULL);

			// Remove null terminator
			if (!fileName.empty() && fileName.back() == '\0')
				fileName.pop_back();

			// Check if this is the file we're watching
			if (fileName == m_fileName)
			{
				debug->Info("File changed: %s (Action: %d)\n", fileName.c_str(), pNotify->Action);

				// Call the callback
				if (m_onChange)
				{
					m_onChange(m_filePath);
				}
			}

			// Move to next notification
			if (pNotify->NextEntryOffset == 0)
				break;

			pNotify = (FILE_NOTIFY_INFORMATION*)((BYTE*)pNotify + pNotify->NextEntryOffset);
		}
	}

	debug->Info("File watch loop ended\n");
}
