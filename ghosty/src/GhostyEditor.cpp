#include "GhostyEditor.h"
#include "GhostyProcessor.h"
#include "GhostyPresets.h"
#include "full_interface.h"
#include "default_look_and_feel.h"
#include "authentication.h"
#include "skin.h"
#include "GhostyProLogo.h"
#include "BinaryData.h"
#include "load_save.h"

using namespace ghosty;

namespace {
  const char* kTabNames[] { "SYNTH", "MORPH", "EXTRAS" };
  const char* kTabTips[] {
    "The synth: oscillators and wavetable editor, filters, envelopes, LFOs, modulation, effects",
    "The SPIRIT BOARD: morph between sounds",
    "GHOST GATE and HAUNT: Ghosty Pro's own effects (off by default)" };
  // Vital skins for Ghosty Pro's looks (ghosty/resources/skins, made by ghosty/tools/make_skins.py)
  const char* kSkins[] { "default_vitalskin", "ghosty_black_vitalskin", "ghosty_sketch_vitalskin" };   // GHOST, BLACK, LIGHT
}

// the ONE place the open-source projects are named (GPLv3 section 5(d) "Appropriate Legal Notices"): MENU > About / Credits
String GhostyEditor::ghostyAboutText() {
  return "Ghosty Pro 2 by GhostBlock, version 1 (beta). Free software under the GNU General Public License v3 "
         "(GPL-3), with NO WARRANTY.\n\n"
         "The sound engine and synth pages are Vital by Matt Tytel (Vital Audio), GPL-3. Upstream source code: "
         "https://github.com/mtytel/vital\n"
         "Built with JUCE (GPL-3) and other open-source parts; the full list, licences and links are in CREDITS.txt and "
         "LICENSE (GPL-3).txt, installed with the plug-in. Ghosty Pro is not made or endorsed by Vital Audio.\n\n"
         "Our modified source code for this version is available on request (GhostBlock) until we publish it.";
}

