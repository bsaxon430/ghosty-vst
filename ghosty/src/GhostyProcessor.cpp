#include "GhostyProcessor.h"
#include "GhostyEditor.h"
#include "GhostyPresets.h"
#include "load_save.h"
#include "VitalPresetFinder.h"
#include "sound_engine.h"

namespace {
  // the host's transport, moved on to a chunk inside the block (so Vital's tempo sync sees the true position of each chunk)
  // (TEMPO 1/2x or 2x: the tempo and the beat position both times the multiple, so the engine's own tempo sync runs at
  // that tempo through its normal host-tempo input; the time in seconds stays the host's)
  struct ChunkPlayHead : public AudioPlayHead {
    AudioPlayHead* host = nullptr;
    double offset_seconds = 0.0;
    int64 offset_samples = 0;
    double mult = 1.0;
    bool getCurrentPosition(CurrentPositionInfo& info) override {
      if (host == nullptr || !host->getCurrentPosition(info))
        return false;
      info.timeInSamples += offset_samples;
      info.timeInSeconds += offset_seconds;
      if (mult != 1.0) {
        info.bpm *= mult;
        info.ppqPosition *= mult;
        info.ppqPositionOfLastBarStart *= mult;
        info.ppqLoopStart *= mult;
        info.ppqLoopEnd *= mult;
      }
      if (info.bpm > 0.0)
        info.ppqPosition += offset_seconds * info.bpm / 60.0;
      return true;
    }
  };
}

GhostyProcessor::GhostyProcessor() {
  if (auto* out = getBus(false, 0)) {
    out->setCurrentLayout(AudioChannelSet::stereo());
    out->enable(true);
  }
  if (auto* in = getBus(true, 0))
    in->enable(false);
  ghosty::VitalPresetFinder::ensureDataFolder();
  morph_.init(controls_);
  morph_.publish();
}

GhostyProcessor::~GhostyProcessor() = default;

bool GhostyProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto& out = layouts.getMainOutputChannelSet();
  return out == AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void GhostyProcessor::prepareToPlay(double sample_rate, int buffer_size) {
  SynthPlugin::prepareToPlay(sample_rate, buffer_size);
  extras_.prepare(sample_rate, buffer_size);
  constexpr int kKeysMidiBytes = 16384;   // pre-allocated: KEYS never grows a buffer on the audio thread
  keys_out_.ensureSize(kKeysMidiBytes);
  keys_.prepare(sample_rate, kKeysMidiBytes);
}

void GhostyProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midi) {
  static const bool force_chunks = SystemStats::getEnvironmentVariable("GHOSTY_FORCE_CHUNKS", {}).isNotEmpty();   // (diagnostics)
  const bool morphing = morph_.enabled.load(std::memory_order_relaxed) || force_chunks;
  const bool extras = extras_.anyOn();
  const bool keys = keys_.anyOn() || keys_.busy();
  const double tempo = tempo_mult_.load(std::memory_order_relaxed);
  const bool scaled = tempo != 1.0;
  double ppq = 0.0, bpm = 120.0;
  bool playing = false;
  if (AudioPlayHead* head = getPlayHead()) {   // (reading the transport changes nothing for the engine)
    AudioPlayHead::CurrentPositionInfo info;
    if (head->getCurrentPosition(info)) {
      ppq = info.ppqPosition;
      bpm = info.bpm > 0.0 ? info.bpm : 120.0;
      playing = info.isPlaying;
      if (info.bpm > 0.0) host_bpm_.store(info.bpm, std::memory_order_relaxed);
    }
  }
  if (!morphing && !extras && !keys && !scaled) {
    SynthPlugin::processBlock(buffer, midi);   // exactly Vital
    return;
  }
  // TEMPO: our tempo-synced parts and the engine see the host tempo times the multiple
  ppq *= tempo;
  bpm *= tempo;

  if (keys) {
    // KEYS: the notes (host MIDI and the on-screen keyboard) go through ARP / CHORDS / SCALE LOCK / HOLD, then to Vital.
    // Vital's own keyboard merge runs first here; when SynthPlugin::processBlock runs it again it finds nothing new.
    if (buffer.getNumSamples() > 0)
      processKeyboardEvents(midi, buffer.getNumSamples());
    keys_.process(midi, keys_out_, buffer.getNumSamples(), bpm, playing, ppq);
    midi.swapWith(keys_out_);
  }
  // this instance's own free-running clock (never shared: a second instance starts its own from 0)
  free_seconds_ += buffer.getNumSamples() / juce::jmax(1.0, AudioProcessor::getSampleRate());
  const double free_seconds = free_seconds_;

  if (morphing) {
    // MORPH moves Vital's knobs every 64 samples (a smooth glide, no zipper); MIDI stays sample-exact
    constexpr int kChunk = 64;
    const double rate = juce::jmax(1.0, AudioProcessor::getSampleRate());
    const int total = buffer.getNumSamples();
    double t = free_seconds - total / rate;
    AudioPlayHead* host = getPlayHead();
    ChunkPlayHead chunk_head;
    chunk_head.host = host;
    chunk_head.mult = tempo;
    for (int start = 0; start < total; start += kChunk) {
      const int n = juce::jmin(kChunk, total - start);
      morph_.process(n, rate, ppq + start / rate * bpm / 60.0, bpm, playing, t + start / rate);
      AudioSampleBuffer part(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
      MidiBuffer slice;
      for (const auto meta : midi)
        if (meta.samplePosition >= start && meta.samplePosition < start + n)
          slice.addEvent(meta.getMessage(), meta.samplePosition - start);
      if (host != nullptr) {
        chunk_head.offset_samples = start;
        chunk_head.offset_seconds = start / rate;
        setPlayHead(&chunk_head);
      }
      SynthPlugin::processBlock(part, slice);
    }
    if (host != nullptr)
      setPlayHead(host);
  }
  else if (scaled && getPlayHead() != nullptr) {
    AudioPlayHead* host = getPlayHead();
    ChunkPlayHead head;
    head.host = host;
    head.mult = tempo;
    setPlayHead(&head);
    SynthPlugin::processBlock(buffer, midi);
    setPlayHead(host);
  }
  else
    SynthPlugin::processBlock(buffer, midi);

  if (extras)
    extras_.process(buffer, ppq, bpm, playing);
}

AudioProcessorEditor* GhostyProcessor::createEditor() {
  return new GhostyEditor(*this);
}

// ------------------------------------------------------------------ state
// Vital's own state json (LoadSave::stateToJson + tuning, as SynthPlugin saves it) with one more key, "ghosty", for
// our board and extras. Vital's loader ignores keys it does not know.
void GhostyProcessor::getStateInformation(MemoryBlock& dest) {
  json data = LoadSave::stateToJson(this, getCallbackLock());
  data["tuning"] = getTuning()->stateToJson();

  ValueTree ours("GHOSTY_PRO");
  ours.setProperty("factory", factory_index_, nullptr);
  ours.setProperty("tempo", (double) tempo_mult_.load(), nullptr);
  ours.appendChild(morph_.toTree(), nullptr);
  ours.appendChild(extras_.toTree(), nullptr);
  ours.appendChild(keys_.toTree(), nullptr);
  data["ghosty"] = ours.toXmlString().toStdString();

  String text = data.dump();
  MemoryOutputStream stream;
  stream.writeString(text);
  dest.append(stream.getData(), stream.getDataSize());
}

void GhostyProcessor::setStateInformation(const void* data, int size) {
  MemoryInputStream stream(data, (size_t) size, false);
  String text = stream.readEntireStreamAsString();
  json parsed;
  try {
    parsed = json::parse(text.toStdString());
  }
  catch (const std::exception&) {
    // not Vital json: a project saved with the old Ghosty Pro engine. Start from our first sound, no dialog.
    String error;
    if (!loadFactoryPreset(0, error))
      loadInit();
    return;
  }

  SynthPlugin::setStateInformation(data, size);   // Vital's own restore
  setTempoMultiple(1.0f);   // (a project without it: 1x)

  if (parsed.count("ghosty") && parsed["ghosty"].is_string()) {
    if (auto xml = parseXML(String(parsed["ghosty"].get<std::string>()))) {
      ValueTree ours = ValueTree::fromXml(*xml);
      factory_index_ = (int) ours.getProperty("factory", -1);
      setTempoMultiple((float) (double) ours.getProperty("tempo", 1.0));
      morph_.fromTree(ours.getChildWithName("GHOSTY_MORPH"));
      extras_.fromTree(ours.getChildWithName("GHOSTY_EXTRAS"));
      keys_.fromTree(ours.getChildWithName("GHOSTY_KEYS"));
    }
  }
  soundChanged();
}

// ------------------------------------------------------------------ presets
void GhostyProcessor::soundChanged() {
  ++sound_epoch;
  if (SynthGuiInterface* gui = getGuiInterface())
    gui->updateFullGui();
}

bool GhostyProcessor::loadPresetFile(const File& file, String& error) {
  std::string err;
  if (!loadFromFile(file, err)) {   // Vital's own loader (also sets the name and updates Vital's editor)
    error = err.empty() ? String("Couldn't open that preset.") : String(err);
    return false;
  }
  factory_index_ = -1;
  soundChanged();
  return true;
}

bool GhostyProcessor::loadPresetText(const String& text, const String& name, String& error) {
  try {
    json parsed = json::parse(text.toStdString());
    if (!loadFromJson(parsed)) {   // Vital's own
      error = "This preset was made with a newer version of the engine.";
      return false;
    }
  }
  catch (const std::exception&) {
    error = "That preset file is damaged.";
    return false;
  }
  clearActiveFile();
  setPresetName(name);
  soundChanged();
  return true;
}

bool GhostyProcessor::loadFactoryPreset(int index, String& error) {
  const auto& list = ghosty::factoryPresets();
  if (!isPositiveAndBelow(index, (int) list.size())) {
    error = "No such preset.";
    return false;
  }
  if (!loadPresetText(list[(size_t) index].text, list[(size_t) index].name, error))
    return false;
  factory_index_ = index;
  return true;
}

void GhostyProcessor::loadInit() {
  loadInitPreset();   // Vital's own
  factory_index_ = -1;
  clearActiveFile();
  setPresetName("Init");
  soundChanged();
}

bool GhostyProcessor::savePresetFile(const File& file) {
  return saveToFile(file);   // Vital's own .vital writer
}

String GhostyProcessor::presetText() {
  return String(saveToJson().dump());
}

void GhostyProcessor::setControl(const std::string& name, float value) {
  if (controls_.count(name) == 0)
    return;
  const vital::ValueDetails& d = vital::Parameters::getDetails(name);
  value = juce::jlimit(d.min, d.max, value);
  valueChangedInternal(name, value);
  if (SynthGuiInterface* gui = getGuiInterface())
    gui->updateGuiControl(name, value);
}

float GhostyProcessor::getControl(const std::string& name) {
  auto it = controls_.find(name);
  return it == controls_.end() ? 0.0f : it->second->value();
}

#if !GHOSTY_TOOL
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new GhostyProcessor();
}
#endif
