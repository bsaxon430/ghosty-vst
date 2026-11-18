#include "GhostyPresetMenu.h"
#include "GhostyLook.h"

namespace ghosty {

StringArray MenuIndex::menuPath(const File& file, const Array<File>& roots) {
  File root;
  for (auto& r : roots)
    if (file.isAChildOf(r) && (root == File() || r.getFullPathName().length() > root.getFullPathName().length()))
      root = r;
  StringArray parts;
  if (root == File())
    return parts;
  auto rel = StringArray::fromTokens(file.getParentDirectory().getRelativePathFrom(root), "/\\", "");
  for (auto& p : rel)
    if (p.isNotEmpty() && p != "." && !p.equalsIgnoreCase("Presets") && !p.equalsIgnoreCase("Sorted by GhostBlock"))
      parts.add(p);
  if (parts.isEmpty())
    parts.add(root.getFileName().equalsIgnoreCase("Presets") ? root.getParentDirectory().getFileName() : root.getFileName());
  return parts;
}

void MenuIndex::build(const std::vector<FoundPreset>& found, const Array<File>& roots) {
  root = Node();
  flat.clear(); keyOf.clear(); folders.clear();
  flat.reserve(found.size());
  for (auto& f : found)
    if (!f.bank)
      flat.push_back(f);
  std::stable_sort(flat.begin(), flat.end(), [](const FoundPreset& a, const FoundPreset& b) { return a.name.compareNatural(b.name) < 0; });
  keyOf.resize(flat.size());
  for (int i = 0; i < (int) flat.size(); ++i) {
    const auto parts = menuPath(flat[(size_t) i].file, roots);
    Node* n = &root;
    for (auto& p : parts) { n = &n->kids[p]; n->name = p; }
    n->items.push_back(i);
    keyOf[(size_t) i] = parts.joinIntoString("/");
    folders[keyOf[(size_t) i]].push_back(i);
  }
}

int MenuIndex::indexOf(const File& f) const {
  if (f == File())
    return -1;
  for (int i = 0; i < (int) flat.size(); ++i)
    if (flat[(size_t) i].file == f)
      return i;
  return -1;
}

namespace {
  bool contains(const MenuIndex::Node& n, int idx) {
    if (std::find(n.items.begin(), n.items.end(), idx) != n.items.end())
      return true;
    for (auto& k : n.kids)
      if (contains(k.second, idx))
        return true;
    return false;
  }

  void addItems(PopupMenu& menu, const MenuIndex& ix, const std::vector<int>& items, int ticked) {
    if ((int) items.size() <= MenuIndex::kChunk) {
      for (int i : items)
        menu.addItem(MenuIndex::kFoundBase + i, ix.flat[(size_t) i].name, true, i == ticked);
      return;
    }
    // a big folder: alphabetical ranges, each short enough to scroll through at a glance
    for (size_t s = 0; s < items.size(); s += MenuIndex::kChunk) {
      const size_t e = std::min(items.size(), s + (size_t) MenuIndex::kChunk);
      PopupMenu sub;
      bool has = false;
      for (size_t k = s; k < e; ++k) {
        sub.addItem(MenuIndex::kFoundBase + items[k], ix.flat[(size_t) items[k]].name, true, items[k] == ticked);
        has |= items[k] == ticked;
      }
      auto head = [&](size_t k) { return ix.flat[(size_t) items[k]].name.substring(0, 3).trim(); };
      menu.addSubMenu(head(s) + " - " + head(e - 1) + "  (" + String((int) (e - s)) + ")", sub, true, nullptr, has);
    }
  }