GhostyEditor::GhostyEditor(GhostyProcessor& p) : AudioProcessorEditor(&p), SynthGuiInterface(&p, false), proc_(p) {
  {
    // "look3" (2.0.16: GHOST, BLACK, LIGHT). From an older build: SKETCH on or the old LIGHT -> LIGHT, BLACK -> BLACK,
    // GHOST and the removed PURPLE -> GHOST
    int look = 0;
    if (settings().containsKey("look3")) look = settings().getIntValue("look3", 0);
    else {
      const int old = settings().getIntValue("look", 0);
      look = settings().getBoolValue("sketch", false) || old == 3 ? 2 : old == 1 ? 1 : 0;
    }
    setLook((Look) jlimit(0, kNumLooks - 1, look));
    lnf_.refresh();
  }
  setLookAndFeel(&lnf_);
  auto morph = std::make_unique<MorphPage>(p);
  morph->foundPresets = [this] { return finder_.results(); };
  pages_[0] = std::move(morph);
  pages_[1] = std::make_unique<ExtrasPage>(p);
  for (auto& pg : pages_) {
    pg->showPage = [this](int i) { showPage(i); };
    addChildComponent(*pg);
  }
  addChildComponent(vitalHolder_);

  keyboard_ = std::make_unique<MidiKeyboardComponent>(*p.keyboardState(), MidiKeyboardComponent::horizontalKeyboard);
  keyboard_->setAvailableRange(24, 108);
  keyboard_->setLowestVisibleKey(36);
  keyboard_->setKeyWidth(22.0f);
  keyboard_->setWantsKeyboardFocus(false);
  addAndMakeVisible(*keyboard_);

  auto pill = [this](Pill& b, const String& text, const String& tip, std::function<void()> click) {
    b.setButtonText(text);
    b.setTooltip(tip);
    b.onClick = std::move(click);
    addAndMakeVisible(b);
  };
  pill(prev_, "<", "Previous preset", [this] { stepPreset(-1); });
  pill(next_, ">", "Next preset", [this] { stepPreset(1); });
  pill(name_, "", "Pick a preset by type: BASS, LEAD, PAD, PLUCK, KEYS, FX, DRUMS... (the < > arrows step through that type, A-Z)", [this] { presetMenu(); });
  pill(save_, "SAVE", "Save this sound as a preset file", [this] { savePreset(); });
  pill(load_, "LOAD", "Open a preset file from anywhere", [this] { openPreset(); });
  pill(init_, "INIT", "Start from the plain init sound", [this] { proc_.loadInit(); });
  pill(menu_, "MENU", "Preset menu: the preset browser, save with notes, open / export presets and banks, tuning, skins", [this] { mainMenu(); });
  pill(look_, "", "Ghosty Pro's looks: GHOST, BLACK, LIGHT (hand-drawn watercolor)", [this] { setLookIndex(((int) currentLook() + 1) % kNumLooks); });
  pill(bypass_, "BYPASS", "Bypass the synth", [this] { proc_.setControl("bypass", proc_.getControl("bypass") >= 0.5f ? 0.0f : 1.0f); });
  for (int i = 0; i < kNumTabs; ++i)
    pill(tabs_[i], kTabNames[i], kTabTips[i], [this, i] { showPage(i); });
  pill(keyArp_, "ARP", "Arpeggiator: plays the notes you hold one by one, in time with your song (settings on EXTRAS)",
       [this] { proc_.keys().arp = !proc_.keys().arp.load(); });
  pill(keyHold_, "HOLD", "Hold: notes keep playing after you let go; a new chord replaces them; HOLD off lets them go",
       [this] { proc_.keys().hold = !proc_.keys().hold.load(); });
  pill(keyChord_, "", "Chords: one key plays a whole chord (in the scale when SCALE LOCK is on)", [this] { keysMenu(false); });
  pill(keyScale_, "", "Scale lock: every key plays a note of the scale", [this] { keysMenu(true); });
  {
    const char* texts[] { "1/2x", "1x", "2x" };
    const float mults[] { 0.5f, 1.0f, 2.0f };
    for (int i = 0; i < 3; ++i)
      pill(tempo_[i], texts[i], "TEMPO: everything in time with your song (arp, chords, MORPH motion, GATE, HAUNT and the synth's synced "
                                "LFOs and delays) runs at half, normal or double your song's tempo. Saved with the project.",
           [this, m = mults[i]] { proc_.setTempoMultiple(m); refreshTempo(); });
    tempoBpm_.setFont(LnF::font(12.5f));
    tempoBpm_.setJustificationType(Justification::centredLeft);
    tempoBpm_.setTooltip("The tempo the synced parts run at: your song's tempo times 1/2, 1 or 2");
    addAndMakeVisible(tempoBpm_);
  }
  for (auto* b : { &keyArp_, &keyHold_ }) b->setTint(1);
  for (auto* b : { &keyChord_, &keyScale_ }) { b->setTint(2); b->setDropArrow(true); }
  version_.setText("v1 beta", dontSendNotification);
  version_.setFont(LnF::font(11.5f, false));
  save_.setTint(1);
  look_.setTint(1);
  bypass_.setTint(2);
  name_.setDropArrow(true);
  name_.getProperties().set("big", true);
  for (auto& t : tabs_) { t.getProperties().set("big", true); t.setTint(2); }
  version_.setJustificationType(Justification::centred);
  addAndMakeVisible(version_);
  addAndMakeVisible(matrix_);   // the dot-matrix GHOSTY row (plays once on open; hover replays a letter)

  // hosts (FL Studio, Logic, Ableton) may resize the window; it keeps the shape where the SYNTH page fills it (no bars)
  fit_.setSizeLimits(960, fittedHeight(960), fittedWidth(1760), 1760);
  setResizable(true, true);
  setConstrainer(&fit_);
  setSize(kWidth, kHeight);
  refreshTempo();
  showPage(0);   // a new window always opens on the SYNTH page (fresh start: nothing carried over from another instance)
  soundChanged();
  finder_.rescan([sp = Component::SafePointer<GhostyEditor>(this)]() mutable {
    if (sp) sp->indexDirty_ = true;
  });
  startTimerHz(30);
}

