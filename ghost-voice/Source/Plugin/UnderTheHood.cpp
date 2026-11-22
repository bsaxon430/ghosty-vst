// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "UnderTheHood.h"

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
    // (polish 1) no developer folder is ever shown raw: every other absolute path is shortened to its last part ("…/voice-a.wav"),
    // and a number never shows float noise (0.699999988 -> 0.7). commandText() / jobText() stay exact; only the boxes show this.
    juce::String out; const int n = t.length(); int i = 0;
    auto stop = [] (juce::juce_wchar c) { return c == ' ' || c == '"' || c == '\n' || c == ',' || c == '\'' || c == ']'; };
    while (i < n)
    {
        const auto c = t[i];
        const bool tokenStart = i == 0 || t[i - 1] == ' ' || t[i - 1] == '"' || t[i - 1] == '\'' || t[i - 1] == '[';
        if (c == '/' && tokenStart)
        {
            int j = i; while (j < n && ! stop (t[j])) ++j;
            const auto tok = t.substring (i, j);
            out << choir::friendlyPath (tok.endsWithChar ('/') ? tok.dropLastCharacters (1) : tok);
            i = j; continue;
        }
        if (juce::CharacterFunctions::isDigit (c) && (i == 0 || ! juce::CharacterFunctions::isLetterOrDigit (t[i - 1])))
        {
            int j = i; while (j < n && juce::CharacterFunctions::isDigit (t[j])) ++j;
            if (j + 1 < n && t[j] == '.' && juce::CharacterFunctions::isDigit (t[j + 1]))
            {
                int k = j + 1; while (k < n && juce::CharacterFunctions::isDigit (t[k])) ++k;
                if (k - j - 1 > 4)
                {
                    auto v = juce::String (t.substring (i, k).getDoubleValue(), 3);
                    while (v.containsChar ('.') && (v.endsWithChar ('0') || v.endsWithChar ('.'))) { const bool dot = v.endsWithChar ('.'); v = v.dropLastCharacters (1); if (dot) break; }
                    out << v; i = k; continue;
                }
                out << t.substring (i, k); i = k; continue;
            }
            out << t.substring (i, j); i = j; continue;
        }
        out << c; ++i;
    }
    return uiWords (out);
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
    choir::Cache art;
    void paint (juce::Graphics& g) override
    {
        const auto& st = page.ed.proc.settings;
        juce::String k; k << (int) page.ed.cp().dark << getWidth() << "x" << getHeight() << (int) on << valueLine (st) << givenLine() << warning();
        art.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintArt (ig); });
    }
    void paintArt (juce::Graphics& g)
    {   // a hymn-book entry: its number in the margin, our title, the engine's own flag, its help; the value / control on the right
        const auto& P = page.ed.cp();
        const auto& s = page.ed.proc.settings;
        // (polish 2) a compact entry: four set lines on the left (title, flag, help, original default), the value, the live
        // control and ours on the right. Nothing wraps, nothing is sliced
        auto r = getLocalBounds().toFloat().reduced (2, 2);
        choir::page (g, P, r);
        const float a = on ? 1.0f : 0.42f;
        auto num = r.removeFromLeft (40);
        g.setColour (choir::greyInk (P, P.gold, a)); g.setFont (choir::display (20.0f)); g.drawText (juce::String (number), num, juce::Justification::centred, false);
        g.setColour (P.edgeDim); g.fillRect (num.getRight(), r.getY() + 8, 1.0f, r.getHeight() - 16);
        auto left = r.reduced (12, 5).withTrimmedRight (318.0f);
        const auto t = uiWords (opt.title);
        g.setColour (choir::greyInk (P, P.ink, a)); g.setFont (choir::fitted (choir::disp, t, left.getWidth(), 17.0f, 11.0f));
        g.drawText (t, left.removeFromTop (20), juce::Justification::centredLeft, false);
        const auto fl = juce::String (opt.flag) + "     " + opt.where + "  -  " + opt.type;
        g.setColour (choir::greyInk (P, P.select, a)); g.setFont (choir::fitted (choir::bold, fl, left.getWidth(), 14.0f, 9.0f));
        g.drawText (fl, left.removeFromTop (16), juce::Justification::centredLeft, false);
        const auto help = uiWords (opt.help);
        {   // (polish 3) the help is always 14 px, like every row: a long one wraps onto more lines and the row grows to fit
            const int n = helpLines (getWidth());
            juce::AttributedString as; as.append (help, choir::serif (14.0f), choir::greyInk (P, P.dim, a)); as.setWordWrap (juce::AttributedString::byWord);
            as.setLineSpacing (16.0f - choir::serif (14.0f).getHeight()); as.setJustification (juce::Justification::topLeft);
            juce::TextLayout tl; tl.createLayout (as, left.getWidth());
            auto box = left.removeFromTop (16.0f * (float) n);
            tl.draw (g, box.withTrimmedTop (juce::jmax (0.0f, (16.0f - choir::serif (14.0f).getHeight()) * 0.5f)));
        }
        juce::String f1 = juce::String ("original default: ") + uiWords (opt.upstreamDefault);
        if (juce::String (opt.range).isNotEmpty()) f1 << "   range " << opt.range;
        g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, shown (f1), left.getWidth(), 13.5f, 9.5f));
        g.drawText (shown (f1), left.removeFromTop (15), juce::Justification::centredLeft, true);
        auto right = r.reduced (12, 5).removeFromRight (306.0f);
        if (isControlled())
        {
            auto line = right.removeFromTop (20);
            const auto v = shown (valueLine (s));
            g.setColour (choir::greyInk (P, P.dark ? P.gold : P.edge.darker (0.3f), a)); g.setFont (choir::fitted (choir::bold, v, line.getWidth(), 15.5f, 10.0f));
            g.drawText (v, line, juce::Justification::centredRight, false);
            right.removeFromTop (28);
        }
        else
        {
            const auto gl = shown (givenLine());
            g.setColour (choir::greyInk (P, P.ink, a)); g.setFont (choir::serif (14.5f));
            g.drawFittedText (gl, right.removeFromTop (46).toNearestInt(), juce::Justification::topRight, 3, 0.9f);
            right.removeFromTop (2);
        }
        const juce::String f2 = juce::String (opt.ours) != "" ? juce::String ("ours: ") + opt.ours : juce::String();
        if (f2.isNotEmpty()) { g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, f2, right.getWidth(), 13.5f, 9.5f)); g.drawText (f2, right.withY ((float) getHeight() - 22).withHeight (16), juce::Justification::centredRight, false); }
        if (const auto w = warning(); w.isNotEmpty())
        { g.setColour (P.warn); g.setFont (choir::serif (13.0f, choir::italic)); g.drawText (uiWords (w), juce::Rectangle<float> (right.getX(), (float) getHeight() - 22, right.getWidth(), 16), juce::Justification::centredLeft, true); }
    }
    void resized() override
    {
        auto c = getLocalBounds().reduced (14, 7).removeFromRight (306).withTrimmedTop (22).withHeight (26);
        slider.setBounds (c.withTrimmedLeft (60)); toggle.setBounds (c.withWidth (40).withX (c.getRight() - 40)); combo.setBounds (c.withTrimmedLeft (40).reduced (0, 1));
    }
    // how many 14 px lines the help needs at this row width (rows grow 16 px per extra line)
    int helpLines (int width) const
    {
        const float w = (float) width - 4.0f - 40.0f - 24.0f - 318.0f;
        if (w <= 10.0f) return 1;
        juce::AttributedString as; as.append (uiWords (opt.help), choir::serif (14.0f)); as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (16.0f - choir::serif (14.0f).getHeight());
        juce::TextLayout tl; tl.createLayout (as, w);
        return juce::jlimit (1, 3, tl.getNumLines());
    }
    int heightFor (int width) const { return 78 + 16 * (helpLines (width) - 1); }
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
    int number = 0;
    juce::Slider slider; choir::Candle toggle { "" }; juce::ComboBox combo;
    bool on = true;
};

