#pragma once
// MY VITAL PRESETS: finds the .vital presets already on this computer (Vital's own data folder, the folder Vital's
// settings point to, the open-source build's folder, and any folder the user adds) so Ghosty Pro can open them with
// the same engine. Files are only read when picked; nothing is ever copied into Ghosty Pro.
#include "JuceHeader.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace ghosty {

struct FoundPreset {
  File file;
  String name;     // file name without .vital
  String group;    // "Factory", "User", a pack's folder name, or the added folder's name
  int64 size = 0;  // bytes (the same preset copied into two folders shows once)
  String style;    // the preset's own "preset_style" tag (Bass, Lead, Keys, Pad, ...), read from the file's first bytes
  String unusable;  // why this build can't open it ("newer": made with a newer engine version; "unreadable"), or empty
  bool bank = false; // a .vitalbank (a zip of presets): picking it imports it, as Vital's own "Import Bank" does
};

class VitalPresetFinder : private Thread {
  public:
    VitalPresetFinder();
    ~VitalPresetFinder() override;

    // where Vital keeps its presets on this machine (only folders that exist)
    static Array<File> vitalFolders();
    // folders the user added ("ADD FOLDER"); saved in Ghosty Pro's settings
    static Array<File> addedFolders();
    static void addFolder(const File& folder);
    static void removeFolder(const File& folder);
    // Ghosty Pro's own data folder (Vital's data-folder setting in our own config; ~/Music/Ghosty Pro by default):
    // imported banks live here, and Vital's own browser on the ENGINE page reads it
    static File dataFolder();
    static void ensureDataFolder();
    // Vital's "Import Bank": unzip the bank into the data folder and mark the pack installed (LoadSave, unchanged)
    static bool importBank(const File& bank, String& error);

    // scans on a background thread; 'done' is called on the message thread
    void rescan(std::function<void()> done);
    bool scanning() const { return isThreadRunning(); }
    std::vector<FoundPreset> results();   // this finder's last scan, or (before its first one ends) the last scan any window made
    int scansDone() const { return scansDone_.load(); }

    // the "preset_style" tag of a preset file: only the first 4 KB are read (the engine writes its keys A-Z, so the tag sits
    // near the top, before the sound itself)
    static String readStyle(const File& preset);
    // cheap check (the first 4 KB and the last 1 KB, no full load): "" if this build can open it, else "newer" / "unreadable"
    static String unusableReason(const File& preset);

    // blocking scan (tests and the tool)
    static std::vector<FoundPreset> scanNow(const Array<File>& roots);

  private:
    void run() override;
    std::mutex lock_;
    std::vector<FoundPreset> results_;
    std::function<void()> done_;
    std::atomic<int> scansDone_ { 0 };
};

} // namespace ghosty