GhostyEditor::~GhostyEditor() {
  stopTimer();
  retired_.clear();   // engine editors replaced by a look change, not yet deleted: go now, while everything they use is here
  if (gui_) {
    vitalHolder_.removeChildComponent(gui_.get());
    gui_ = nullptr;   // detaches Vital's OpenGL context before we go
  }
  setLookAndFeel(nullptr);
}

void GhostyEditor::applySkin() {
  // the look's Vital skin: Vital's editor is rebuilt with it (some of its parts read their colours once, when made)
  Skin::defaultSkinResource() = kSkins[(int) currentLook()];
  if (!gui_)
    return;
  // the old editor goes away on the next turn of the message loop, after any of its pending window events
  // (2.0.11 fix: it was deleted by a loose async call; when the window closed first, that delete ran after the window and
  // crashed inside the engine editor's modulation buttons. Now the window owns it until then, and deletes it when it closes.)
  FullInterface* old = gui_.release();
  old->setVisible(false);
  vitalHolder_.removeChildComponent(old);
  retired_.emplace_back(old);
  MessageManager::callAsync([sp = Component::SafePointer<GhostyEditor>(this), old]() mutable {
    if (sp) sp->retired_.erase(std::remove_if(sp->retired_.begin(), sp->retired_.end(), [old](const std::unique_ptr<FullInterface>& g) { return g.get() == old; }), sp->retired_.end());
  });
  if (page_ == 0)
    ensureVitalGui();
}

void GhostyEditor::ensureVitalGui() {
  if (gui_)
    return;
  Skin::defaultSkinResource() = kSkins[(int) currentLook()];
  // as Vital's SynthEditor
  SynthGuiData synth_data(&proc_);
  gui_ = std::make_unique<FullInterface>(&synth_data);
  gui_->setLookAndFeel(DefaultLookAndFeel::instance());
  Authentication::create();
  gui_->reset();
  gui_->setOscilloscopeMemory(proc_.getOscilloscopeMemory());
  gui_->setAudioMemory(proc_.getAudioMemory());
  gui_->animate(LoadSave::shouldAnimateWidgets());
  gui_->setPresetBarHidden(true);   // one preset bar: ours, in the header (its menu, save and browse live behind our MENU button)
  vitalHolder_.addAndMakeVisible(gui_.get());
  resized();
}

void GhostyEditor::showPage(int index) {
  page_ = jlimit(0, kNumTabs - 1, index);
  if (page_ == 0)
    ensureVitalGui();
  for (int t = 1; t < kNumTabs; ++t)
    ourPage(t)->setVisible(t == page_);
  vitalHolder_.setVisible(page_ == 0);
  keyboard_->setVisible(page_ != 0);   // Vital's editor has its own keyboard
  for (int i = 0; i < kNumTabs; ++i)
    tabs_[i].setToggleState(i == page_, dontSendNotification);
  if (auto* pg = ourPage(page_))
    pg->sync();
  else if (gui_)
    updateFullGui();
  resized();
  repaint();
}

void GhostyEditor::setLookIndex(int look) {
  look = jlimit(0, kNumLooks - 1, look);
  setLook((Look) look);
#if !GHOSTY_TOOL   // (the test tool switches looks a lot: it never changes the look Brandon picked on his Mac)
  settings().setValue("look3", look);
  settings().saveIfNeeded();
#endif
  lnf_.refresh();
  look_.setButtonText(lookName(currentLook()));
  applySkin();
  sendLookAndFeelChange();
  repaint();
  for (auto& p : pages_) p->repaint();
}

void GhostyEditor::updateFullGui() {
  SynthGuiInterface::updateFullGui();
  proc_.updateHostDisplay();
}

