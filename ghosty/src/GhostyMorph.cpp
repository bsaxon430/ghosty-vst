#include "GhostyMorph.h"
#include "synth_parameters.h"
#include "value.h"

namespace ghosty {

namespace {
  // controls the board never moves: the master switch, the host-facing wheels and the oversampling choice
  bool skipped(const std::string& name) {
    return name == "bypass" || name == "pitch_wheel" || name == "mod_wheel" || name == "oversampling" ||
           name == "polyphony" || name == "voice_tune" || name == "view_spectrogram";
  }
}

void Morph::init(vital::control_map& controls) {
  names_.clear(); values_.clear(); indexed_.clear(); lo_.clear(); hi_.clear(); def_.clear();
  int num = vital::Parameters::getNumParameters();
  for (int i = 0; i < num; ++i) {
    const vital::ValueDetails* d = vital::Parameters::getDetails(i);
    if (d == nullptr || controls.count(d->name) == 0 || skipped(d->name))
      continue;
    names_.push_back(d->name);
    values_.push_back(controls[d->name]);
    indexed_.push_back(d->value_scale == vital::ValueDetails::kIndexed ? 1 : 0);
    lo_.push_back(d->min); hi_.push_back(d->max); def_.push_back(d->default_value);
  }
  current_.assign(names_.size(), 0.0f);
  pair_of_.assign(names_.size(), -1);
  auto slotOf = [this](const std::string& n) { for (size_t s = 0; s < names_.size(); ++s) if (names_[s] == n) return (int) s; return -1; };
  const std::pair<const char*, const char*> pairs[] {
    { "chorus_on", "chorus_dry_wet" }, { "delay_on", "delay_dry_wet" }, { "distortion_on", "distortion_mix" },
    { "flanger_on", "flanger_dry_wet" }, { "phaser_on", "phaser_dry_wet" }, { "reverb_on", "reverb_dry_wet" },
    { "compressor_on", "compressor_mix" }, { "filter_fx_on", "filter_fx_mix" }, { "osc_1_on", "osc_1_level" },
    { "osc_2_on", "osc_2_level" }, { "osc_3_on", "osc_3_level" }, { "filter_1_on", "filter_1_mix" },
    { "filter_2_on", "filter_2_mix" }, { "sample_on", "sample_level" }, { "sub_on", "sub_level" } };
  for (auto& p : pairs) {
    const int sw = slotOf(p.first), lv = slotOf(p.second);
    if (sw >= 0 && lv >= 0) pair_of_[(size_t) sw] = lv;
  }
}

std::vector<float> Morph::captureCurrent() const {
  std::vector<float> v(names_.size());
  for (size_t s = 0; s < names_.size(); ++s)
    v[s] = values_[s]->value();
  return v;
}

std::vector<float> Morph::captureFromPresetJson(const String& preset_text) const {
  std::vector<float> v(def_);
  try {
    json data = json::parse(preset_text.toStdString());
    if (data.count("settings")) {
      const json& settings = data["settings"];
      for (size_t s = 0; s < names_.size(); ++s)
        if (settings.count(names_[s]) && settings[names_[s]].is_number())
          v[s] = settings[names_[s]].get<float>();
    }
  }
  catch (const std::exception&) { }
  return v;
}

void Morph::publish() {
  collect();
  auto set = std::make_unique<MorphSet>();
  set->ghosts = juce::jmin((int) board.ghosts.size(), ghosty::spirit::kMaxGhosts);
  set->slots = (int) names_.size();
  set->values.resize((size_t) (set->ghosts * set->slots));
  for (int k = 0; k < set->ghosts; ++k) {
    auto it = ghost_values.find(board.ghosts[(size_t) k].id);
    for (int s = 0; s < set->slots; ++s) {
      float v = def_[(size_t) s];
      if (it != ghost_values.end() && (int) it->second.size() == set->slots)
        v = it->second[(size_t) s];
      set->values[(size_t) (k * set->slots + s)] = v;
    }
  }
  board.orbit.mode = ghosty::spirit::Orbit::Off;   // (the board's own orbit is unused: MOTION above moves the puck)
  // a ghost with a switch off has that switch's level at 0 (so the blend fades it in and out)
  for (int k = 0; k < set->ghosts; ++k)
    for (int s = 0; s < set->slots; ++s)
      if (pair_of_[(size_t) s] >= 0 && set->values[(size_t) (k * set->slots + s)] < 0.5f)
        set->values[(size_t) (k * set->slots + pair_of_[(size_t) s])] = 0.0f;
  shared.publish(board);
  if (hasDrawnPath()) shared.publishPath(&board.paths[0]);
  delete pending_.exchange(set.release(), std::memory_order_acq_rel);
  collect();
}

const char* Morph::motionName(int m) {
  static const char* n[] { "STILL", "CIRCLE", "EIGHT", "WANDER", "GLIDE", "STEP", "DRAWN" };
  return n[juce::jlimit(0, kNumMotions - 1, m)];
}

const std::vector<Morph::Division>& Morph::divisions() {
  static const std::vector<Division> d {
    { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 }, { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 },
    { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2T", 4.0 / 3.0 }, { "1/2", 2.0 }, { "1/2D", 3.0 },
    { "1 BAR", 4.0 }, { "2 BARS", 8.0 }, { "4 BARS", 16.0 }, { "8 BARS", 32.0 } };
  return d;
}

void Morph::setDrawnPath(const ghosty::spirit::OrbitPath& path) {
  board.paths[0] = path;
  drawn_beats = juce::jmax(0.25, path.loopBeats());
  shared.publishPath(&board.paths[0]);
}

juce::Point<float> Morph::puckAt(double periods, const float* xs, const float* ys, int n) const noexcept {
  namespace sp = ghosty::spirit;
  const juce::Point<float> base(puck_x.load(std::memory_order_relaxed), puck_y.load(std::memory_order_relaxed));
  if (!std::isfinite(periods)) periods = 0.0;
  switch (motion.load(std::memory_order_relaxed)) {
    case kCircle: case kEight: case kWander: {
      sp::OrbitParams op;
      const int m = motion.load(std::memory_order_relaxed);
      op.mode = m == kCircle ? sp::Orbit::Circle : m == kEight ? sp::Orbit::Figure8 : sp::Orbit::RandomWalk;
      op.rate = 1.0f;   // one cycle (WANDER: one new point) per period
      op.depth = depth.load(std::memory_order_relaxed);
      return sp::clampToBoard(base + sp::orbitOffset(op, periods));
    }
    case kGlide: case kStep: {
      if (n <= 0) return base;
      const double whole = std::floor(periods);
      const int k = (int) (((int64_t) whole % n + n) % n);
      if (motion.load(std::memory_order_relaxed) == kStep || n == 1)
        return { xs[k], ys[k] };
      const int j = (k + 1) % n;
      const float f = (float) (periods - whole);
      const float t = f * f * (3.0f - 2.0f * f);   // ease in and out of each ghost
      return { xs[k] + t * (xs[j] - xs[k]), ys[k] + t * (ys[j] - ys[k]) };
    }
    case kDrawn: {
      juce::Point<float> p;
      if (shared.pathPuck(periods * drawn_beats.load(std::memory_order_relaxed), p)) return p;
      return base;
    }
    default: return base;
  }
}

juce::Point<float> Morph::idlePuck() const {
  float xs[ghosty::spirit::kMaxGhosts], ys[ghosty::spirit::kMaxGhosts];
  const int n = juce::jmin((int) board.ghosts.size(), ghosty::spirit::kMaxGhosts);
  for (int k = 0; k < n; ++k) { xs[k] = board.ghosts[(size_t) k].x; ys[k] = board.ghosts[(size_t) k].y; }
  const double secs = juce::Time::getMillisecondCounterHiRes() * 0.001;
  double periods;
  if (sync.load(std::memory_order_relaxed)) {
    const auto& d = divisions();
    const double bpm = juce::jmax(1.0, shared.liveBpm.load(std::memory_order_relaxed));
    periods = secs * bpm / 60.0 / d[(size_t) juce::jlimit(0, (int) d.size() - 1, division.load(std::memory_order_relaxed))].beats;
  }
  else
    periods = secs * (double) juce::jmax(0.001f, hz.load(std::memory_order_relaxed));
  return ghosty::spirit::clampToBoard(puckAt(periods, xs, ys, n));
}

bool Morph::process(int num_samples, double sample_rate, double ppq, double bpm, bool playing, double free_seconds) noexcept {
  namespace sp = ghosty::spirit;
  if (pending_.load(std::memory_order_acquire) != nullptr && trash_.load(std::memory_order_acquire) == nullptr) {
    if (MorphSet* fresh = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
      trash_.store(live_, std::memory_order_release);
      live_ = fresh;
      snap_ = true;
    }
  }
  float xs[sp::kMaxGhosts], ys[sp::kMaxGhosts];
  sp::WeightParams wp; sp::OrbitParams op;
  const int n = shared.read(xs, ys, wp, op);
  if (!enabled.load(std::memory_order_relaxed) || live_ == nullptr || n <= 0 || live_->ghosts != n ||
      live_->slots != (int) names_.size()) {
    was_on_ = false;
    return false;
  }

  // the period count: tempo-locked to the host's beat position while it plays (free-running at its tempo when stopped)
  double periods;
  if (sync.load(std::memory_order_relaxed)) {
    const auto& d = divisions();
    const double beats = playing ? ppq : free_seconds * juce::jmax(1.0, bpm) / 60.0;
    periods = beats / d[(size_t) juce::jlimit(0, (int) d.size() - 1, division.load(std::memory_order_relaxed))].beats;
  }
  else
    periods = free_seconds * (double) juce::jmax(0.001f, hz.load(std::memory_order_relaxed));
  shared.postTime(playing ? ppq : free_seconds * juce::jmax(1.0, bpm) / 60.0, bpm, playing);
  const auto pk = sp::clampToBoard(puckAt(periods, xs, ys, n));
  float w[sp::kMaxGhosts] {};
  sp::computeWeights(xs, ys, n, pk.x, pk.y, wp, w);
  shared.postLive(pk.x, pk.y, w, n);

  int exact = -1, leader = 0;
  for (int k = 0; k < n; ++k) {
    if (w[k] >= 1.0f) exact = k;
    if (w[k] > w[leader]) leader = k;
  }
  const int S = live_->slots;
  // knobs glide to where the board says: an exponential glide (25 ms), run every 64 samples by the processor
  const float coef = 1.0f - std::exp(-(float) num_samples / (float) (0.025 * sample_rate));
  for (int s = 0; s < S; ++s) {
    float target;
    if (pair_of_[(size_t) s] >= 0) {
      // on while any ghost using it has weight, or while its level is still gliding out
      bool on = false;
      for (int k = 0; k < n && !on; ++k) on = w[k] > 1.0e-4f && live_->values[(size_t) (k * S + s)] >= 0.5f;
      const int lv = pair_of_[(size_t) s];
      if (!on && !(snap_ || !was_on_) && current_[(size_t) lv] > (hi_[(size_t) lv] - lo_[(size_t) lv]) * 1.0e-3f) on = true;
      target = on ? 1.0f : 0.0f;
    }
    else if (indexed_[(size_t) s])
      target = live_->values[(size_t) (leader * S + s)];
    else if (exact >= 0)
      target = live_->values[(size_t) (exact * S + s)];
    else {
      double acc = 0.0;
      for (int k = 0; k < n; ++k)
        acc += (double) w[k] * live_->values[(size_t) (k * S + s)];
      target = (float) acc;
    }
    float& cur = current_[(size_t) s];
    if (snap_ || !was_on_ || indexed_[(size_t) s])
      cur = target;
    else {
      cur += (target - cur) * coef;
      if (std::abs(target - cur) <= (hi_[(size_t) s] - lo_[(size_t) s]) * 1.0e-5f)
        cur = target;   // arrived: exactly the ghost's value
    }
    if (values_[(size_t) s]->value() != cur)
      values_[(size_t) s]->set(cur);
  }
  snap_ = false;
  was_on_ = true;
  return true;
}

ValueTree Morph::toTree() const {
  ValueTree t("GHOSTY_MORPH");
  t.setProperty("on", enabled.load(), nullptr);
  t.setProperty("px", puck_x.load(), nullptr);
  t.setProperty("py", puck_y.load(), nullptr);
  t.setProperty("motion", motion.load(), nullptr);
  t.setProperty("division", division.load(), nullptr);
  t.setProperty("sync", sync.load(), nullptr);
  t.setProperty("hz", hz.load(), nullptr);
  t.setProperty("depth", depth.load(), nullptr);
  t.appendChild(board.toTree(), nullptr);
  for (auto& g : board.ghosts) {
    auto it = ghost_values.find(g.id);
    if (it == ghost_values.end())
      continue;
    ValueTree v("GHOST_SOUND");
    v.setProperty("id", g.id, nullptr);
    auto nm = ghost_names.find(g.id);
    v.setProperty("from", nm != ghost_names.end() ? nm->second : String(), nullptr);
    // stored by control name, so a newer engine with more controls still reads it
    for (size_t s = 0; s < names_.size() && s < it->second.size(); ++s)
      v.setProperty(Identifier(names_[s]), it->second[s], nullptr);
    t.appendChild(v, nullptr);
  }
  return t;
}

void Morph::fromTree(const ValueTree& t) {
  enabled = (bool) t.getProperty("on", false);
  puck_x = juce::jlimit(0.0f, 1.0f, (float) t.getProperty("px", 0.5f));
  puck_y = juce::jlimit(0.0f, 1.0f, (float) t.getProperty("py", 0.5f));
  board = ghosty::spirit::Board::fromTree(t.getChildWithName("SPIRITBOARD"));
  motion = juce::jlimit(0, kNumMotions - 1, (int) t.getProperty("motion", 0));
  division = juce::jlimit(0, (int) divisions().size() - 1, (int) t.getProperty("division", 12));
  sync = (bool) t.getProperty("sync", true);
  hz = juce::jlimit(0.01f, 20.0f, (float) t.getProperty("hz", 0.25f));
  depth = juce::jlimit(0.0f, 0.5f, (float) t.getProperty("depth", 0.25f));
  ghost_values.clear(); ghost_names.clear();
  for (auto c : t) {
    if (!c.hasType("GHOST_SOUND"))
      continue;
    String id = c.getProperty("id").toString();
    std::vector<float> v(def_);
    for (size_t s = 0; s < names_.size(); ++s) {
      Identifier key(names_[s]);
      if (c.hasProperty(key))
        v[s] = (float) c.getProperty(key);
    }
    ghost_values[id] = v;
    ghost_names[id] = c.getProperty("from").toString();
  }
  publish();
  if (hasDrawnPath()) setDrawnPath(board.paths[0]);
}

} // namespace ghosty