  void addNode(PopupMenu& menu, const MenuIndex& ix, const MenuIndex::Node& n, int ticked) {
    for (auto& k : n.kids) {
      PopupMenu sub;
      addNode(sub, ix, k.second, ticked);
      int count = 0;
      std::function<void(const MenuIndex::Node&)> cnt = [&](const MenuIndex::Node& x) { count += (int) x.items.size(); for (auto& c : x.kids) cnt(c.second); };
      cnt(k.second);
      menu.addSubMenu(k.first + "  (" + String(count) + ")", sub, true, nullptr, ticked >= 0 && contains(k.second, ticked));
    }
    if (!n.items.empty() && !n.kids.empty())
      menu.addSeparator();
    addItems(menu, ix, n.items, ticked);
  }
}

void MenuIndex::addTo(PopupMenu& menu, int ticked) const {
  addNode(menu, *this, root, ticked);
}

// ------------------------------------------------------------------ search
PresetSearch::PresetSearch(const MenuIndex& index, std::function<void(int)> pick) : index_(index), pick_(std::move(pick)) {
  box_.setTextToShowWhenEmpty("Search " + String((int) index.flat.size()) + " presets...", pal().dim);
  box_.setFont(LnF::font(15.0f, false));
  box_.setColour(TextEditor::backgroundColourId, pal().screen);
  box_.setColour(TextEditor::textColourId, pal().ink);
  box_.setColour(TextEditor::outlineColourId, pal().cardEdge);
  box_.setColour(TextEditor::focusedOutlineColourId, pal().accent);
  box_.addListener(this);
  addAndMakeVisible(box_);
  list_.setRowHeight(26);
  list_.setColour(ListBox::backgroundColourId, Colours::transparentBlack);
  addAndMakeVisible(list_);
  setWantsKeyboardFocus(false);
  setSize(380, 440);
  filter();
}

void PresetSearch::resized() {
  auto r = getLocalBounds().reduced(10);
  box_.setBounds(r.removeFromTop(32));
  r.removeFromTop(6);
  list_.setBounds(r);
}

void PresetSearch::paint(Graphics& g) {
  g.fillAll(pal().card);
  g.setColour(pal().dim);
  g.setFont(LnF::font(11.0f, false));
  g.drawText(String((int) shown_.size()) + (total_ > (int) shown_.size() ? " of " + String(total_) : String()) + " presets",
             getLocalBounds().removeFromBottom(14).reduced(12, 0), Justification::centredRight);
}

void PresetSearch::filter() {
  shown_.clear();
  const auto words = StringArray::fromTokens(box_.getText().toLowerCase(), " ", "");
  total_ = 0;
  for (int i = 0; i < (int) index_.flat.size(); ++i) {
    const String hay = (index_.flat[(size_t) i].name + " " + index_.keyOf[(size_t) i]).toLowerCase();
    bool all = true;
    for (auto& w : words) if (!hay.contains(w)) { all = false; break; }
    if (!all) continue;
    ++total_;
    if ((int) shown_.size() < 300) shown_.push_back(i);
  }
  list_.updateContent();
  if (!shown_.empty()) list_.selectRow(0);
  list_.repaint();
  repaint();
}

void PresetSearch::paintListBoxItem(int row, Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= (int) shown_.size()) return;
  const int i = shown_[(size_t) row];
  if (selected) { g.setColour(pal().accent.withAlpha(0.22f)); g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 6.0f); }
  g.setColour(pal().ink);
  g.setFont(LnF::font(14.0f, false));
  g.drawText(index_.flat[(size_t) i].name, 10, 0, w - 130, h, Justification::centredLeft, true);
  g.setColour(pal().dim);
  g.setFont(LnF::font(11.0f, false));
  g.drawText(index_.keyOf[(size_t) i], w - 126, 0, 118, h, Justification::centredRight, true);
}

void PresetSearch::choose(int row) {
  if (row >= 0 && row < (int) shown_.size() && pick_)
    pick_(shown_[(size_t) row]);
}

bool PresetSearch::keyPressed(const KeyPress& k) {
  if (k == KeyPress::downKey) { list_.selectRow(jmin(list_.getSelectedRow() + 1, (int) shown_.size() - 1)); return true; }
  if (k == KeyPress::upKey) { list_.selectRow(jmax(list_.getSelectedRow() - 1, 0)); return true; }
  return false;
}