void GhostyEditor::soundChanged() {
  epoch_ = proc_.sound_epoch.load();
  String n = proc_.currentPresetName();
  name_.setButtonText(n.isEmpty() ? String("Init") : n);
  for (auto& p : pages_) p->soundChanged();
}

void GhostyEditor::timerCallback() {
  if (epoch_ != proc_.sound_epoch.load())
    soundChanged();
  if (auto* pg = ourPage(page_))
    pg->sync();
  bypass_.setToggleState(proc_.getControl("bypass") >= 0.5f, dontSendNotification);
  // the header: the ghost's float and glow (cheap, inside its own box) and the GHOSTY row, which repaints only while a tile is moving
  if (isShowing()) {
    repaint(rGhost);
    if (matrix_.tick(Time::getMillisecondCounterHiRes()))
      matrix_.repaint();
  }
  look_.setButtonText(lookName(currentLook()));
  refreshTempo();
  auto& k = proc_.keys();
  keyArp_.setToggleState(k.arp.load(), dontSendNotification);
  keyHold_.setToggleState(k.hold.load(), dontSendNotification);
  keyChord_.setToggleState(k.chord.load() > 0, dontSendNotification);
  keyScale_.setToggleState(k.scale.load() > 0, dontSendNotification);
  const String chordText = "CHORDS: " + chordNames()[k.chord.load()];
  const String scaleText = k.scale.load() > 0 ? "SCALE: " + keyNames()[k.key.load()] + " " + scaleNames()[k.scale.load()] : String("SCALE LOCK: OFF");
  if (keyChord_.getButtonText() != chordText) keyChord_.setButtonText(chordText);
  if (keyScale_.getButtonText() != scaleText) keyScale_.setButtonText(scaleText);
}

void GhostyEditor::refreshTempo() {
  const float tm = proc_.tempoMultiple();
  for (int i = 0; i < 3; ++i) tempo_[i].setToggleState(std::abs(tm - (i == 0 ? 0.5f : i == 1 ? 1.0f : 2.0f)) < 0.01f, dontSendNotification);
  const double host = proc_.hostBpm();
  const String t = host > 0.0 ? String(roundToInt(host * tm * 10.0) / 10.0) + " BPM" : String("TEMPO");
  if (tempoBpm_.getText() != t) tempoBpm_.setText(t, dontSendNotification);
}

void GhostyEditor::keysMenu(bool scale) {
  auto& k = proc_.keys();
  PopupMenu menu;
  menu.setLookAndFeel(&lnf_);
  if (scale) {
    PopupMenu keyMenu;
    for (int i = 0; i < 12; ++i) keyMenu.addItem(100 + i, keyNames()[i], true, k.key.load() == i);
    menu.addSubMenu("KEY: " + keyNames()[k.key.load()], keyMenu);
    menu.addSeparator();
    for (int i = 0; i < scaleNames().size(); ++i) menu.addItem(1 + i, scaleNames()[i], true, k.scale.load() == i);
  }
  else
    for (int i = 0; i < chordNames().size(); ++i) menu.addItem(1 + i, chordNames()[i], true, k.chord.load() == i);
  menu.addSeparator();
  menu.addItem(999, "All KEYS settings...");
  menu.showMenuAsync(PopupMenu::Options().withTargetComponent(scale ? &keyScale_ : &keyChord_), [this, scale](int id) {
    auto& kk = proc_.keys();
    if (id == 999) showPage(2);
    else if (id >= 100) kk.key = id - 100;
    else if (id > 0) { if (scale) kk.scale = id - 1; else kk.chord = id - 1; }
  });
}

void GhostyEditor::rebuildIndex() {
  Array<File> roots = VitalPresetFinder::vitalFolders();
  const File data = VitalPresetFinder::dataFolder();
  if (data != File() && data.isDirectory() && !roots.contains(data)) roots.add(data);
  roots.addArray(VitalPresetFinder::addedFolders());
  const auto found = finder_.results();
  index_.build(found, roots);
  std::set<std::string> favourites;
  try { favourites = LoadSave::getFavorites(); } catch (...) { }
  catalog_.build(factoryPresets(), found, roots, data == File() ? File() : data.getChildFile("User"), favourites);
  indexDirty_ = false;
}

