// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "UnderTheHood.h"
#include "Look.h"
#include "LiquidToggle.h"

namespace gv
{
namespace hd = gv::hood;

// what the command / job boxes SHOW: the exact text, with the engine's install folder and its model file names in plain words
// (Brandon 2026-11-06: no open-source project names in the window; ABOUT / CREDITS names them). commandText() stays exact.
static juce::String shown (juce::String t)
{
    const auto eng = engineDir().getChildFile ("seed-vc").getFullPathName();
    t = t.replace (eng, "ENGINE");
    static const char* const files[][2] {
        { "DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth", "singing-model.pth" }, { "config_dit_mel_seed_uvit_whisper_base_f0_44k.yml", "singing-model.yml" },
        { "DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth", "speech-model.pth" }, { "config_dit_mel_seed_uvit_whisper_small_wavenet.yml", "speech-model.yml" } };
    for (auto& f : files) t = t.replace (f[0], f[1]);
    return uiWords (t);
}

// ------------------------------------------------------------------------------------------------ one option
struct HoodPage::Row : public juce::Component, public juce::SettableTooltipClient
{
    Row (HoodPage& p, const hd::Option& o) : page (p), opt (o)
    {
        auto done = [this] { page.ed.refresh(); };
        switch (opt.kind)
        {
            case hd::Kind::number:
                slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
                slider.setRange (opt.lo, opt.hi, opt.step);
                slider.onValueChange = [this, done]
                {
                    auto& st = page.ed.proc.settings; const double v = slider.getValue();
                    switch (opt.id)
                    {
                        case hd::Id::steps: st.steps = (int) std::lround (v); break;
                        case hd::Id::length: st.lengthAdjust = (float) (std::round (v * 100.0) / 100.0); break;
                        case hd::Id::cfg: st.follow = (float) v; break;
                        case hd::Id::semis: st.semitones = (int) std::lround (v); break;
                        default: break;
                    }
                    done();
                };
                addAndMakeVisible (slider);
                break;
            case hd::Kind::toggle:
                toggle.onChange = [this, done] (bool on)
                {
                    auto& st = page.ed.proc.settings;
                    if (opt.id == hd::Id::f0) st.f0Condition = on; else st.autoPitch = on;
                    done();
                };
                addAndMakeVisible (toggle);
                break;
            case hd::Kind::threeWay:
                combo.addItemList ({ "Automatic (Apple GPU on, CPU off)", "True", "False" }, 1);
                combo.onChange = [this, done] { page.ed.proc.settings.fp16 = combo.getSelectedId() == 1 ? -1 : combo.getSelectedId() == 2 ? 1 : 0; done(); };
                addAndMakeVisible (combo);
                break;
            case hd::Kind::model:
                combo.onChange = [this, done]
                {
                    auto& st = page.ed.proc.settings; const int id = combo.getSelectedId();
                    const auto ms = hd::installedModels();
                    if (id == 1) { st.checkpoint = {}; st.config = {}; }
                    else if (id - 2 < (int) ms.size()) { st.checkpoint = ms[(size_t) (id - 2)].checkpoint; st.config = ms[(size_t) (id - 2)].config; }
                    done();
                };
                addAndMakeVisible (combo);
                break;
            default: break;
        }
        setTooltip (uiWords (juce::String (opt.flag) + ": " + opt.help));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = page.ed.pal();
        const auto& s = page.ed.proc.settings;
        auto r = getLocalBounds().toFloat().reduced (0, 3);
        panel (g, P, r, 10);
        const float a = on ? 1.0f : 0.45f;
        auto left = r.reduced (14, 8).withTrimmedRight (isControlled() ? 340.0f : 0.0f);
        g.setColour (P.ink.withMultipliedAlpha (a)); g.setFont (gfont (15.0f));
        const auto tw = ghosty::textWidth (gfont (15.0f), juce::String (opt.title));
        g.drawText (uiWords (opt.title), left.removeFromTop (20), juce::Justification::centredLeft, true);
        // the engine's own option name, small, under ours
        g.setColour (P.violet.withMultipliedAlpha (a)); g.setFont (gfont (12.0f));
        g.drawText (juce::String (opt.flag) + "     " + opt.where + "  -  " + opt.type, left.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (P.dim.withMultipliedAlpha (a)); g.setFont (gfont (11.5f, false));
        g.drawFittedText (uiWords (opt.help), left.toNearestInt().withHeight (30), juce::Justification::topLeft, 2, 1.0f);
        (void) tw;
        auto right = r.reduced (14, 8).removeFromRight (326.0f);
        if (isControlled())
        {
            auto line = right.removeFromTop (22);
            g.setColour (P.ink.withMultipliedAlpha (a)); g.setFont (gfont (13.0f));
            g.drawText (shown (valueLine (s)), line, juce::Justification::centredRight, true);
            right.removeFromTop (28);                                                   // the control sits here
        }
        else { g.setColour (P.ink.withMultipliedAlpha (a)); g.setFont (gfont (12.5f, false)); g.drawFittedText (shown (givenLine()), right.removeFromTop (44).toNearestInt(), juce::Justification::topRight, 3, 1.0f); }
        g.setColour (P.dim); g.setFont (gfont (10.5f, false));
        juce::String facts = juce::String ("original default: ") + uiWords (opt.upstreamDefault);
        if (juce::String (opt.range).isNotEmpty()) facts << "   range " << opt.range;
        if (juce::String (opt.ours) != "") facts << "   ours: " << opt.ours;
        g.drawFittedText (facts, right.toNearestInt().withHeight (28).withY (getHeight() - 33), juce::Justification::bottomRight, 2, 1.0f);
        if (const auto w = warning(); w.isNotEmpty())
        { g.setColour (P.warn); g.setFont (gfont (11.5f, false)); g.drawFittedText (uiWords (w), left.toNearestInt().withY (getHeight() - 28).withHeight (22), juce::Justification::centredLeft, 1, 1.0f); }
    }
    void resized() override
    {
        auto c = getLocalBounds().reduced (14, 11).removeFromRight (326).withTrimmedTop (22).withHeight (28);
        slider.setBounds (c); toggle.setBounds (c.withWidth (60).withX (c.getRight() - 60)); combo.setBounds (c.reduced (0, 1));
    }
    bool isControlled() const { return opt.kind == hd::Kind::number || opt.kind == hd::Kind::toggle || opt.kind == hd::Kind::threeWay || opt.kind == hd::Kind::model; }
    juce::String valueLine (const EngineSettings& s) const
    {
        switch (opt.id)
        {
            case hd::Id::steps: return juce::String (s.steps) + " steps";
            case hd::Id::length: return hd::valueText (opt.id, s);
            case hd::Id::cfg: return hd::valueText (opt.id, s);
            case hd::Id::semis: return (s.semitones > 0 ? "+" : "") + juce::String (s.semitones) + " semitones";
            case hd::Id::f0: return s.f0Condition ? "True  (singing model)" : "False  (speech model)";
            case hd::Id::autoF0: return s.autoPitch ? "True" : "False";
            case hd::Id::fp16: return s.fp16 < 0 ? juce::String ("automatic: ") + (s.cpuOnly ? "False (CPU)" : "True (Apple GPU)") : hd::valueText (opt.id, s);
            case hd::Id::checkpoint: return s.checkpoint.isEmpty() ? "None  (Seed-VC picks " + hd::ownPick (s.f0Condition) + ")" : juce::File (s.checkpoint).getFileName();
            default: return {};
        }
    }
    juce::String givenLine() const
    {
        const auto& p = page.ed.proc; const auto& s = p.settings;
        switch (opt.id)
        {
            case hd::Id::source: { const auto& t = p.takes[slotPerf]; return t.isEmpty() ? (p.takes[slotA].isEmpty() ? "set by the performance card" : "voice A's first take (no performance given)") : juce::File (t[0]).getFileName(); }
            case hd::Id::target: return "voice A's first take below 50 % B, voice B's first take from 50 % B";
            case hd::Id::output: return "a fresh folder for every blend";
            case hd::Id::config: return s.config.isEmpty() ? "None  (Seed-VC picks its own: follows the checkpoint)" : juce::File (s.config).getFileName() + "  (follows the checkpoint)";
            case hd::Id::share: return "not used: no web page is started";
            case hd::Id::gpu: return "not used: Macs have no CUDA";
            default: return {};
        }
    }
    juce::String warning() const
    {
        const auto& s = page.ed.proc.settings;
        if (opt.id == hd::Id::checkpoint && s.checkpoint.isNotEmpty())
        {
            for (auto& m : hd::installedModels()) if (m.checkpoint == s.checkpoint && m.needsF0 != s.f0Condition) return m.needsF0 ? "This model needs F0 condition True." : "This model needs F0 condition False.";
            if (! juce::File (s.checkpoint).existsAsFile()) return "This file is missing: Seed-VC will fail on it.";
        }
        if (opt.id == hd::Id::f0 && ! s.f0Condition && s.checkpoint.isEmpty())
            return "The speech model is fetched by Seed-VC itself the first time (needs internet).";
        if (! on) return "Only works with F0 condition True.";
        return {};
    }
    void update()
    {
        const auto& s = page.ed.proc.settings;
        on = ! ((opt.id == hd::Id::autoF0 || opt.id == hd::Id::semis) && ! s.f0Condition);
        slider.setEnabled (on); toggle.setEnabled (on); combo.setEnabled (on);
        switch (opt.id)
        {
            case hd::Id::steps: slider.setValue (s.steps, juce::dontSendNotification); break;
            case hd::Id::length: slider.setValue (s.lengthAdjust, juce::dontSendNotification); break;
            case hd::Id::cfg: slider.setValue (s.follow, juce::dontSendNotification); break;
            case hd::Id::semis: slider.setValue (s.semitones, juce::dontSendNotification); break;
            case hd::Id::f0: toggle.setToggleState (s.f0Condition, false); break;
            case hd::Id::autoF0: toggle.setToggleState (s.autoPitch, false); break;
            case hd::Id::fp16: combo.setSelectedId (s.fp16 < 0 ? 1 : s.fp16 == 1 ? 2 : 3, juce::dontSendNotification); break;
            case hd::Id::checkpoint:
            {
                combo.clear (juce::dontSendNotification);
                combo.addItem ("The engine's own pick", 1);
                const auto ms = hd::installedModels(); int sel = 1;
                for (size_t i = 0; i < ms.size(); ++i) { combo.addItem (ms[i].label, (int) i + 2); if (ms[i].checkpoint == s.checkpoint) sel = (int) i + 2; }
                if (s.checkpoint.isNotEmpty() && sel == 1) { combo.addItem ("(not found) " + juce::File (s.checkpoint).getFileName(), 99); sel = 99; }
                combo.setSelectedId (sel, juce::dontSendNotification);
                break;
            }
            default: break;
        }
        repaint();
    }
    bool matches (const juce::String& q) const
    {
        if (q.isEmpty()) return true;
        const juce::String hay = juce::String (opt.flag) + " " + opt.title + " " + opt.ours + " " + opt.jobKey + " " + opt.help + " " + opt.where;
        for (auto& w : juce::StringArray::fromTokens (q, " ", ""))
            if (! hay.containsIgnoreCase (w)) return false;
        return true;
    }
    HoodPage& page; const hd::Option& opt;
    juce::Slider slider; LiquidToggle toggle; juce::ComboBox combo;
    bool on = true;
};

struct HoodPage::Content : public juce::Component
{
    Content (HoodPage& p) : page (p)
    {
        for (auto& o : hd::options()) { rows.push_back (std::make_unique<Row> (p, o)); addAndMakeVisible (*rows.back()); }
    }
    void layoutRows (int width)
    {
        int y = 0;
        for (auto& r : rows)
        {
            const bool show = r->matches (query);
            r->setVisible (show);
            if (show) { r->setBounds (0, y, width, kRowH); y += kRowH; }
        }
        setSize (width, juce::jmax (y, 10));
    }
    static constexpr int kRowH = 92;
    HoodPage& page; std::vector<std::unique_ptr<Row>> rows; juce::String query;
};

// ------------------------------------------------------------------------------------------------ the page
HoodPage::HoodPage (Editor& e) : ed (e)
{
    content = std::make_unique<Content> (*this);
    vp.setViewedComponent (content.get(), false);
    vp.setScrollBarsShown (true, false);
    addAndMakeVisible (vp);
    search.setTextToShowWhenEmpty ("Search the options (name, flag, our control, job key)...", ed.pal().dim);
    search.onTextChange = [this] { filter(); };
    addAndMakeVisible (search);
    for (auto* b : { &startBtn, &defaultsBtn }) addAndMakeVisible (*b);
    startBtn.setTooltip ("Put every original option back to where Ghost Voice starts (the singing model, 30 steps, FOLLOW 0.70, PITCH 0): the job is then exactly build 3's");
    defaultsBtn.setTooltip ("Set the options to the neural engine's own inference.py defaults. Note its F0 condition default is False (the speech model), which the engine fetches itself the first time");
    startBtn.onClick = [this] { hd::setGhostVoiceStart (ed.proc.settings); ed.refresh(); };
    defaultsBtn.onClick = [this] { hd::setSeedVcDefaults (ed.proc.settings); ed.refresh(); };
    for (auto* t : { &cmdBox, &jobBox })
    {
        t->setMultiLine (true, true); t->setReadOnly (true); t->setCaretVisible (false); t->setScrollbarsShown (true);
        t->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain)));
        addAndMakeVisible (*t);
    }
    cmdBox.setTooltip ("The command line of the neural engine's inference.py for the blend on the balance slider: exactly the arguments the helper gives it (the last four are the Identity Lab's blend patch)");
    jobBox.setTooltip ("The job file helper/ghostvoice_engine.py reads. An option you never touch is not in it, so an untouched job is byte for byte build 3's");
    applyTheme();
}
HoodPage::~HoodPage() = default;