struct HoodPage::Content : public juce::Component
{
    Content (HoodPage& p) : page (p)
    {
        for (auto& o : hd::options()) { rows.push_back (std::make_unique<Row> (p, o)); rows.back()->number = (int) rows.size(); addAndMakeVisible (*rows.back()); }
    }
    void layoutRows (int width)
    {
        int y = 0;
        for (auto& r : rows)
        {
            const bool show = r->matches (query);
            r->setVisible (show);
            if (show) { const int h = r->heightFor (width); r->setBounds (0, y, width, h); y += h; }
        }
        setSize (width, juce::jmax (y, 10));
    }
    static constexpr int kRowH = 78;
    HoodPage& page; std::vector<std::unique_ptr<Row>> rows; juce::String query;
};

// ------------------------------------------------------------------------------------------------ the page
HoodPage::HoodPage (Editor& e) : ed (e)
{
    content = std::make_unique<Content> (*this);
    vp.setViewedComponent (content.get(), false);
    vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (12);   // (polish 2) a scrollbar you can see: there are 14 options
    addAndMakeVisible (vp);
    search.setTextToShowWhenEmpty ("Search the options (name, flag, our control, job key)...", ed.cp().faint);
    search.setFont (choir::serif (16.0f)); search.setIndents (10, 6);
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
        t->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain))); t->setIndents (10, 8);
        addAndMakeVisible (*t);
        t->setBufferedToImage (true);   // long read-only text: painted once into an image, again only when it changes
    }
    others[intersectSound] = hoodui::makeIntersectSound (ed);
    others[intersectAutoChop] = hoodui::makeIntersectAutoChop (ed);
    others[phrases] = hoodui::makePhrases (ed);
    others[flayr] = hoodui::makeFlayr (ed);
    for (int i = 0; i < kTabs; ++i)
    {
        if (others[i] != nullptr) addChildComponent (*others[i]);
        auto& b = tabs[i];
        b.setButtonText (tabTitle (i)); b.setRadioGroupId (0x600d); b.setClickingTogglesState (false); b.setWantsKeyboardFocus (false);
        b.getProperties().set ("fontH", 14.0f);
        b.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }
    tabs[seedvc].setTooltip ("Seed-VC's own options: the neural engine's singing conversion, under its own flag names");
    tabs[intersectSound].setTooltip ("INTERSECT's own sound controls (its signal chain): the clip's and each chop's settings");
    tabs[intersectAutoChop].setTooltip ("INTERSECT's own auto-chop panel: split a chop at its transients or into equal pieces");
    tabs[phrases].setTooltip ("OpenVPI audio-slicer's own options: how PHRASES finds the silences");
    tabs[flayr].setTooltip ("Flayr Chop's own read-outs and AUTO-CHOP section: the tempo, key and hits it finds");
    tabs[seedvc].setToggleState (true, juce::dontSendNotification);
    cmdBox.setTooltip ("The command line of the neural engine's inference.py for the blend on the balance slider: exactly the arguments the helper gives it (the last four are the Identity Lab's blend patch)");
    jobBox.setTooltip ("The job file helper/ghostvoice_engine.py reads. An option you never touch is not in it, so an untouched job is byte for byte build 3's");
    applyTheme();
}
HoodPage::~HoodPage() = default;