// < > : the next preset of the same TYPE, A-Z (every folder and pack together)
int GhostyEditor::currentEntry() {
  if (indexDirty_) rebuildIndex();
  return proc_.factoryIndex() >= 0 ? catalog_.indexOfFactory(proc_.factoryIndex()) : catalog_.indexOfFile(proc_.getActiveFile());
}

void GhostyEditor::stepPreset(int delta) {
  const int cur = currentEntry();
  if (cur >= 0 && pickerType_ != PresetPicker::kFavourites) pickerType_ = catalog_.entries[(size_t) cur].type;
  const bool favs = pickerType_ == PresetPicker::kFavourites && std::find(catalog_.favourites.begin(), catalog_.favourites.end(), cur) != catalog_.favourites.end();
  if (pickerType_ == PresetPicker::kFavourites && !favs && cur >= 0) pickerType_ = catalog_.entries[(size_t) cur].type;
  const auto& list = favs ? catalog_.favourites : catalog_.byType[jlimit(0, (int) kNumPresetTypes - 1, pickerType_)];
  if (list.empty()) return;
  const int n = (int) list.size();
  const auto at = std::find(list.begin(), list.end(), cur);
  const int pos = at == list.end() ? (delta > 0 ? -1 : 0) : (int) (at - list.begin());
  loadEntry(list[(size_t) (((pos + delta) % n + n) % n)]);
}

void GhostyEditor::loadEntry(int entry) {
  if (!isPositiveAndBelow(entry, catalog_.total())) return;
  const auto& e = catalog_.entries[(size_t) entry];
  String err;
  if (e.factory >= 0) proc_.loadFactoryPreset(e.factory, err);
  else if (!proc_.loadPresetFile(e.file, err) && err.isNotEmpty())
    AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Ghosty Pro 2", err);
}

void GhostyEditor::loadFound(int index) {
  if (!isPositiveAndBelow(index, (int) index_.flat.size())) return;
  String err;
  if (!proc_.loadPresetFile(index_.flat[(size_t) index].file, err) && err.isNotEmpty())
    AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Ghosty Pro 2", err);
}

// the preset picker: one level of TYPES (with counts), each a flat A-Z list of every preset of that type, and a search
void GhostyEditor::presetMenu() {
  const int cur = currentEntry();
  if (cur >= 0) pickerType_ = catalog_.entries[(size_t) cur].type;
  auto picker = std::make_unique<PresetPicker>(catalog_, pickerType_, cur, finder_.scanning(), [sp = Component::SafePointer<GhostyEditor>(this)](int i) mutable {
    if (!sp) return;
    sp->pickerType_ = sp->catalog_.entries[(size_t) i].type;
    sp->loadEntry(i);
  });
  picker->setLookAndFeel(&lnf_);   // our pills and colours (the box is its own window; the look is held weakly)
  CallOutBox::launchAsynchronously(std::move(picker), name_.getScreenBounds(), nullptr);
}

void GhostyEditor::searchPresets() {
  if (indexDirty_) rebuildIndex();
  auto box = std::make_unique<PresetSearch>(index_, [sp = Component::SafePointer<GhostyEditor>(this)](int i) mutable {
    if (sp) sp->loadFound(i);
  });
  CallOutBox::launchAsynchronously(std::move(box), name_.getScreenBounds(), nullptr);
}

// Vital's own actions (its save dialog with author and notes, browser, banks, tuning, skins) run on the SYNTH page's editor
void GhostyEditor::withVital(std::function<void(SynthPresetSelector&)> action) {
  ensureVitalGui();
  if (page_ != 0) showPage(0);
  MessageManager::callAsync([sp = Component::SafePointer<GhostyEditor>(this), action = std::move(action)]() mutable {
    if (sp && sp->gui_)
      if (auto* sel = sp->gui_->presetSelector())
        action(*sel);
  });
}