void HoodPage::applyTheme()
{
    const auto& P = ed.pal();
    search.applyColourToAllText (P.ink); search.setColour (juce::TextEditor::textColourId, P.ink);
    search.setColour (juce::TextEditor::backgroundColourId, P.cell);
    for (auto* t : { &cmdBox, &jobBox }) { t->setColour (juce::TextEditor::backgroundColourId, P.waveBg); t->setColour (juce::TextEditor::textColourId, P.ink); }
    for (auto& r : content->rows)
    { auto& t = r->toggle; t.on = P.violet; t.off = P.cell.brighter (0.1f); t.text = P.ink; t.ink = P.isLight ? juce::Colours::white : juce::Colour (0xfff5f1ff); t.edge = col::edge (P); }
    repaint();
}

// a job as the next CREATE / SING would send it (placeholders where a voice is not loaded yet); only used to show text
Job HoodPage::sampleJob() const
{
    auto& p = ed.proc;
    Job j;
    j.a = p.takes[slotA]; j.b = p.takes[slotB];
    if (j.a.isEmpty()) j.a.add ("<voice A take>");
    if (j.b.isEmpty()) j.b.add ("<voice B take>");
    j.performance = p.takes[slotPerf].isEmpty() ? j.a[0] : p.takes[slotPerf][0];
    j.settings = p.effectiveSettings(); j.rights = p.rights;
    j.mixes = { std::round (juce::jlimit (0.0f, 1.0f, p.blendB() + p.land().lean) * 1000.0f) / 1000.0f };
    j.outDir = workDir().getChildFile ("renders").getChildFile ("voiceC_<date>");
    return j;
}

