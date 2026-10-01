#pragma once
/*
    A PLAYER'S SETTINGS: a small typed store, loaded at startup and saved as it changes.

    Keys are DECLARED first, each with a type, a default and (for numbers) a range - the
    declaration is the whole schema, so there is nothing else to keep in step with it:

        settings.DeclareBool("fullscreen",false);
        settings.DeclareInt("msaa",4,1,16);
        settings.DeclareFloat("volume_master",0.8f,0.0f,1.0f);
        settings.Load(GetExecutableDirectory() + "/settings.json");    //see File.h
        ...
        if (settings.SetFloat("volume_master",0.6f)) settings.Save();

    THE FILE is one flat JSON object, key -> value, written with one key per line:

        {
          "fullscreen": false,
          "msaa": 4,
          "volume_master": 0.8
        }

    It is the player's file and they may edit it, so loading FORGIVES: a missing file is the
    defaults, an unparsable one is the defaults, a key of the wrong type is its default and one
    out of range is clamped - each named in the load's report, never fatal. Keys this build does
    not declare are KEPT and written back, so an older build cannot strip a newer one's settings.
    A save goes through a temporary file and a rename, so a crash mid-write leaves the old file.

    Read with ReadFileToString and an absolute path, NEVER LoadFile: LoadFile is cached and also
    answers from the assets baked into a ship exe, and a player's settings are neither. See
    docs/menu_plan.md.

    THREADS: every call takes the store's lock, so the physics thread may set while the render
    thread reads. Revision() changes on every successful Set, which is how a reader applies only
    what changed without comparing every value every frame.

    No engine dependency beyond the json header: tools/menu_test.cpp tests it without a window.
*/
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Settings{
public:
    //Declare before Load. Declaring a key twice replaces the first declaration.
    void DeclareBool(const char* key, bool def);
    void DeclareInt(const char* key, int def, int lo, int hi);
    void DeclareFloat(const char* key, float def, float lo, float hi);

    /*
        Reads `path` (absolute - see the note above), keeping the defaults for anything missing or
        wrong. Returns true if the file existed and parsed. `report` collects one line per thing
        it had to forgive, for the caller to log; empty when the file was clean. The path is kept
        for Save either way, so a first run writes its file where the next one will look.
    */
    bool Load(const std::string& path, std::string* report = NULL);
    //Writes every declared key (and every unknown one kept from the file). False on any IO error.
    bool Save();

    bool  GetBool(const char* key) const;
    int   GetInt(const char* key) const;
    float GetFloat(const char* key) const;
    //Each clamps to the declared range and returns true if the stored value CHANGED. An
    //undeclared key, or one of another type, is refused (false) rather than created.
    bool  SetBool(const char* key, bool value);
    bool  SetInt(const char* key, int value);
    bool  SetFloat(const char* key, float value);
    //Back to the declared default.
    void  Reset(const char* key);

    uint32_t Revision() const;
    std::string Path() const;
    //The JSON text Save would write, for tests and for showing.
    std::string ToText() const;


private:
    enum Type{ T_BOOL, T_INT, T_FLOAT };
    struct Entry{
        std::string key;
        int   type = T_BOOL;
        bool  b = false, b_def = false;
        int   i = 0, i_def = 0, i_lo = 0, i_hi = 0;
        float f = 0.0f, f_def = 0.0f, f_lo = 0.0f, f_hi = 0.0f;
    };
    Entry*       Find(const char* key);
    const Entry* Find(const char* key) const;
    void         Declare(const Entry& e);
    std::string  TextLocked() const;

    mutable std::mutex mutex;
    std::vector<Entry> entries;             //in declaration order, which is the file's order
    std::vector<std::pair<std::string,std::string>> unknown;   //key -> its JSON text, from the file
    std::string path;
    uint32_t revision = 0;
};