// MENU: everything Vital's preset bar menu had (and its save icon), now under our one preset bar
void GhostyEditor::mainMenu() {
  PopupMenu menu;
  menu.setLookAndFeel(&lnf_);
  // (presets themselves: the picker under the preset name)
  menu.addItem(2, "Save preset with name, author and notes...");
  menu.addItem(3, "Open a preset file...");
  menu.addItem(4, "Export preset...");
  menu.addSeparator();
  menu.addItem(5, "Import a preset bank...");
  menu.addItem(6, "Export bank...");
  menu.addSeparator();
  {
    const File active = proc_.factoryIndex() < 0 ? proc_.getActiveFile() : File();
    bool fav = false;
    try { fav = active != File() && LoadSave::getFavorites().count(active.getFullPathName().toStdString()) > 0; } catch (...) { }
    menu.addItem(33, fav ? "Remove this preset from FAVOURITES" : "Add this preset to FAVOURITES", active.existsAsFile());
  }
  {
    if (indexDirty_) rebuildIndex();
    const int n = catalog_.hiddenNewer, u = catalog_.hiddenUnreadable;
    if (n > 0) menu.addItem(34, String(n) + (n == 1 ? " preset needs" : " presets need") + " a newer synth version: hidden", false);
    if (u > 0) menu.addItem(35, String(u) + (u == 1 ? " preset file can't be read: hidden" : " preset files can't be read: hidden"), false);
  }
  menu.addItem(30, "Add a preset folder...");
  menu.addItem(31, "Forget the folders I added", !VitalPresetFinder::addedFolders().isEmpty());
  menu.addItem(32, finder_.scanning() ? "Looking for presets..." : "Look for presets again", !finder_.scanning());
  menu.addSeparator();
  menu.addItem(7, "Initialize preset");
  menu.addSeparator();
  menu.addItem(8, "Load tuning file...");
  PopupMenu skin;
  skin.addItem(11, "Open Skin Designer");
  skin.addItem(12, "Load skin...");
  skin.addItem(13, "Load default skin");
  menu.addSubMenu("Skin", skin);
  menu.addSeparator();
  menu.addItem(20, "About / Credits...");
  menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&menu_), [this](int id) {
    switch (id) {
      case 30:
        chooser_ = std::make_unique<FileChooser>("A folder with presets", File::getSpecialLocation(File::userHomeDirectory));
        chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this](const FileChooser& fc) {
          if (fc.getResult().isDirectory()) { VitalPresetFinder::addFolder(fc.getResult()); rescanPresets(); }
        });
        break;
      case 31: for (auto& f : VitalPresetFinder::addedFolders()) VitalPresetFinder::removeFolder(f); rescanPresets(); break;
      case 32: rescanPresets(); break;
      case 33: {
        const File active = proc_.getActiveFile();
        try {
          if (LoadSave::getFavorites().count(active.getFullPathName().toStdString())) LoadSave::removeFavorite(active);
          else LoadSave::addFavorite(active);
        } catch (...) { }
        indexDirty_ = true;
        break;
      }
      case 2: withVital([](SynthPresetSelector& s) { s.savePreset(); }); break;
      case 3: openPreset(); break;
      case 4: withVital([](SynthPresetSelector& s) { s.exportPreset(); }); break;
      case 5: withVital([sp = Component::SafePointer<GhostyEditor>(this)](SynthPresetSelector& s) mutable { s.importBank(); if (sp) sp->rescanPresets(); }); break;
      case 6: withVital([](SynthPresetSelector& s) { s.exportBank(); }); break;
      case 7: proc_.loadInit(); break;
      case 8: withVital([](SynthPresetSelector& s) { s.loadTuningFile(); }); break;
      case 11: withVital([](SynthPresetSelector& s) { s.openSkinDesigner(); }); break;
      case 12: withVital([](SynthPresetSelector& s) { s.loadSkin(); }); break;
      case 20: AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "About / Credits", ghostyAboutText(), "OK"); break;
      case 13: withVital([](SynthPresetSelector& s) { s.clearSkin(); }); break;
      default: break;
    }
  });
}

