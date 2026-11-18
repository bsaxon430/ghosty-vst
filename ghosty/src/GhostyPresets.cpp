#include "GhostyPresets.h"
#include "BinaryData.h"
#include "load_save.h"

namespace ghosty {

const std::vector<FactoryPreset>& factoryPresets() {
  static const std::vector<FactoryPreset> list = [] {
    // file names are "<order> <name>.vital", e.g. "012 Rubber Sub.vital": the number only sorts the list
    std::vector<std::pair<String, FactoryPreset>> found;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
      String file = BinaryData::originalFilenames[i];
      if (!file.endsWithIgnoreCase(".vital"))
        continue;
      int size = 0;
      const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
      if (data == nullptr || size <= 0)
        continue;
      FactoryPreset p;
      p.text = String::fromUTF8(data, size);
      p.name = file.upToLastOccurrenceOf(".", false, false);
      if (p.name.length() > 4 && p.name.substring(0, 3).containsOnly("0123456789") && p.name[3] == ' ')
        p.name = p.name.substring(4);
      try {
        json j = json::parse(p.text.toStdString());
        if (j.count("preset_style") && j["preset_style"].is_string())
          p.category = String(j["preset_style"].get<std::string>());
      }
      catch (const std::exception&) { continue; }
      if (p.category.isEmpty())
        p.category = "Other";
      found.emplace_back(file, std::move(p));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first.compareNatural(b.first) < 0; });
    std::vector<FactoryPreset> out;
    for (auto& f : found)
      out.push_back(std::move(f.second));
    return out;
  }();
  return list;
}

StringArray factoryCategories() {
  StringArray cats;
  for (auto& p : factoryPresets())
    cats.addIfNotAlreadyThere(p.category);
  return cats;
}

PropertiesFile& settings() {
  static std::unique_ptr<PropertiesFile> file = [] {
    PropertiesFile::Options o;
    o.applicationName = "Ghosty Pro 2";   // never the old Ghosty Pro's settings file
    o.filenameSuffix = "settings";
    o.folderName = "GhostBlock";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = PropertiesFile::storeAsXML;
    return std::make_unique<PropertiesFile>(o);
  }();
  return *file;
}

} // namespace ghosty
