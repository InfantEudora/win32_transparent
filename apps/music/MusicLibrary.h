#ifndef _MUSIC_LIBRARY_H_
#define _MUSIC_LIBRARY_H_

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
    The sample library as the bench sees it: samples/catalog.csv, plus whatever is on disk under
    samples/ that the catalog does not know about yet.

    IT DOES NOT ANALYSE ANYTHING ITSELF, and cannot. The measurements come from tools/samplescan,
    which decodes mp3 and flac with its own build of miniaudio - and this app links the engine's,
    which has every decoder compiled out and must not meet a second build with a different struct
    layout (3rdparty/miniaudio_config.h). So scanning and exporting run samplescan.exe as a
    process, and this class reads and writes the CSV it leaves behind. The division of labour is
    the catalog's own: samplescan owns the measured columns and rewrites them on every scan, and
    the four human columns - category, instrument, root, comment - are what Classify writes and a
    scan carries over.

    Every entry is in one of four states:
      NOT_SCANNED    on disk, not in the catalog - dropped into unsorted/ since the last scan
      UNCLASSIFIED   scanned, so there is a guess, but nobody has given it a category
      CLASSIFIED     has a category, typed by hand or accepted from the guess
      MISSING        in the catalog, gone from disk

    Thread-safe: the panel, the MCP tools and the scan thread all come through `mutex`.
*/
class MusicLibrary{
public:
    enum State{ NOT_SCANNED, UNCLASSIFIED, CLASSIFIED, MISSING };

    struct Entry{
        std::string file;                           //relative to samples/, forward slashes
        State state = NOT_SCANNED;
        std::map<std::string, std::string> fields;  //every catalog column, by header name
        std::string Get(const std::string& column) const;
    };

    ~MusicLibrary();

    void SetPaths(const std::string& samples_dir, const std::string& samplescan_exe);

    //Re-reads the catalog and re-walks the folder.
    bool Refresh(std::string& error);
    std::vector<Entry> Snapshot();

    /*
        Runs samplescan over the library on its own thread, then refreshes. False if one is
        running. f_only_new measures just the NOT_SCANNED files and leaves every other row as it
        is (samplescan --new) - seconds instead of re-measuring the whole library.
    */
    bool StartScan(bool f_only_new = false);
    bool IsScanning() const { return f_scanning; }
    bool WaitForScan(int timeout_ms);
    std::string Message();                          //what the last scan, save or export said

    /*
        Sets the human columns of one catalogued file - any subset of category, instrument, root
        and comment; an empty value clears one. Refused for a file that has not been scanned (it
        has no row to write to yet) and while a scan is running (samplescan is about to rewrite the
        file from the copy it read at its start).
    */
    bool Classify(const std::string& file, const std::map<std::string, std::string>& human, std::string& error);

    //Decodes a library file to a PCM16 wav at `out_path`, through samplescan. Blocking.
    bool Export(const std::string& file, const std::string& out_path, bool f_trim, std::string& error);

    std::string SamplesDir() const { return samples_dir; }

    //The roles a sample can be given - samplescan's guesses, plus the hand-assigned stinger.
    static const std::vector<std::string>& Categories();
    static const char* StateName(State s);

private:
    std::string samples_dir, samplescan_exe;

    std::mutex mutex;
    std::vector<std::string> header;
    std::vector<Entry> entries;
    std::string message;

    std::thread scan_thread;
    std::atomic<bool> f_scanning{false};

    //Runs samplescan with these arguments, capturing what it prints. Returns its exit code, -1 if
    //it could not be started.
    int RunTool(const std::vector<std::string>& args, std::string& output);
    bool WriteCatalog(std::string& error);          //with `mutex` held
};

#endif