void HoodPage::applyTheme()
{
    const auto& P = ed.cp();
    search.setColour (juce::TextEditor::textColourId, P.ink); search.setColour (juce::TextEditor::backgroundColourId, P.waveBg);
    search.setTextToShowWhenEmpty ("Search the options (name, flag, our control, job key)...", P.faint);
    search.applyColourToAllText (P.ink);
    for (auto* t : { &cmdBox, &jobBox })
    {   // (round 0 bug: LIGHT kept DARK's white text in these boxes. Re-colour the text that is already there, too.)
        t->setColour (juce::TextEditor::backgroundColourId, P.waveBg); t->setColour (juce::TextEditor::textColourId, P.ink);
        t->applyColourToAllText (P.ink, true);
    }
    for (auto& r : content->rows) r->toggle.P = &P;
    for (int k = 1; k < kTabs; ++k) if (others[k] != nullptr) { for (auto* c : others[k]->cells()) { c->P = &P; c->repaint(); } others[k]->repaint(); }
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
    if (auto* p = panel (tabIndex)) { p->sync(); return; }
    for (auto& r : content->rows) r->update();
    const auto c = commandText(), j = jobText();
    if (c != cmdShown) { cmdShown = c; cmdBox.setText (shown (c), false); }
    if (j != jobShown)
    {
        jobShown = j; const auto t = shown (j); jobBox.setText (t, false);
        const int lines = juce::StringArray::fromLines (t).size();
        const float lh = lines > 1 ? (float) (jobBox.getTextHeight() - 8) / (float) lines : 0.0f;
        juce::ignoreUnused (lh);
    }
    repaint();
}

