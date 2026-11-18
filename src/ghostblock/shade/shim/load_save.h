// SEANCE: stands in for Vital's load_save.h, of which the wavetable code uses three helpers.
// convertBufferToPcm and convertPcmToFloatBuffer are copied from Vital's src/common/load_save.cpp (GPL-3, Matt Tytel);
// compareVersionStrings does the same comparison as Vital's, written with std::string.
#pragma once
#include "JuceHeader.h"
#include "json/json.h"
#include "utils.h"
using json = nlohmann::json;

class LoadSave
{
  public:
    static void convertBufferToPcm(json &data, const std::string &field)
    {
        if (data.count(field) == 0)
            return;

        MemoryOutputStream decoded;
        std::string wave_data = data[field];
        Base64::convertFromBase64(decoded, wave_data);
        int size = static_cast<int>(decoded.getDataSize()) / sizeof(float);
        std::unique_ptr<float[]> float_data = std::make_unique<float[]>(size);
        memcpy(float_data.get(), decoded.getData(), size * sizeof(float));
        std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(size);
        vital::utils::floatToPcmData(pcm_data.get(), float_data.get(), size);

        String encoded = Base64::toBase64(pcm_data.get(), sizeof(int16_t) * size);
        data[field] = encoded.toStdString();
    }
    static void convertPcmToFloatBuffer(json &data, const std::string &field)
    {
        if (data.count(field) == 0)
            return;

        MemoryOutputStream decoded;
        std::string wave_data = data[field];
        Base64::convertFromBase64(decoded, wave_data);
        int size = static_cast<int>(decoded.getDataSize()) / sizeof(int16_t);
        std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(size);
        memcpy(pcm_data.get(), decoded.getData(), size * sizeof(int16_t));
        std::unique_ptr<float[]> float_data = std::make_unique<float[]>(size);
        vital::utils::pcmToFloatData(float_data.get(), pcm_data.get(), size);

        String encoded = Base64::toBase64(float_data.get(), sizeof(float) * size);
        data[field] = encoded.toStdString();
    }
    // 1 if a is the newer version, -1 if b is, 0 if equal; "1.0.4" style, a missing or non-numeric part counts as 0
    static int compareVersionStrings(std::string a, std::string b)
    {
        auto trim = [](std::string &s) {
            while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        };
        trim(a);
        trim(b);
        if (a.empty() && b.empty())
            return 0;
        auto head = [](const std::string &s) { return s.substr(0, s.find('.')); };
        auto tail = [](const std::string &s) { auto p = s.find('.'); return p == std::string::npos ? std::string() : s.substr(p + 1); };
        auto value = [](const std::string &s) {
            if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) return 0;
            return atoi(s.c_str());
        };
        const int va = value(head(a)), vb = value(head(b));
        if (va > vb) return 1;
        if (va < vb) return -1;
        return compareVersionStrings(tail(a), tail(b));
    }
};
