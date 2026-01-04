#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include "Debug.h"

class FileWatcher
{
public:
	FileWatcher();
	~FileWatcher();

	// Start watching a specific file
	bool WatchFile(const std::string& filePath, std::function<void(const std::string&)> onChange);

	// Stop watching
	void Stop();

	// Check if watching
	bool IsWatching() const { return m_watching; }

private:
	std::string m_filePath;
	std::string m_directory;
	std::string m_fileName;
	std::atomic<bool> m_watching;
	HANDLE m_watchThread;
	HANDLE m_dirHandle;
	std::function<void(const std::string&)> m_onChange;

	// Watch thread function
	static DWORD WINAPI WatchThreadProc(LPVOID param);
	void WatchLoop();

	// Parse file path into directory and filename
	void ParseFilePath(const std::string& filePath);
};