juce::String HoodPage::tabTitle (int i) const
{
    switch (i)
    {
        case seedvc: return "SEED-VC";
        case intersectSound: return "INTERSECT  SOUND";
        case intersectAutoChop: return "INTERSECT  AUTO CHOP";
        case phrases: return "OPENVPI  PHRASES";
        case flayr: return "FLAYR CHOP";
        default: return {};
    }
}

hoodui::Panel* HoodPage::panel (int i) const { return i > 0 && i < kTabs ? others[i].get() : nullptr; }

void HoodPage::showTab (int i)
{
    tabIndex = juce::jlimit (0, kTabs - 1, i);
    for (int k = 0; k < kTabs; ++k) tabs[k].setToggleState (k == tabIndex, juce::dontSendNotification);
    const bool seed = tabIndex == seedvc;
    for (juce::Component* c : { (juce::Component*) &search, (juce::Component*) &cmdBox, (juce::Component*) &jobBox, (juce::Component*) &startBtn,
                                (juce::Component*) &defaultsBtn, (juce::Component*) &vp })
        c->setVisible (seed);
    for (int k = 1; k < kTabs; ++k) if (others[k] != nullptr) others[k]->setVisible (k == tabIndex);
    resized();
    if (auto* p = panel (tabIndex)) p->sync();
    else sync();
    repaint();
    if (onTabChange) onTabChange();
}

juce::String HoodPage::subLine() const
{
    if (auto* p = panel (tabIndex)) return p->subLine();
    return "Every option of the neural engine's singing conversion, under its own name, default and range. NATURAL and CLASSIC are ours, so they are not here.";
}

juce::String HoodPage::countLine() const
{
    if (auto* p = panel (tabIndex)) return p->countLine();
    int live = 0, given = 0, unused = 0;
    for (auto& o : hd::options()) { if (o.kind == hd::Kind::given || o.kind == hd::Kind::modelConfig) ++given; else if (o.kind == hd::Kind::notUsed) ++unused; else ++live; }
    return juce::String ((int) hd::options().size()) + " original options listed: " + juce::String (live) + " live controls, " + juce::String (given) + " set for you by the cards and the checkpoint, " + juce::String (unused) + " not used on a Mac";
}

std::vector<std::pair<juce::String, juce::String>> HoodPage::sideRows() const
{
    if (auto* p = panel (tabIndex)) return p->sideRows();
    return {};
}

