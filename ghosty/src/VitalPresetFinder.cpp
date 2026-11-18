#include "VitalPresetFinder.h"
#include "GhostyPresets.h"
#include "load_save.h"

namespace ghosty {

namespace {
  constexpr int kMaxPresets = 40000;
  constexpr int kMaxDepth = 6;

  // Vital's own settings file names its data folder ("data_directory") when the user moved it
  File configuredDataFolder(const File& config) {
    if (!config.existsAsFile())
      return {};
    try {
      json data = json::parse(config.loadFileAsString().toStdString());
      if (data.count("data_directory") && data["data_directory"].is_string()) {
        File f(String(data["data_directory"].get<std::string>()));
        if (f.isDirectory())
          return f;
      }
    }
    catch (const std::exception&) { }
    return {};
  }

  void addIfDir(Array<File>& list, const File& f) {
    if (f != File() && f.isDirectory()) {
      for (auto& existing : list)
        if (existing == f || f.isAChildOf(existing))
          return;
      list.add(f);
    }
  }

  void scanFolder(const File& root, const File& folder, int depth, std::vector<FoundPreset>& out) {
    if (depth > kMaxDepth || (int) out.size() >= kMaxPresets)
      return;
    for (const auto& entry : RangedDirectoryIterator(folder, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles)) {
      const File f = entry.getFile();
      // zip leftovers from Macs (banks often carry them): never presets
      if (f.getFileName() == "__MACOSX" || f.getFileName().startsWith("._"))
        continue;
      if (entry.isDirectory()) {
        scanFolder(root, f, depth + 1, out);
      }
      else if (f.hasFileExtension("vital") || f.hasFileExtension("vitalbank")) {
        FoundPreset p;
        p.bank = f.hasFileExtension("vitalbank");
        p.file = f;
        p.name = f.getFileNameWithoutExtension();
        p.size = entry.getFileSize();
        if (!p.bank) { p.style = VitalPresetFinder::readStyle(f); p.unusable = VitalPresetFinder::unusableReason(f); }
        // the first folder under the root: Vital's "Factory", "User" or a pack's name
        File top = f.getParentDirectory();
        while (top.getParentDirectory() != root && top != root && top.getParentDirectory() != top)
          top = top.getParentDirectory();
        p.group = top == root ? root.getFileName() : top.getFileName();
        out.push_back(p);
        if ((int) out.size() >= kMaxPresets)
          return;
      }
    }
  }
}

String VitalPresetFinder::readStyle(const File& preset) {
  FileInputStream in(preset);
  if (!in.openedOk()) return {};
  MemoryBlock head;
  in.readIntoMemoryBlock(head, 4096);
  const String text = head.toString();
  const int k = text.indexOf("\"preset_style\"");
  if (k < 0) return {};
  const int colon = text.indexOfChar(k + 14, ':');
  const int q1 = colon < 0 ? -1 : text.indexOfChar(colon, '"');
  const int q2 = q1 < 0 ? -1 : text.indexOfChar(q1 + 1, '"');
  if (q2 <= q1 || q2 - q1 > 40) return {};
  return text.substring(q1 + 1, q2).trim();
}

String VitalPresetFinder::unusableReason(const File& preset) {
  FileInputStream in(preset);
  if (!in.openedOk() || in.getTotalLength() < 2) return "unreadable";
  MemoryBlock head;
  in.readIntoMemoryBlock(head, 64);
  const String start = head.toString().trimStart();
  if (!start.startsWithChar('{')) return "unreadable";
  // the engine writes its keys A-Z, so "synth_version" sits at the very end (after the sound); older files may have it anywhere
  const int64 len = in.getTotalLength();
  String text;
  for (int64 from : { jmax((int64) 0, len - 1024), (int64) 0 }) {
    in.setPosition(from);
    MemoryBlock part;
    in.readIntoMemoryBlock(part, from == 0 ? 4096 : 1024);
    text = part.toString();
    if (text.contains("\"synth_version\"")) break;
    text = {};
  }
  if (text.isEmpty()) return {};   // no version found cheaply: let the engine decide when it is picked
  const int k = text.indexOf("\"synth_version\"");
  const int q1 = text.indexOfChar(text.indexOfChar(k + 15, ':'), '"');
  const int q2 = q1 < 0 ? -1 : text.indexOfChar(q1 + 1, '"');
  if (q2 <= q1) return {};
  const String version = text.substring(q1 + 1, q2);
  // the engine's own rule (LoadSave::jsonToState): a newer feature version can't be opened
  return LoadSave::compareFeatureVersionStrings(version, GHOSTY_ENGINE_VERSION_STRING) > 0 ? "newer" : "";
}

VitalPresetFinder::VitalPresetFinder() : Thread("Ghosty Pro preset finder") { }

VitalPresetFinder::~VitalPresetFinder() {
  stopThread(4000);
}

Array<File> VitalPresetFinder::vitalFolders() {
  Array<File> list;
  const File home = File::getSpecialLocation(File::userHomeDirectory);
#if JUCE_MAC
  const File support = home.getChildFile("Library/Application Support");
  addIfDir(list, configuredDataFolder(support.getChildFile("vital/Vital.config")));
  addIfDir(list, home.getChildFile("Music/Vital"));
  addIfDir(list, configuredDataFolder(support.getChildFile("vial/Vial.config")));
  addIfDir(list, home.getChildFile("Music/Vial"));
  addIfDir(list, home.getChildFile("Library/Audio/Presets/Vital"));
#elif JUCE_LINUX
  addIfDir(list, configuredDataFolder(home.getChildFile(".vital/Vital.config")));
  String xdg = SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
  if (xdg.isNotEmpty()) {
    addIfDir(list, File(xdg).getChildFile("vital"));
    addIfDir(list, File(xdg).getChildFile("vial"));
  }
  addIfDir(list, home.getChildFile(".local/share/vital"));
  addIfDir(list, home.getChildFile(".local/share/vial"));
#else
  const File docs = File::getSpecialLocation(File::userDocumentsDirectory);
  addIfDir(list, configuredDataFolder(File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("vital/Vital.config")));
  addIfDir(list, docs.getChildFile("Vital"));
  addIfDir(list, docs.getChildFile("Vial"));
#endif
  return list;
}

Array<File> VitalPresetFinder::addedFolders() {
  Array<File> list;
  StringArray paths;
  paths.addLines(settings().getValue("vitalFolders"));
  for (auto& p : paths)
    if (p.isNotEmpty() && File::isAbsolutePath(p))
      addIfDir(list, File(p));
  return list;
}

void VitalPresetFinder::addFolder(const File& folder) {
  StringArray paths;
  paths.addLines(settings().getValue("vitalFolders"));
  paths.addIfNotAlreadyThere(folder.getFullPathName());
  paths.removeEmptyStrings();
  settings().setValue("vitalFolders", paths.joinIntoString("\n"));
  settings().saveIfNeeded();
}

void VitalPresetFinder::removeFolder(const File& folder) {
  StringArray paths;
  paths.addLines(settings().getValue("vitalFolders"));
  paths.removeString(folder.getFullPathName());
  paths.removeEmptyStrings();
  settings().setValue("vitalFolders", paths.joinIntoString("\n"));
  settings().saveIfNeeded();
}

File VitalPresetFinder::dataFolder() {
  return LoadSave::getDataDirectory();
}

void VitalPresetFinder::ensureDataFolder() {
  // Vital's config for this build is Ghosty Pro's own (its folder is named after the product), never Vital's
  json config = LoadSave::getConfigJson();
  if (config.count("data_directory") == 0) {
    File folder = File::getSpecialLocation(File::userMusicDirectory).getChildFile("Ghosty Pro 2");
#if JUCE_LINUX
    folder = File::getSpecialLocation(File::userHomeDirectory).getChildFile(".local/share/ghosty-pro-2");
#endif
    folder.createDirectory();
    if (folder.isDirectory())
      LoadSave::saveDataDirectory(folder);
  }
}

bool VitalPresetFinder::importBank(const File& bank, String& error) {
  // as Vital's SynthPresetSelector::importBank
  FileInputStream input_stream(bank);
  if (!input_stream.openedOk()) {
    error = "Couldn't open that bank.";
    return false;
  }
  ensureDataFolder();
  File data_directory = LoadSave::getDataDirectory();
  data_directory.createDirectory();
  if (!LoadSave::hasDataDirectory())
    LoadSave::saveDataDirectory(data_directory);
  ZipFile import_zip(input_stream);
  Result unzip_result = import_zip.uncompressTo(data_directory);
  if (unzip_result != Result::ok()) {
    error = "That bank couldn't be unpacked: " + unzip_result.getErrorMessage();
    return false;
  }
  LoadSave::markPackInstalled(bank.getFileNameWithoutExtension().toStdString());
  return true;
}

std::vector<FoundPreset> VitalPresetFinder::scanNow(const Array<File>& roots) {
  std::vector<FoundPreset> out;
  for (auto& r : roots)
    scanFolder(r, r, 0, out);
  std::sort(out.begin(), out.end(), [](const FoundPreset& a, const FoundPreset& b) {
    int g = a.group.compareNatural(b.group);
    return g != 0 ? g < 0 : a.name.compareNatural(b.name) < 0;
  });
  return out;
}

void VitalPresetFinder::rescan(std::function<void()> done) {
  stopThread(4000);
  done_ = std::move(done);
  startThread();
}

std::vector<FoundPreset> VitalPresetFinder::results() {
  std::lock_guard<std::mutex> g(lock_);
  return results_;
}

void VitalPresetFinder::run() {
  Array<File> roots = vitalFolders();
  addIfDir(roots, dataFolder());
  roots.addArray(addedFolders());
  auto found = scanNow(roots);
  {
    std::lock_guard<std::mutex> g(lock_);
    results_ = std::move(found);
  }
  if (threadShouldExit())
    return;
  auto done = done_;
  MessageManager::callAsync([done] { if (done) done(); });
}

} // namespace ghosty
