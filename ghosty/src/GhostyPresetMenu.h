#pragma once
// The preset drop-down (Serum style) and the preset search for Ghosty Pro 2's one preset bar.
//   MenuIndex   the .vital files on this computer as a folder tree: Vital's own Factory and User folders and any sorted folders
//               (Bass, Leads, Pads ...). Built once from the finder's scan and cached; a rescan builds it again. Big folders are
//               split into alphabetical ranges so no menu has more than ~100 rows to scroll through.
//   PresetSearch  a search box over every preset (name or folder), a list under it; Enter / click loads, Up / Down move.
#include "JuceHeader.h"
#include "VitalPresetFinder.h"
#include "GhostyPresets.h"
#include "GhostyLook.h"
#include <set>
#include <map>
#include <vector>

namespace ghosty {

struct MenuIndex {
  struct Node {
    String name;
    std::map<String, Node> kids;   // sorted by name
    std::vector<int> items;        // indices into flat, sorted by name
  };
  static constexpr int kFoundBase = 100000;   // menu ids of found presets: kFoundBase + index
  static constexpr int kChunk = 100;          // rows per range in a big folder
  Node root;
  std::vector<FoundPreset> flat;              // presets only (banks are imported on BROWSE)
  std::vector<String> keyOf;                  // folder key of each preset ("Bass", "Factory/Pads")
  std::map<String, std::vector<int>> folders; // folder key -> its presets sorted by name (the < > arrows step through these)

  void build(const std::vector<FoundPreset>& found, const Array<File>& roots);
  // the menu folders a preset sits in: the folders under its root, without Vital's wrapper folders (Presets, Sorted by GhostBlock)
  static StringArray menuPath(const File& file, const Array<File>& roots);
  int indexOf(const File& f) const;
  // appends the tree to a menu; the preset with index `ticked` (or -1) gets a tick, and so do its folders
  void addTo(PopupMenu& menu, int ticked) const;
  bool empty() const { return flat.empty(); }
};

class PresetSearch : public Component, private TextEditor::Listener, private ListBoxModel {
  public:
    PresetSearch(const MenuIndex& index, std::function<void(int)> pick);
    void resized() override;
    void paint(Graphics&) override;
    bool keyPressed(const KeyPress&) override;
    void parentHierarchyChanged() override { if (isShowing()) box_.grabKeyboardFocus(); }
  private:
    void textEditorTextChanged(TextEditor&) override { filter(); }
    void textEditorReturnKeyPressed(TextEditor&) override { choose(list_.getSelectedRow()); }
    int getNumRows() override { return (int) shown_.size(); }
    void paintListBoxItem(int row, Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const MouseEvent&) override { choose(row); }
    void filter();
    void choose(int row);
    const MenuIndex& index_;
    std::function<void(int)> pick_;
    TextEditor box_;
    ListBox list_ { {}, this };
    std::vector<int> shown_;
    int total_ = 0;
};

// ------------------------------------------------------------------ the preset picker (Brandon 2026-11-07 16:07)
// One level of TYPES, each one flat A-Z list of EVERY preset of that type from every folder and pack: no sub-folders, nothing
// sliding around. A preset's type comes from its own "preset_style" tag, then the words in its name, then its folders;
// anything unclear goes to OTHER (never lost). The user's own saved presets are MY PRESETS. Every preset is in exactly one
// type; FAVOURITES (the engine's own favourites list) is an extra shortcut on top.
enum PresetType { kBass, kLead, kPad, kPluck, kKeys, kFx, kDrums, kOther, kMine, kNumPresetTypes };
const char* presetTypeName(int type);
int presetTypeOf(const String& style, const String& name, const StringArray& folders);

struct PresetCatalog {
  struct Entry {
    String name;
    int type = kOther;
    int factory = -1;   // one of Ghosty Pro's presets (index into factoryPresets()), or -1: a file on this computer
    File file;
    String where;       // its pack or folder (shown dim on the right)
  };
  std::vector<Entry> entries;
  std::vector<int> byType[kNumPresetTypes];   // entry indices, A-Z
  std::vector<int> favourites;                // A-Z (also in their own type)
  int hiddenNewer = 0, hiddenUnreadable = 0;  // presets this build can't open: left out of every list (the files are untouched)

  void build(const std::vector<FactoryPreset>& ours, const std::vector<FoundPreset>& found, const Array<File>& roots,
             const File& userFolder, const std::set<std::string>& favouritePaths);
  int indexOfFile(const File& f) const;
  int indexOfFactory(int factory) const;
  int total() const { return (int) entries.size(); }
};

class PresetPicker : public Component, private TextEditor::Listener, private ListBoxModel {
  public:
    static constexpr int kFavourites = kNumPresetTypes;   // the list id of FAVOURITES
    PresetPicker(const PresetCatalog& catalog, int type, int current, bool scanning, std::function<void(int)> pick);
    void resized() override;
    void paint(Graphics&) override;
    bool keyPressed(const KeyPress&) override;
    void parentHierarchyChanged() override { if (isShowing()) search_.grabKeyboardFocus(); }
    void showType(int type);
    void refresh(bool scanning, int current);   // the catalog changed (the preset search finished): counts, list and note again
    void setSearch(const String& text) { search_.setText(text, false); refill(); }
    int shownType() const { return type_; }
    const std::vector<int>& shown() const { return shown_; }
    std::vector<Component*> parts();   // (tests: nothing overlaps)
  private:
    void textEditorTextChanged(TextEditor&) override { refill(); }
    void textEditorReturnKeyPressed(TextEditor&) override { choose(list_.getSelectedRow()); }
    void textEditorEscapeKeyPressed(TextEditor&) override { close(); }
    int getNumRows() override { return (int) shown_.size(); }
    void paintListBoxItem(int row, Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const MouseEvent&) override { choose(row); }
    void refill();
    void buildTypeButtons();
    void choose(int row);
    void close();
    const PresetCatalog& cat_;
    std::function<void(int)> pick_;
    TextEditor search_;
    OwnedArray<Pill> typeButtons_;
    std::vector<int> typeOfButton_;
    ListBox list_ { {}, this };
    std::vector<int> shown_;
    int type_ = kBass, current_ = -1;
    bool scanning_ = false;
};

} // namespace ghosty