void GhostyEditor::rescanPresets() {
  indexDirty_ = true;
  finder_.rescan([sp = Component::SafePointer<GhostyEditor>(this)]() mutable { if (sp) sp->indexDirty_ = true; });
}

void GhostyEditor::savePreset() {
  File folder = VitalPresetFinder::dataFolder().getChildFile("User").getChildFile("Presets");
  folder.createDirectory();
  chooser_ = std::make_unique<FileChooser>("Save a preset", folder.getChildFile(proc_.currentPresetName() + ".vital"), "*.vital");
  chooser_->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                        [this](const FileChooser& fc) {
    File f = fc.getResult();
    if (f == File()) return;
    f = f.withFileExtension("vital");
    proc_.setPresetName(f.getFileNameWithoutExtension());
    proc_.savePresetFile(f);
    soundChanged();
    rescanPresets();   // (it shows under MY PRESETS)
  });
}

void GhostyEditor::openPreset() {
  chooser_ = std::make_unique<FileChooser>("Open a preset or bank", File::getSpecialLocation(File::userHomeDirectory), "*.vital;*.vitalbank");
  chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
    File f = fc.getResult();
    if (!f.existsAsFile()) return;
    String err;
    if (f.hasFileExtension("vitalbank")) {
      if (VitalPresetFinder::importBank(f, err)) rescanPresets();
    }
    else
      proc_.loadPresetFile(f, err);
    if (err.isNotEmpty())
      AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Ghosty Pro 2", err);
  });
}

void GhostyEditor::resized() {
  const int W = getWidth(), H = getHeight(), m = kMargin;
  rHeader = { m, 4, W - 2 * m, 66 };
  rTabs = { m, rHeader.getBottom() + 6, W - 2 * m, 34 };   // (the body starts at kChromeH = 116)
  const bool engine = page_ == 0;
  const int kb = jlimit(80, 104, H - 580);   // the keyboard card gives a little height back in short windows
  rKeys = engine ? Rectangle<int>() : Rectangle<int>(m, H - kb - m, W - 2 * m, kb);
  // SYNTH: Vital's editor runs to the window's left, right and bottom edges (the window keeps its shape, see kWidth)
  rBody = engine ? Rectangle<int>(0, kChromeH, W, H - kChromeH)
                 : Rectangle<int>(m, kChromeH, W - 2 * m, rKeys.getY() - 6 - kChromeH);

  // narrower windows (a host may resize down to 960): the header and tab row shrink with the width
  const float f = jlimit(0.72f, 1.0f, (float) (W - 2 * m) / 1240.0f);
  auto sw = [f](int x) { return roundToInt((float) x * f); };
  auto h = rHeader.reduced(14, 17);
  h.removeFromLeft(sw(352));   // the Ghosty Pro logo: the ghost + the GHOSTY row (+ a small PRO 2 tag under it)
  {
    const auto l = layoutHeaderLogo(rHeader, sw(352) - 8);   // the ghost, the GHOSTY row and the PRO 2 tag
    rGhost = l.ghost; rTag = l.tag;
    matrix_.setBounds(l.row);
  }
  prev_.setBounds(h.removeFromLeft(32));
  h.removeFromLeft(4);
  name_.setBounds(h.removeFromLeft(sw(200)));
  h.removeFromLeft(4);
  next_.setBounds(h.removeFromLeft(32));
  h.removeFromLeft(sw(10));
  for (auto* b : { &save_, &load_, &init_ }) { b->setBounds(h.removeFromLeft(sw(66))); h.removeFromLeft(sw(5)); }
  menu_.setBounds(h.removeFromLeft(sw(66))); h.removeFromLeft(sw(5));
  bypass_.setBounds(h.removeFromRight(sw(84)));
  h.removeFromRight(6);
  look_.setBounds(h.removeFromRight(sw(84)));
  h.removeFromRight(6);
  version_.setBounds(h);
  version_.setVisible(h.getWidth() >= 150);

  auto t = rTabs;
  for (int i = 0; i < kNumTabs; ++i) { tabs_[i].setBounds(t.removeFromLeft(sw(130))); t.removeFromLeft(sw(6)); }
  t.removeFromLeft(sw(8));
  for (auto& b : tempo_) { b.setBounds(t.removeFromLeft(sw(b.getButtonText() == "1/2x" ? 52 : 44))); t.removeFromLeft(sw(3)); }
  tempoBpm_.setBounds(t.removeFromLeft(sw(96)).reduced(4, 0));
  keyScale_.setBounds(t.removeFromRight(sw(230)));
  t.removeFromRight(sw(6));
  keyChord_.setBounds(t.removeFromRight(sw(160)));
  t.removeFromRight(sw(6));
  keyHold_.setBounds(t.removeFromRight(sw(80)));
  t.removeFromRight(sw(6));
  keyArp_.setBounds(t.removeFromRight(sw(80)));

  for (auto& p : pages_) p->setBounds(rBody);
  vitalHolder_.setBounds(vitalArea(rBody));
  if (gui_) gui_->setBounds(vitalHolder_.getLocalBounds());
  if (!engine) {
    auto k = rKeys.reduced(14, 12);
    keyboard_->setBounds(k);
    const float keyW = (float) k.getWidth() / 50.0f;   // 50 white keys from C1
    keyboard_->setKeyWidth(keyW);
    keyboard_->setLowestVisibleKey(24);
  }
}