juce::String HoodPage::commandText() const
{
    const auto j = sampleJob();
    return hd::commandLine (j, j.mixes[0], ! j.settings.cpuOnly);
}
juce::String HoodPage::jobText() const { return jobToJson (sampleJob()); }

bool HoodPage::touch (hd::Id id, double v)
{
    for (auto& r : content->rows)
    {
        if (r->opt.id != id) continue;
        if (r->opt.kind == hd::Kind::number) r->slider.setValue (v, juce::sendNotificationSync);
        else if (r->opt.kind == hd::Kind::toggle) r->toggle.setToggleState (v != 0, true);
        else if (r->opt.kind == hd::Kind::threeWay || r->opt.kind == hd::Kind::model) r->combo.setSelectedId ((int) v, juce::sendNotificationSync);
        else return false;
        return true;
    }
    return false;
}

int HoodPage::shownRows() const { int n = 0; for (auto& r : content->rows) if (r->matches (content->query)) ++n; return n; }

void HoodPage::filter()
{
    content->query = search.getText().trim();
    resized();
    repaint();
}

void HoodPage::sync()
{
    if (! isVisible()) return;
    for (auto& r : content->rows) r->update();
    const auto c = commandText(), j = jobText();
    if (c != cmdShown) { cmdShown = c; cmdBox.setText (shown (c), false); }
    if (j != jobShown) { jobShown = j; jobBox.setText (shown (j), false); }
    repaint();
}

void HoodPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (34);
    defaultsBtn.setBounds (top.removeFromRight (210)); top.removeFromRight (8);
    startBtn.setBounds (top.removeFromRight (170)); top.removeFromRight (10);
    search.setBounds (top);
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (juce::jmin (210, juce::jmax (120, r.getHeight() / 3)));
    r.removeFromBottom (6);
    vp.setBounds (r);
    content->layoutRows (juce::jmax (200, vp.getMaximumVisibleWidth()));
    bottom.removeFromTop (20);
    auto lb = bottom.removeFromLeft (bottom.getWidth() * 55 / 100); bottom.removeFromLeft (10);
    cmdBox.setBounds (lb); jobBox.setBounds (bottom);
}

void HoodPage::paint (juce::Graphics& g)
{
    const auto& P = ed.pal();
    auto r = getLocalBounds();
    r.removeFromTop (42);
    auto bottom = r.removeFromBottom (juce::jmin (210, juce::jmax (120, r.getHeight() / 3)));
    auto lb = bottom.removeFromLeft (bottom.getWidth() * 55 / 100); bottom.removeFromLeft (10);
    g.setFont (gfont (11.5f));
    g.setColour (P.ink); g.drawText ("SENT TO THE NEURAL ENGINE  (its inference.py, this blend)", lb.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("THE JOB FILE THE HELPER READS", bottom.removeFromTop (18), juce::Justification::centredLeft);
    if (shownRows() == 0)
    { g.setColour (P.dim); g.setFont (gfont (14.0f, false)); g.drawText ("No original option matches that.", vp.getBounds().withHeight (60), juce::Justification::centred); }
}
}