// ------------------------------------------------------------------ preset types
const char* presetTypeName(int type) {
  static const char* names[] { "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "FX", "DRUMS", "OTHER", "MY PRESETS" };
  return isPositiveAndBelow(type, (int) kNumPresetTypes) ? names[type] : "FAVOURITES";
}

namespace {
  // words in a name or folder that say the type (a word, its plural, or a pack's short tag: "BS Wobble", "LD Supersaw")
  int typeFromWords(const String& text) {
    static const std::vector<std::pair<int, StringArray>> words {
      { kBass,  { "bass", "basses", "bassline", "sub", "subs", "808", "808s", "reese", "wobble", "bs", "ba" } },
      { kLead,  { "lead", "leads", "ld", "solo", "synthlead" } },
      { kPad,   { "pad", "pads", "pd", "atmos", "atmosphere", "ambient", "ambience", "drone", "drones", "choir", "strings", "string", "swell" } },
      { kPluck, { "pluck", "plucks", "plk", "pl", "pk", "arp", "arps", "arpeggio", "bell", "bells", "mallet", "marimba", "kalimba", "pizz", "pizzicato", "harp", "seq", "sequence", "sq" } },
      { kKeys,  { "keys", "key", "ky", "kb", "piano", "pianos", "rhodes", "organ", "organs", "ep", "epiano", "wurli", "chord", "chords", "stab", "stabs", "clav" } },
      { kDrums, { "drum", "drums", "dr", "drm", "kick", "kicks", "snare", "snares", "hat", "hats", "hihat", "perc", "percs", "percussion", "clap", "claps", "tom", "toms", "cymbal", "shaker", "rim" } },
      { kFx,    { "fx", "sfx", "riser", "risers", "impact", "impacts", "sweep", "sweeps", "noise", "downlifter", "uplifter", "whoosh", "transition", "texture", "textures", "glitch" } },
    };
    StringArray tokens;
    tokens.addTokens(text.toLowerCase().replaceCharacters("_-.()[]&+,/", "           "), " ", "");
    tokens.removeEmptyStrings();
    for (auto& w : words)
      for (auto& t : tokens)
        if (w.second.contains(t))
          return w.first;
    return -1;
  }
}

int presetTypeOf(const String& style, const String& name, const StringArray& folders) {
  // 1. the preset's own tag (the engine's styles, plus Pluck and FX that packs use)
  const String st = style.trim().toLowerCase();
  if (st == "bass") return kBass;
  if (st == "lead") return kLead;
  if (st == "pad") return kPad;
  if (st == "keys") return kKeys;
  if (st == "pluck" || st == "plucks") return kPluck;
  if (st == "percussion" || st == "drums" || st == "drum") return kDrums;
  if (st == "sfx" || st == "fx") return kFx;
  // 2. words in its name, 3. its folders (the nearest first)
  int t = typeFromWords(name);
  for (int i = folders.size(); t < 0 && --i >= 0;)
    t = typeFromWords(folders[i]);
  if (t >= 0) return t;
  if (st == "sequence") return kPluck;   // the engine's "Sequence" style is arps and sequenced plucks
  return kOther;                         // unclear: OTHER, never lost
}

void PresetCatalog::build(const std::vector<FactoryPreset>& ours, const std::vector<FoundPreset>& found, const Array<File>& roots,
                          const File& userFolder, const std::set<std::string>& favouritePaths) {
  entries.clear();
  favourites.clear();
  hiddenNewer = hiddenUnreadable = 0;
  for (auto& v : byType) v.clear();
  for (int i = 0; i < (int) ours.size(); ++i) {
    Entry e;
    e.name = ours[(size_t) i].name;
    e.factory = i;
    e.type = presetTypeOf(ours[(size_t) i].category, e.name, {});
    e.where = "Ghosty Pro";
    entries.push_back(e);
  }
  std::set<String> seen;   // the same preset (name and size) copied into two folders or packs: shown once
  for (auto& f : found) {
    if (f.bank) continue;
    if (f.unusable == "newer") { ++hiddenNewer; continue; }
    if (f.unusable.isNotEmpty()) { ++hiddenUnreadable; continue; }
    if (f.size > 0 && !seen.insert(f.name.toLowerCase() + "|" + String(f.size)).second) continue;
    Entry e;
    e.name = f.name;
    e.file = f.file;
    const auto folders = MenuIndex::menuPath(f.file, roots);
    e.where = folders.isEmpty() ? f.group : folders[folders.size() - 1];
    const bool mine = userFolder != File() && f.file.isAChildOf(userFolder);
    e.type = mine ? (int) kMine : presetTypeOf(f.style, f.name, folders);
    entries.push_back(e);
  }
  for (int i = 0; i < (int) entries.size(); ++i) {
    byType[entries[(size_t) i].type].push_back(i);
    if (entries[(size_t) i].file != File() && favouritePaths.count(entries[(size_t) i].file.getFullPathName().toStdString()))
      favourites.push_back(i);
  }
  auto az = [this](int a, int b) {
    const int c = entries[(size_t) a].name.compareNatural(entries[(size_t) b].name);
    return c != 0 ? c < 0 : a < b;
  };
  for (auto& v : byType) std::sort(v.begin(), v.end(), az);
  std::sort(favourites.begin(), favourites.end(), az);
}

int PresetCatalog::indexOfFile(const File& f) const {
  if (f == File()) return -1;
  for (int i = 0; i < (int) entries.size(); ++i)
    if (entries[(size_t) i].factory < 0 && entries[(size_t) i].file == f) return i;
  return -1;
}

int PresetCatalog::indexOfFactory(int factory) const {
  for (int i = 0; i < (int) entries.size(); ++i)
    if (entries[(size_t) i].factory == factory && factory >= 0) return i;
  return -1;
}

// ------------------------------------------------------------------ the picker
PresetPicker::PresetPicker(const PresetCatalog& catalog, int type, int current, bool scanning, std::function<void(int)> pick)
    : cat_(catalog), pick_(std::move(pick)), current_(current), scanning_(scanning) {
  search_.setTextToShowWhenEmpty("Search all " + String(cat_.total()) + " presets...", pal().dim);
  search_.setFont(LnF::font(16.0f, false));
  search_.setColour(TextEditor::backgroundColourId, pal().screen);
  search_.setColour(TextEditor::textColourId, pal().ink);
  search_.setColour(TextEditor::outlineColourId, pal().cardEdge);
  search_.setColour(TextEditor::focusedOutlineColourId, pal().accent);
  search_.setIndents(10, 8);
  search_.addListener(this);
  addAndMakeVisible(search_);
  // one button per type with its count; MY PRESETS and FAVOURITES only when there are some
  for (int t = 0; t <= kFavourites; ++t) {
    const int n = t == kFavourites ? (int) cat_.favourites.size() : (int) cat_.byType[t].size();
    if ((t == kMine || t == kFavourites) && n == 0) continue;
    auto* b = typeButtons_.add(new Pill(String(presetTypeName(t)) + "   " + String(n)));
    b->setTint(t >= kMine ? 1 : 2);
    b->getProperties().set("big", true);
    b->onClick = [this, t] { search_.setText({}, false); showType(t); };
    typeOfButton_.push_back(t);
    addAndMakeVisible(b);
  }
  list_.setRowHeight(32);
  list_.setColour(ListBox::backgroundColourId, Colours::transparentBlack);
  list_.setColour(ListBox::outlineColourId, Colours::transparentBlack);
  addAndMakeVisible(list_);
  setWantsKeyboardFocus(false);
  setSize(720, 560);
  showType(type);
}

std::vector<Component*> PresetPicker::parts() {
  std::vector<Component*> v { &search_, &list_ };
  for (auto* b : typeButtons_) v.push_back(b);
  return v;
}

void PresetPicker::resized() {
  auto r = getLocalBounds().reduced(12);
  search_.setBounds(r.removeFromTop(38));
  r.removeFromTop(10);
  r.removeFromBottom(20);   // the count line
  auto left = r.removeFromLeft(200);
  r.removeFromLeft(12);
  const int h = jmin(40, (left.getHeight() - 6 * (typeButtons_.size() - 1)) / jmax(1, typeButtons_.size()));
  for (auto* b : typeButtons_) { b->setBounds(left.removeFromTop(h)); left.removeFromTop(6); }
  list_.setBounds(r);
}

void PresetPicker::paint(Graphics& g) {
  g.fillAll(pal().card);
  g.setColour(pal().dim);
  g.setFont(LnF::font(12.0f, false));
  String line = search_.getText().isNotEmpty() ? String((int) shown_.size()) + " found"
                                               : String(presetTypeName(type_)) + ": " + String((int) shown_.size()) + " presets, A-Z";
  if (scanning_) line << "   (still looking for presets on this computer...)";
  g.drawText(line, getLocalBounds().reduced(14, 0).removeFromBottom(26), Justification::centredRight);
}

void PresetPicker::showType(int type) {
  type_ = type;
  for (size_t i = 0; i < typeOfButton_.size(); ++i)
    typeButtons_[(int) i]->setToggleState(search_.getText().isEmpty() && typeOfButton_[i] == type_, dontSendNotification);
  refill();
}

void PresetPicker::refill() {
  shown_.clear();
  const String text = search_.getText().trim().toLowerCase();
  if (text.isEmpty()) {
    shown_ = type_ == kFavourites ? cat_.favourites : (isPositiveAndBelow(type_, (int) kNumPresetTypes) ? cat_.byType[type_] : std::vector<int>());
  }
  else {
    const auto words = StringArray::fromTokens(text, " ", "");
    for (int t = 0; t < kNumPresetTypes && shown_.size() < 600; ++t)
      for (int i : cat_.byType[t]) {
        const auto& e = cat_.entries[(size_t) i];
        const String hay = (e.name + " " + e.where + " " + presetTypeName(e.type)).toLowerCase();
        bool all = true;
        for (auto& w : words) if (!hay.contains(w)) { all = false; break; }
        if (all) shown_.push_back(i);
        if (shown_.size() >= 600) break;
      }
    std::sort(shown_.begin(), shown_.end(), [this](int a, int b) { return cat_.entries[(size_t) a].name.compareNatural(cat_.entries[(size_t) b].name) < 0; });
  }
  for (size_t i = 0; i < typeOfButton_.size(); ++i)
    typeButtons_[(int) i]->setToggleState(text.isEmpty() && typeOfButton_[i] == type_, dontSendNotification);
  list_.updateContent();
  const auto at = std::find(shown_.begin(), shown_.end(), current_);
  if (at != shown_.end()) { const int row = (int) (at - shown_.begin()); list_.selectRow(row); list_.scrollToEnsureRowIsOnscreen(row); }
  else if (!shown_.empty()) list_.selectRow(0);
  list_.repaint();
  repaint();
}

void PresetPicker::paintListBoxItem(int row, Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= (int) shown_.size()) return;
  const auto& e = cat_.entries[(size_t) shown_[(size_t) row]];
  if (selected) { g.setColour(pal().accent.withAlpha(0.22f)); g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 6.0f); }
  if (shown_[(size_t) row] == current_) { g.setColour(pal().accent); g.fillEllipse(6.0f, h * 0.5f - 3.0f, 6.0f, 6.0f); }
  g.setColour(pal().ink);
  g.setFont(LnF::font(15.5f, false));
  const int side = jmin(190, w / 3);
  g.drawText(e.name, 18, 0, w - side - 24, h, Justification::centredLeft, true);
  g.setColour(pal().dim);
  g.setFont(LnF::font(11.5f, false));
  const String tag = search_.getText().isNotEmpty() ? String(presetTypeName(e.type)) + "  " + e.where : e.where;
  g.drawText(tag, w - side - 6, 0, side, h, Justification::centredRight, true);
}

void PresetPicker::choose(int row) {
  if (row < 0 || row >= (int) shown_.size()) return;
  const int pick = shown_[(size_t) row];
  auto fn = pick_;
  close();
  if (fn) fn(pick);
}

void PresetPicker::close() {
  if (auto* box = findParentComponentOfClass<CallOutBox>()) box->dismiss();
}

bool PresetPicker::keyPressed(const KeyPress& k) {
  if (k == KeyPress::downKey) { list_.selectRow(jmin(list_.getSelectedRow() + 1, (int) shown_.size() - 1)); return true; }
  if (k == KeyPress::upKey) { list_.selectRow(jmax(list_.getSelectedRow() - 1, 0)); return true; }
  if (k == KeyPress::escapeKey) { close(); return true; }
  return false;
}

} // namespace ghosty