void HoodPage::resized()
{
    // the sub-tabs: one per open-source engine, each the width its title needs, filling the row
    auto bar = getLocalBounds().removeFromTop (32);
    {
        float need[kTabs], total = 0;
        for (int i = 0; i < kTabs; ++i) { need[i] = choir::width (choir::serif (14.0f, choir::bold), tabTitle (i)) + 36.0f; total += need[i]; }
        const float spare = juce::jmax (0.0f, (float) bar.getWidth() - total - 8.0f * (kTabs - 1));
        float x = (float) bar.getX();
        for (int i = 0; i < kTabs; ++i)
        {
            const float w = need[i] + spare * need[i] / total;
            const int x0 = juce::roundToInt (x), x1 = i == kTabs - 1 ? bar.getRight() : juce::roundToInt (x + w);
            tabs[i].setBounds (x0, bar.getY(), x1 - x0, bar.getHeight());
            x += w + 8.0f;
        }
    }
    auto area = getLocalBounds().withTrimmedTop (32 + 10);
    for (int k = 1; k < kTabs; ++k) if (others[k] != nullptr) others[k]->setBounds (area);
    layoutSeed (area);
}

void HoodPage::layoutSeed (juce::Rectangle<int> r)
{
    seedArea = r;
    auto top = r.removeFromTop (34);
    defaultsBtn.setBounds (top.removeFromRight (210)); top.removeFromRight (8);
    startBtn.setBounds (top.removeFromRight (170)); top.removeFromRight (10);
    search.setBounds (top);
    r.removeFromTop (8);
    // (round 3) the list shows whole rows only: a row is never sliced under the boxes' header. What a whole row leaves goes to the boxes
    const int minBottom = juce::jmin (170, juce::jmax (120, r.getHeight() / 3));
    // (polish 3) rows have their own heights now (a long help wraps): lay them out, then take as many whole rows as fit from the top
    vp.setBounds (r.withHeight (juce::jmax (Content::kRowH, r.getHeight() - minBottom - 14)));
    const int rowW = juce::jmax (200, r.getWidth() - vp.getScrollBarThickness());   // (the scrollbar always shows: 14 options)
    content->layoutRows (rowW);
    int listH = 0;
    for (auto& row : content->rows)
    {
        if (! row->isVisible()) continue;
        if (listH > 0 && row->getBottom() > vp.getHeight()) break;
        listH = row->getBottom();
    }
    auto list = r.removeFromTop (juce::jmax (Content::kRowH, listH));
    r.removeFromTop (14);
    auto bottom = r;
    boxesArea = bottom;
    vp.setBounds (list);
    content->layoutRows (rowW);
    bottom.removeFromTop (22);
    // (polish 1) the boxes hold whole lines: their height is the top indent, a whole number of lines and as much again at the foot,
    // so no line is ever cut through at the bottom (the rest scrolls)
    const float lh = cmdBox.getFont().getHeight();
    const int lines = juce::jmax (2, (int) std::floor ((float) (bottom.getHeight() - 12) / lh));
    bottom = bottom.withHeight (juce::jmin (bottom.getHeight(), 8 + (int) std::floor ((float) lines * lh) + 3));
    auto lb = bottom.removeFromLeft (bottom.getWidth() * 55 / 100); bottom.removeFromLeft (10);
    cmdBox.setBounds (lb); jobBox.setBounds (bottom);
}

void HoodPage::paint (juce::Graphics& g)
{
    const auto& P = ed.cp();
    if (tabIndex != seedvc) return;
    auto bottom = boxesArea;
    auto lb = bottom.removeFromLeft (bottom.getWidth() * 55 / 100); bottom.removeFromLeft (10);
    g.setColour (P.gold); g.setFont (choir::display (14.0f));
    g.drawText ("SENT TO THE NEURAL ENGINE  (its inference.py, this blend)", lb.removeFromTop (20), juce::Justification::centredLeft, false);
    g.drawText ("THE JOB FILE THE HELPER READS", bottom.removeFromTop (20), juce::Justification::centredLeft, false);
    if (shownRows() == 0)
    { g.setColour (P.dim); g.setFont (choir::serif (17.0f, choir::italic)); g.drawText ("No original option matches that.", vp.getBounds().withHeight (60), juce::Justification::centred, false); }
}
}