void GhostyEditor::FitConstrainer::checkBounds(Rectangle<int>& b, const Rectangle<int>& prev, const Rectangle<int>& limits,
                                               bool top, bool left, bool bottom, bool right) {
  const bool fromHeight = (top || bottom) && !(left || right);
  int w = b.getWidth(), h = b.getHeight();
  if (fromHeight) { h = jlimit(getMinimumHeight(), getMaximumHeight(), h); w = fittedWidth(h); }
  else            { w = jlimit(getMinimumWidth(), getMaximumWidth(), w); h = fittedHeight(w); }
  if (w > getMaximumWidth()) { w = getMaximumWidth(); h = fittedHeight(w); }
  if (h > getMaximumHeight()) { h = getMaximumHeight(); w = fittedWidth(h); }
  // keep the edge that isn't being dragged where it was
  const int x = left ? prev.getRight() - w : b.getX(), y = top ? prev.getBottom() - h : b.getY();
  b = { x, y, w, h };
  ignoreUnused(limits);
}

Rectangle<int> GhostyEditor::vitalArea(Rectangle<int> area) {
  const double aspect = (double) vital::kDefaultWindowWidth / (double) vital::kDefaultWindowHeight;
  int w = area.getWidth(), h = (int) std::lround(w / aspect);
  if (h > area.getHeight()) { h = area.getHeight(); w = (int) std::lround(h * aspect); }
  return area.withSizeKeepingCentre(w, h);
}

void GhostyEditor::paint(Graphics& g) {
  paintBackground(g, getLocalBounds());
  const auto& p = pal();
  drawCard(g, rHeader.toFloat(), {}, p.accent);
  // the ghost (floating, glowing, fitted inside its box) and the PRO 2 tag (the GHOSTY row is the child component matrix_)
  paintHeaderLogo(g, { rGhost, matrix_.getBounds(), rTag }, (float) (Time::getMillisecondCounter() % 1000000) * 0.001f);
  if (!rKeys.isEmpty())
    drawCard(g, rKeys.toFloat(), {}, p.accent);
}

// a hidden editor does no header work (the timer's header part waits for the window to show again)
void GhostyEditor::visibilityChanged() {
  if (isShowing()) repaint(rHeader);
}
