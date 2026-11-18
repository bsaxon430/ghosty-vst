#include "Hood.h"
#include "HoodCatalog.inc"
#include "HoodDialogs.inc"
#include "HoodFriendly.inc"

namespace glab::hood
{
// ------------------------------------------------------------------------------------------------ the catalogue
static std::vector<Process> loadCatalog()
{
    juce::String all;
    for (int i = 0; kHoodCatalogChunks[i] != nullptr; ++i) all << juce::String::fromUTF8 (kHoodCatalogChunks[i]);
    const auto root = juce::JSON::parse (all);
    std::vector<Process> out;
    const auto* procs = root["processes"].getArray();
    if (procs == nullptr) return out;
    for (auto& pv : *procs)
    {
        Process p;
        p.prog = pv["prog"].toString(); p.name = pv["name"].toString();
        p.title = pv["title"].toString(); p.progTitle = pv["progTitle"].toString();
        p.usage = pv["usage"].toString();
        if (auto* lab = pv["labels"].getDynamicObject())
            for (auto& kv : lab->getProperties()) p.labels[kv.name.toString().getIntValue()] = kv.value.toString();
        if (auto* forms = pv["forms"].getArray())
            for (auto& fv : *forms)
            {
                Form f;
                f.usage = fv["usage"].toString(); f.modeSpec = fv["mode"].toString();
                if (auto* ms = fv["modes"].getArray()) for (auto& m : *ms) f.modes.push_back ((int) m);
                f.inputs = (int) fv["nin"]; f.out = fv["out"].toString(); f.outExt = fv["outext"].toString(); f.multi = (bool) fv["multi"];
                if (auto* ps = fv["params"].getArray())
                    for (auto& qv : *ps)
                    {
                        Param q;
                        q.name = qv["name"].toString(); q.kind = qv["kind"].toString(); q.flag = qv["flag"].toString();
                        q.help = qv["help"].toString(); q.defaultText = qv["default"].toString(); q.rangeText = qv["range"].toString();
                        q.optional = (bool) qv["optional"]; q.ext = qv["ext"].toString(); q.main = (bool) qv["main"]; q.pre = (bool) qv["pre"];
                        f.params.push_back (std::move (q));
                    }
                p.forms.push_back (std::move (f));
            }
        out.push_back (std::move (p));
    }
    return out;
}

const std::vector<Process>& catalog() { static const auto c = loadCatalog(); return c; }

const Process* find (const juce::String& prog, const juce::String& name)
{
    for (auto& p : catalog()) if (p.prog == prog && p.name == name) return &p;
    return nullptr;
}

const Form* formFor (const Process& p, int mode)
{
    for (auto& f : p.forms)
    {
        if (f.modeSpec.isEmpty()) { if (mode == 0) return &f; continue; }
        for (int m : f.modes) if (m == mode) return &f;
    }
    if (mode != 0) for (auto& f : p.forms) if (f.modeSpec.isEmpty()) return &f;   // a mode given to a process without modes
    return nullptr;
}

std::vector<int> modesOf (const Process& p)
{
    std::vector<int> v;
    for (auto& f : p.forms) for (int m : f.modes) if (std::find (v.begin(), v.end(), m) == v.end()) v.push_back (m);
    std::sort (v.begin(), v.end());
    return v;
}

std::vector<std::pair<juce::String, juce::String>> groups()
{
    std::vector<std::pair<juce::String, juce::String>> g;
    for (auto& p : catalog())
    {
        bool seen = false;
        for (auto& e : g) seen = seen || e.first == p.prog;
        if (! seen) g.push_back ({ p.prog, p.progTitle });
    }
    return g;
}

std::map<juce::String, juce::String> startValues (const Form& f)
{
    std::map<juce::String, juce::String> v;
    for (auto& p : f.params)
        if (p.kind == "value" || p.kind == "flagvalue") { if (p.defaultText.isNotEmpty()) v[p.name] = p.defaultText; }
    return v;
}

// ------------------------------------------------------------------------------------------------ a request
juce::String Request::toData() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("prog", prog); o->setProperty ("name", name); o->setProperty ("mode", mode);
    o->setProperty ("points", points); o->setProperty ("overlap", overlap);
    if (! extraFiles.isEmpty()) { juce::Array<juce::var> xs; for (auto& f : extraFiles) xs.add (f); o->setProperty ("extra", xs); }
    if (dataDir.isNotEmpty()) o->setProperty ("dataDir", dataDir);
    auto* vals = new juce::DynamicObject();
    for (auto& kv : values) vals->setProperty (kv.first, kv.second);
    o->setProperty ("values", juce::var (vals));
    return juce::JSON::toString (juce::var (o), true);
}

bool Request::fromData (const juce::String& d, Request& out)
{
    const auto v = juce::JSON::parse (d);
    if (! v.isObject()) return false;
    out = {};
    out.prog = v["prog"].toString(); out.name = v["name"].toString(); out.mode = (int) v["mode"];
    if (v.hasProperty ("points")) out.points = v["points"].toString();
    if (v.hasProperty ("overlap")) out.overlap = v["overlap"].toString();
    if (auto* xs = v["extra"].getArray()) for (auto& x : *xs) out.extraFiles.add (x.toString());
    out.dataDir = v["dataDir"].toString();
    if (auto* vals = v["values"].getDynamicObject())
        for (auto& kv : vals->getProperties()) out.values[kv.name.toString()] = kv.value.toString();
    return out.prog.isNotEmpty() && out.name.isNotEmpty();
}

juce::String Request::describe() const
{
    return "HOOD " + prog + " " + name + (mode > 0 ? " " + juce::String (mode) : juce::String());
}

bool Request::spectral() const
{
    return prog == "blur" || prog == "combine" || prog == "focus" || prog == "morph" || prog == "strange" || prog == "stretch"
        || prog == "repitch" || prog == "formants";
}

// ------------------------------------------------------------------------------------------------ the command words
bool buildWords (const Process& p, const Form& f, const Request& r, const juce::StringArray& inFiles, const juce::String& outFile,
                 const std::function<juce::String (const juce::String&)>& makeFile, juce::StringArray& words, juce::String& error,
                 const std::function<juce::String (const Param&)>& makeOut)
{
    words.clear();
    words.add (p.prog); words.add (p.name);
    if (f.modeSpec.isNotEmpty()) words.add (juce::String (r.mode));
    words.addArray (inFiles);
    auto word = [&] (const Param& q) -> bool
    {
        if (q.kind == "dataout")
        {
            // a data file the program writes: the main output takes the output name, another gets its own
            if (q.main) words.add (outFile.isNotEmpty() ? outFile : q.name + q.ext);
            else words.add (makeOut ? makeOut (q) : q.name + q.ext);
            return true;
        }
        auto it = r.values.find (q.name);
        const auto val = it != r.values.end() ? it->second.trim() : juce::String();
        if (q.kind == "switch") { if (val == "1") words.add (q.flag); return true; }
        if (val.isEmpty())
        {
            if (q.optional || q.kind == "flagvalue") return true;
            error = "Fill in " + q.name.toUpperCase() + " (the engine needs it for " + p.prog + " " + p.name + ").";
            return false;
        }
        if (q.kind == "either")
        {
            if (! val.startsWith ("-f") && ! val.startsWith ("-p")) { error = q.name + ": start with -f or -p and then the number, like -f9."; return false; }
            words.add (val);
            return true;
        }
        const bool asFile = q.kind == "datafile" || val.containsAnyOf (" \t\r\n");
        const auto text = asFile ? makeFile (val) : val;
        words.add (q.kind == "flagvalue" ? q.flag + text : text);
        return true;
    };
    // the words before the output file (the input data files of combine make), the output, then the rest
    bool hasMain = false;
    for (auto& q : f.params) hasMain = hasMain || (q.kind == "dataout" && q.main);
    for (auto& q : f.params) if (q.pre && ! word (q)) return false;
    if (outFile.isNotEmpty() && ! (f.out == "data" && hasMain)) words.add (outFile);   // (a data output named by a usage word takes its own place)
    for (auto& q : f.params) if (! q.pre && ! word (q)) return false;
    // flags the engine's own table accepts but its usage line leaves out (the ORIGINAL page shows them from cdparams, e.g.
    // brassage's -d): keyed by their flag word, "1" = ticked, anything else with a value = a flag with that value
    for (auto& kv : r.values)
    {
        const auto key = kv.first;
        if (! key.startsWith ("-") || key.length() != 2) continue;
        bool inForm = false;
        for (auto& q : f.params) inForm = inForm || q.name == key || q.flag == key;
        if (inForm) continue;
        const auto val = kv.second.trim();
        if (val.isEmpty() || val == "0") continue;
        words.add (val == "1" ? key : key + val);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ the ORIGINAL pages
static DialogItem itemFromVar (const juce::var& iv)
{
    DialogItem it;
    it.kind = iv["kind"].toString(); it.name = iv["name"].toString();
    it.name2 = iv["name2"].toString(); it.name3 = iv["name3"].toString(); it.type = iv["type"].toString();
    it.lo = (double) iv["lo"]; it.hi = (double) iv["hi"]; it.dflt = (double) iv["default"];
    it.sublo = iv.hasProperty ("sublo") ? (double) iv["sublo"] : it.lo; it.subhi = iv.hasProperty ("subhi") ? (double) iv["subhi"] : it.hi;
    it.lo2 = (double) iv["lo2"]; it.hi2 = (double) iv["hi2"]; it.dflt2 = (double) iv["default2"]; it.ranges = (int) iv["ranges"];
    it.param = iv["param"].toString(); it.flag = iv["flag"].toString();
    it.optional = (bool) iv["optional"]; it.extra = (bool) iv["extra"];
    return it;
}

static std::vector<Dialog> loadDialogs()
{
    juce::String all;
    for (int i = 0; kHoodDialogChunks[i] != nullptr; ++i) all << juce::String::fromUTF8 (kHoodDialogChunks[i]);
    const auto root = juce::JSON::parse (all);
    std::vector<Dialog> out;
    const auto* ds = root["dialogs"].getArray();
    if (ds == nullptr) return out;
    for (auto& dv : *ds)
    {
        Dialog d;
        d.prog = dv["prog"].toString(); d.name = dv["name"].toString(); d.constName = dv["const"].toString();
        d.procNo = (int) dv["procno"]; d.mode = (int) dv["mode"]; d.maxmode = (int) dv["maxmode"]; d.brklen = (int) dv["brklen"];
        d.raw = dv["raw"].toString();
        if (auto* a = dv["problems"].getArray()) for (auto& s : *a) d.problems.add (s.toString());
        if (auto* a = dv["notes"].getArray()) for (auto& s : *a) d.notes.add (s.toString());
        if (auto* items = dv["items"].getArray()) for (auto& iv : *items) d.items.push_back (itemFromVar (iv));
        out.push_back (std::move (d));
    }
    return out;
}

const std::vector<Dialog>& dialogs() { static const auto d = loadDialogs(); return d; }

const Dialog* dialogFor (const juce::String& prog, const juce::String& name, int mode)
{
    for (auto& d : dialogs()) if (d.prog == prog && d.name == name && d.mode == mode) return &d;
    if (mode != 0) for (auto& d : dialogs()) if (d.prog == prog && d.name == name && d.maxmode == 0) return &d;
    return nullptr;
}

SoundInfo referenceSound() { return {}; }

// the file kinds the engine's front end names (filetype.h): a sound, or the analysis the spectral programs read
enum { kSndFile = 1, kAnalFile = 24, kPitchFile = 25 };

juce::StringArray cdparamsArgs (const Dialog& d, const SoundInfo& s)
{
    int filetype = kAnalFile, nin = 1;
    if (d.prog == "modify" || (d.prog == "pvoc" && d.name == "anal")) filetype = kSndFile;
    if (d.prog == "combine" && (d.name == "make" || d.name == "make2")) { filetype = kPitchFile; nin = d.name == "make" ? 2 : 3; }
    if (const auto* p = find (d.prog, d.name))
        for (auto& f : p->forms) nin = juce::jmax (nin, f.inputs);
    const auto insams = (juce::int64) juce::jmax<juce::int64> (1, s.samples);
    const double dur = s.sampleRate > 0 ? (double) insams / s.sampleRate : 0.0;
    juce::StringArray a { "cdparams", juce::String (d.procNo), juce::String (d.mode), juce::String (nin), juce::String (filetype) };
    auto num = [] (double v) { return juce::String (v, 9); };   // the same spelling the generator used (gen-hood-dialogs.py arg)
    if (filetype == kSndFile)
    {
        a.addArray ({ juce::String (insams * 2), juce::String (insams), juce::String ((int) s.sampleRate), juce::String (s.channels),
                      "0", "0", "0", num (0.0), num (0.0), num (s.sampleRate / 2.0), num (dur) });
    }
    else
    {
        const int points = juce::jmax (2, s.points), overlap = juce::jmax (1, s.overlap);
        const int wanted = points + 2, decim = juce::jmax (1, points / overlap);
        const juce::int64 wlength = insams / decim;
        const double frametime = decim / s.sampleRate, arate = s.sampleRate / decim;
        const int chans = filetype == kPitchFile ? 1 : wanted;
        const juce::int64 size = filetype == kPitchFile ? wlength : wlength * wanted;
        a.addArray ({ juce::String (size * 4), juce::String (size), juce::String ((int) s.sampleRate), juce::String (chans), juce::String (wanted),
                      juce::String (wlength), "0", num (arate), num (frametime), num (s.sampleRate / 2.0), num (dur) });
    }
    return a;
}

bool parseDialogText (const juce::String& text, Dialog& out)
{
    out.items.clear();
    bool gotBrk = false, ended = false;
    for (auto& raw : juce::StringArray::fromLines (text))
    {
        const auto s = raw.trim();
        if (s.isEmpty()) continue;
        if (s.startsWith ("ERROR")) { out.problems.add (s); return false; }
        if (s == "ENDPARAMS") { ended = true; break; }
        if (! gotBrk && s.containsOnly ("-0123456789")) { out.brklen = s.getIntValue(); gotBrk = true; continue; }
        // quoted names (may hold spaces), then numbers / a type letter
        juce::StringArray names, rest;
        for (int i = 0; i < s.length();)
        {
            if (s[i] == '"') { int j = s.indexOf (i + 1, "\""); if (j < 0) j = s.length(); names.add (s.substring (i + 1, j)); i = j + 1; }
            else if (s[i] == ' ') ++i;
            else { int j = s.indexOf (i, " "); if (j < 0) j = s.length(); rest.add (s.substring (i, j)); i = j; }
        }
        if (rest.isEmpty()) continue;
        DialogItem it;
        it.kind = rest[0]; rest.remove (0);
        it.name = names.size() > 0 ? names[0] : juce::String();
        auto n = [&] (int k) { return k < rest.size() ? rest[k].getDoubleValue() : 0.0; };
        if (it.kind == "LINEAR" || it.kind == "LOG" || it.kind == "POWTWO" || it.kind == "PLOG")
        { it.type = rest[0]; it.lo = n (1); it.hi = n (2); it.dflt = n (3); it.sublo = n (4); it.subhi = n (5); }
        else if (it.kind == "SWITCHED")
        { it.name2 = names.size() > 1 ? names[1] : juce::String(); it.name3 = names.size() > 2 ? names[2] : juce::String();
          it.type = rest[0]; it.lo = n (1); it.hi = n (2); it.dflt = n (3); it.lo2 = n (4); it.hi2 = n (5); it.dflt2 = n (6); }
        else if (it.kind == "FILENAME") { it.ranges = (int) n (0); it.lo = n (1); it.hi = n (2); it.lo2 = n (3); it.hi2 = n (4); }
        else if (it.kind == "FILE_OR_VAL" || it.kind == "NUMERIC" || it.kind == "LOGNUMERIC")
        { it.type = rest[0]; it.lo = n (1); it.hi = n (2); it.dflt = n (3); }
        if (it.kind != "LINEAR" && it.kind != "LOG" && it.kind != "POWTWO" && it.kind != "PLOG") { it.sublo = it.lo; it.subhi = it.hi; }
        out.items.push_back (std::move (it));
    }
    return ended;
}

std::unique_ptr<Dialog> liveDialog (const Cdp& cdp, const Dialog& baked, const SoundInfo& s)
{
    if (! cdp.programsDir().getChildFile ("cdparams").existsAsFile()) return nullptr;
    auto dir = cdp.newJobFolder();
    const auto r = cdp.run (cdparamsArgs (baked, s), dir, {}, nullptr, 20000);
    dir.deleteRecursively();
    if (! r.ok) return nullptr;
    auto d = std::make_unique<Dialog> (baked);
    d->raw = r.log;
    if (! parseDialogText (r.log, *d) || d->items.size() != baked.items.size()) return nullptr;
    for (size_t i = 0; i < d->items.size(); ++i)
    {
        auto& it = d->items[i]; const auto& b = baked.items[i];
        if (it.kind != b.kind || it.name != b.name) return nullptr;
        it.param = b.param; it.flag = b.flag; it.optional = b.optional; it.extra = b.extra;   // what each drives never changes
    }
    return d;
}

juce::String plainWords (const Process& p, const Form& f, const DialogItem& it)
{
    for (auto& q : f.params)
        if (q.name == it.param || (it.flag.isNotEmpty() && q.flag == it.flag && q.kind != "switch"))
        {
            auto help = q.help;
            if (help.isEmpty())   // the same word spelt a little differently in another usage line of the process (pitchshft / pitchshift)
                for (auto& g : p.forms) for (auto& r : g.params)
                    if (help.isEmpty() && r.help.isNotEmpty() && r.kind == q.kind && (r.name.startsWith (q.name.substring (0, 5)) && q.name.startsWith (r.name.substring (0, 5)))) help = r.help;
            auto h = shownText (help).replace ("\n", " ").trim();
            // the first sentence of the engine's own words, in plain form
            const int cut = juce::jmax (h.indexOf (". "), h.indexOf (": "));
            if (cut > 12) h = h.substring (0, cut + 1);
            if (h.length() > 90) h = h.substring (0, 88).trim() + "...";
            juce::String w = q.name.toLowerCase().replace ("-fn|-pn", "formant bands");
            if (q.kind == "flagvalue") w << " (" << q.flag << ")";
            if (h.isNotEmpty() && ! h.equalsIgnoreCase (w)) w << "  -  " << h;
            return w;
        }
    if (it.kind == "CHECKBUTTON") return it.flag + "  switch";
    return it.param.toLowerCase();
}

juce::String formatValue (double v, bool integer)
{
    if (integer || std::abs (v - std::round (v)) < 1e-9) return juce::String ((juce::int64) std::llround (v));
    auto s = juce::String (v, 6);
    while (s.contains (".") && (s.endsWith ("0"))) s = s.dropLastCharacters (1);
    if (s.endsWith (".")) s = s.dropLastCharacters (1);
    return s;
}

std::map<juce::String, juce::String> dialogDefaults (const Dialog& d)
{
    std::map<juce::String, juce::String> v;
    for (auto& it : d.items)
    {
        if (it.param.isEmpty()) continue;
        if (it.kind == "CHECKBUTTON") { v[it.param] = "0"; continue; }
        if (it.kind == "SWITCHED") { v[it.param] = "-p" + formatValue (it.dflt, true); continue; }   // the engine's first choice (PITCHWISE)
        if (it.textual()) { v[it.param] = ""; continue; }
        if (it.optional && it.flag.isNotEmpty()) { v[it.param] = ""; continue; }   // an option not sent = the engine's own default
        v[it.param] = formatValue (it.dflt, it.integer());
    }
    return v;
}

// ------------------------------------------------------------------------------------------------ running it
namespace
{
struct Runner
{
    const Cdp& cdp;
    juce::File dir;
    const std::atomic<bool>* cancel;
    juce::StringArray& commands;
    juce::String error;
    int counter = 0;

    juce::File name (const juce::String& ext) { return dir.getChildFile ("f" + juce::String (++counter) + ext); }
    bool failed() const { return error.isNotEmpty(); }

    // true when the program ran and (if out is given) wrote it
    bool run (juce::StringArray args, const juce::File& out, juce::String* logOut = nullptr)
    {
        if (failed()) return false;
        commands.add (args.joinIntoString (" "));
        auto r = cdp.run (args, dir, out, cancel, 600000);
        // CDP's own words go under the command, so the user sees exactly what the program said
        for (auto& ln : juce::StringArray::fromLines (r.log))
            if (ln.trim().isNotEmpty() && ! ln.startsWith ("CDP Release")) commands.add ("  # " + ln.trim());
        if (logOut != nullptr) *logOut = r.log;
        if (! r.ok) { error = r.error; return false; }
        return true;
    }
    juce::File wav (const Audio& a)
    {
        auto f = name (".wav");
        if (! writeWav (a, f, 32, error)) return {};
        return f;
    }
    juce::File makeFile (const juce::String& typed)
    {
        const auto t = typed.trim();
        if (juce::File::isAbsolutePath (t) && juce::File (t).existsAsFile())
        {
            auto f = name (juce::File (t).getFileExtension());
            juce::File (t).copyFileTo (f);
            return f;
        }
        auto f = name (".txt");
        f.replaceWithText (typed.trim() + "\n", false, false, "\n");
        return f;
    }
};

juce::String cdpWords (const juce::String& log)
{
    for (auto& line : juce::StringArray::fromLines (log))
        if (line.containsIgnoreCase ("ERROR")) return line.fromFirstOccurrenceOf ("ERROR", false, true).trimCharactersAtStart (": ").trim();
    return {};
}
}

juce::String shownText (const juce::String& t)
{
    juce::StringArray out;
    for (auto& ln : juce::StringArray::fromLines (t))
    {
        if (ln.trimStart().startsWith ("CDP Release")) continue;   // the release banner every program prints first
        out.add (ln.replace ("CDP's", "the engine's").replace ("CDP8", "ENGINE").replace ("CDP", "ENGINE"));
    }
    while (out.size() > 0 && out[0].trim().isEmpty()) out.remove (0);
    return out.joinIntoString ("\n");
}

// a data output lands in the session's data folder with a readable name
static juce::File dataDestination (const Request& rq, const juce::String& word, const juce::String& ext)
{
    auto dir = rq.dataDir.isNotEmpty() ? juce::File (rq.dataDir) : Cdp::defaultWorkRoot().getSiblingFile ("data");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%H%M%S");
    return dir.getNonexistentChildFile (rq.prog + "-" + rq.name + (rq.mode > 0 ? "-" + juce::String (rq.mode) : juce::String()) + "-" + word + "-" + stamp, ext, false);
}

RenderResult renderHood (const Step& s, const Audio& inRaw, const Audio* partnerRaw, const Cdp& cdp, const std::atomic<bool>* cancel)
{
    RenderResult res;
    Request rq;
    if (! Request::fromData (s.data, rq)) { res.error = "That UNDER THE HOOD step has no process."; return res; }
    const auto* proc = find (rq.prog, rq.name);
    const auto* form = proc != nullptr ? formFor (*proc, rq.mode) : nullptr;
    if (form == nullptr) { res.error = "The engine has no " + rq.prog + " " + rq.name + (rq.mode > 0 ? " mode " + juce::String (rq.mode) : juce::String()) + "."; return res; }
    if (! cdp.available()) { res.error = "Ghost Lab's sound engine is missing. Reinstall Ghost Lab."; return res; }
    if (form->inputs >= 2 && partnerRaw == nullptr) { res.error = rq.prog + " " + rq.name + " needs two sounds: drop a second sound (SPECIMEN 002) first."; return res; }

    const bool spectral = rq.spectral();
    const bool roundtrip = rq.prog == "pvoc" && (rq.name == "anal" || rq.name == "synth");
    bool hasDataOut = form->out == "data";
    for (auto& q : form->params) hasDataOut = hasDataOut || q.kind == "dataout";
    const bool perChannel = spectral || rq.prog == "pvoc";          // the spectral programs work on one channel
    const bool twoSpectral = form->inputs >= 2 && perChannel;
    const int workCh = (twoSpectral || hasDataOut || inRaw.channels() < 2 || isEffectivelyMono (inRaw) || ! perChannel) ? 1 : inRaw.channels();

    std::unique_ptr<Audio> partner;
    auto matchChannels = [&] (Audio pa) -> Audio
    {
        if (perChannel) return mono (pa);
        if (inRaw.channels() == 1) return mono (pa);
        if (inRaw.channels() == 2 && pa.channels() == 1)
        {
            Audio st; st.sampleRate = pa.sampleRate; st.buf.setSize (2, pa.length());
            for (int c = 0; c < 2; ++c) st.buf.copyFrom (c, 0, pa.buf, 0, 0, pa.length());
            return st;
        }
        return pa;
    };
    if (partnerRaw != nullptr && form->inputs >= 2) partner = std::make_unique<Audio> (matchChannels (resampled (*partnerRaw, inRaw.sampleRate)));
    // the extra sounds of a process that takes several (sausage ...): read now, at the specimen's rate and channel count
    std::vector<std::shared_ptr<Audio>> extras;
    if (form->multi)
        for (auto& path : rq.extraFiles)
        {
            juce::String err;
            auto a = loadAudio (juce::File (path), err);
            if (a == nullptr) { res.error = "Couldn't read " + juce::File (path).getFileName() + (err.isNotEmpty() ? ": " + err : juce::String()); return res; }
            extras.push_back (std::make_shared<Audio> (matchChannels (resampled (*a, inRaw.sampleRate))));
        }

    auto dir = cdp.newJobFolder();
    Runner j { cdp, dir, cancel, res.commands, {}, 0 };
    std::vector<std::shared_ptr<Audio>> parts;
    juce::String textOut;
    std::vector<std::pair<juce::File, juce::File>> dataFiles;   // written in the job folder -> kept in the data folder

    for (int c = 0; c < workCh && ! j.failed(); ++c)
    {
        // the sounds the program reads
        juce::StringArray ins;
        auto toInput = [&] (const Audio& a) -> juce::File
        {
            auto w = j.wav (a);
            if (j.failed() || ! spectral) return w;
            auto an = j.name (".ana");
            j.run ({ "pvoc", "anal", "1", w.getFileName(), an.getFileName(), "-c" + rq.points, "-o" + rq.overlap }, an);
            return an;
        };
        if (form->inputs >= 1)
        {
            const Audio a = perChannel ? (twoSpectral || workCh == 1 ? mono (inRaw) : channel (inRaw, c)) : inRaw;
            auto f = toInput (a);
            if (j.failed()) break;
            ins.add (f.getFileName());
        }
        if (form->inputs >= 2)
        {
            auto f = toInput (*partner);
            if (j.failed()) break;
            ins.add (f.getFileName());
        }
        for (auto& x : extras)
        {
            auto f = toInput (*x);
            if (j.failed()) break;
            ins.add (f.getFileName());
        }
        if (j.failed()) break;

        if (roundtrip)
        {
            // pvoc anal / pvoc synth on their own make an analysis file, which is not sound: to hear it, it is turned back
            auto w = j.name (".wav");
            juce::File wavIn = dir.getChildFile (ins[0]);
            auto an = j.name (".ana");
            juce::StringArray aw { "pvoc", "anal", rq.name == "anal" && rq.mode > 0 ? juce::String (rq.mode) : "1", wavIn.getFileName(), an.getFileName() };
            if (rq.name == "anal")
            {
                juce::StringArray words;
                juce::String err;
                if (! buildWords (*proc, *form, rq, { wavIn.getFileName() }, an.getFileName(), [&] (const juce::String& t) { return j.makeFile (t).getFileName(); }, words, err))
                { j.error = err; break; }
                aw = words;
            }
            else { aw.add ("-c" + rq.points); aw.add ("-o" + rq.overlap); }
            if (! j.run (aw, an)) break;
            if (rq.name == "anal" && rq.mode > 1) { j.error = "pvoc anal mode " + juce::String (rq.mode) + " makes analysis data (a spectral file), not a sound. It ran; see the command log."; break; }
            if (! j.run ({ "pvoc", "synth", an.getFileName(), w.getFileName() }, w)) break;
            juce::String err;
            auto a = readWav (w, err);
            if (a == nullptr) { j.error = err; break; }
            parts.push_back (a);
            continue;
        }

        // the program itself
        juce::File out;
        juce::String outName;
        if (form->out == "sound") { out = j.name (spectral ? ".ana" : ".wav"); outName = out.getFileName(); }
        else if (form->out == "text") { out = j.name (".txt"); outName = out.getFileName(); }
        else if (form->out == "data") { out = j.name (form->outExt.isNotEmpty() ? form->outExt : juce::String (".dat")); outName = out.getFileName(); dataFiles.push_back ({ out, dataDestination (rq, "out", out.getFileExtension()) }); }
        juce::StringArray words;
        juce::String err;
        if (! buildWords (*proc, *form, rq, ins, outName, [&] (const juce::String& t) { return j.makeFile (t).getFileName(); }, words, err,
                          [&] (const Param& q) { auto f = j.name (q.ext.isNotEmpty() ? q.ext : juce::String (".dat")); dataFiles.push_back ({ f, dataDestination (rq, q.name, f.getFileExtension()) }); return f.getFileName(); }))
        { j.error = err; break; }
        juce::String log;
        // a text result: some CDP programs write the outtextfile, some print the answer; either way it is CDP's own answer
        if (! j.run (words, form->out == "text" ? juce::File() : out, &log))
        {
            const auto what = cdpWords (log);
            if (what.isNotEmpty()) j.error = rq.prog + " " + rq.name + ": " + what.substring (0, 140);
            break;
        }
        if (form->out == "text") { textOut = out.existsAsFile() ? out.loadFileAsString().trim() : log.trim(); res.commands.add ("  # result: " + textOut.replace ("\n", " | ")); break; }
        if (form->out == "none" || form->out == "data") { break; }

        juce::File wavOut = out;
        if (spectral)
        {
            wavOut = j.name (".wav");
            if (! j.run ({ "pvoc", "synth", out.getFileName(), wavOut.getFileName() }, wavOut)) break;
        }
        juce::String rerr;
        auto a = readWav (wavOut, rerr);
        if (a == nullptr) { j.error = rerr; break; }
        parts.push_back (a);
    }
    // the data files the program wrote are kept (the page lists them; the pages that need such files can pick them)
    if (! j.failed())
        for (auto& df : dataFiles)
        {
            if (! df.first.existsAsFile() || df.first.getSize() == 0) { j.error = rq.prog + " " + rq.name + " did not write " + df.first.getFileName() + "."; break; }
            df.second.getParentDirectory().createDirectory();
            if (! df.first.copyFileTo (df.second)) { j.error = "Couldn't keep " + df.second.getFileName() + "."; break; }
            res.commands.add ("  # DATA " + df.second.getFileExtension().trimCharactersAtStart (".") + ": " + df.second.getFullPathName());
        }
    dir.deleteRecursively();
    if (j.failed()) { res.error = j.error; return res; }

    if (parts.empty())
    {
        // the process makes no sound (a text result, a calculation, a data file): the sound stays, the answer is in the command log
        auto same = std::make_shared<Audio>();
        same->sampleRate = inRaw.sampleRate;
        same->buf.makeCopyOf (inRaw.buf);
        res.audio = same;
        return res;
    }

    auto out = std::make_shared<Audio>();
    out->sampleRate = parts.front()->sampleRate;
    if (workCh == 1) out->buf = parts.front()->buf;
    else
    {
        int len = INT_MAX;
        for (auto& p : parts) len = juce::jmin (len, p->length());
        out->buf.setSize (workCh, len);
        for (int c = 0; c < workCh; ++c)
            out->buf.copyFrom (c, 0, parts[(size_t) c]->buf, juce::jmin (c, parts[(size_t) c]->channels() - 1), 0, len);
    }
    if (std::abs (out->sampleRate - inRaw.sampleRate) > 0.5) *out = resampled (*out, inRaw.sampleRate);
    if (out->length() < 16) { res.error = "That made no sound."; return res; }
    if (out->seconds() > kMaxOutputSeconds) { res.error = "That would be longer than 12 minutes."; return res; }
    // exactly what CDP wrote: no gain, no fades, no finishing (UNDER THE HOOD is the real program's output)
    res.audio = out;
    return res;
}

// ------------------------------------------------------------------------------------------------ MAKE FROM SOUND
juce::String dataKindFor (const juce::String& w)
{
    const auto n = w.toLowerCase();
    if (n.contains ("pitch") || n == "pfil") return "pitch";
    if (n.contains ("formant") || n.contains ("fmnt")) return "formant";
    if (n.contains ("env")) return "envelope";
    return {};
}

juce::File writeSoundFile (const Audio& a, const juce::File& dataDir, const juce::String& stem)
{
    dataDir.createDirectory();
    auto f = dataDir.getNonexistentChildFile (stem, ".wav", false);
    juce::String err;
    if (! writeWav (a, f, 32, err)) return {};
    return f;
}

DataMade makeData (const Cdp& cdp, const Audio& sound, const juce::String& kind, const juce::String& points, const juce::String& overlap, const juce::File& dataDir)
{
    DataMade m;
    if (! cdp.available()) { m.error = "Ghost Lab's sound engine is missing."; return m; }
    juce::String ext = kind == "pitch" ? ".frq" : kind == "formant" ? ".for" : kind == "envelope" ? ".evl" : juce::String();
    if (ext.isEmpty()) { m.error = "No engine program makes " + kind + " data."; return m; }
    auto dir = cdp.newJobFolder();
    Runner j { cdp, dir, nullptr, m.commands, {}, 0 };
    auto w = j.wav (mono (sound));
    auto an = j.name (".ana");
    auto out = j.name (ext);
    if (! j.failed()) j.run ({ "pvoc", "anal", "1", w.getFileName(), an.getFileName(), "-c" + points, "-o" + overlap }, an);
    if (! j.failed())
    {
        if (kind == "pitch") { auto tone = j.name (".ana"); j.run ({ "repitch", "getpitch", "1", an.getFileName(), tone.getFileName(), out.getFileName() }, out); }
        else if (kind == "formant") j.run ({ "formants", "get", an.getFileName(), out.getFileName(), "-p4" }, out);
        else j.run ({ "repitch", "analenv", an.getFileName(), out.getFileName() }, out);
    }
    if (! j.failed())
    {
        dataDir.createDirectory();
        m.file = dataDir.getNonexistentChildFile (kind + "-" + juce::Time::getCurrentTime().formatted ("%H%M%S"), ext, false);
        if (! out.copyFileTo (m.file)) j.error = "Couldn't keep the " + kind + " data.";
    }
    dir.deleteRecursively();
    m.error = j.error;
    return m;
}

// ------------------------------------------------------------------------------------------------ in plain words
namespace
{
struct Friendly
{
    std::vector<Category> cats;
    std::vector<Entry> entries;
    std::map<juce::String, juce::String> controls;
};
const Friendly& friendly()
{
    static const Friendly f = []
    {
        Friendly r;
        juce::String all;
        for (int i = 0; kHoodFriendlyChunks[i] != nullptr; ++i) all << juce::String::fromUTF8 (kHoodFriendlyChunks[i]);
        const auto root = juce::JSON::parse (all);
        // each category's colour (the cards on the LAB page use the same family)
        const std::map<juce::String, juce::uint32> colours { { "time", 0xff4fd8ff }, { "pitch", 0xffb46bff }, { "tone", 0xffff5fd2 }, { "texture", 0xffd8f23a },
                                                             { "space", 0xff6b8cff }, { "blend", 0xff5cf2a0 }, { "tools", 0xffa9adb8 } };
        if (auto* cs = root["cats"].getArray())
            for (auto& c : *cs)
            {
                Category k { c["id"].toString(), c["title"].toString(), c["blurb"].toString(), juce::Colour (0xffa9adb8) };
                auto it = colours.find (k.id); if (it != colours.end()) k.colour = juce::Colour (it->second);
                r.cats.push_back (k);
            }
        if (auto* es = root["entries"].getArray())
            for (auto& e : *es)
            {
                Entry x;
                x.prog = e["prog"].toString(); x.name = e["name"].toString(); x.mode = (int) e["mode"];
                x.cat = e["cat"].toString(); x.title = e["title"].toString(); x.shortText = e["short"].toString();
                x.about = e["about"].toString(); x.credit = e["credit"].toString();
                x.proc = find (x.prog, x.name);
                if (x.proc != nullptr)
                    if (auto* f = formFor (*x.proc, x.mode)) { x.inputs = f->inputs; x.multi = f->multi; x.out = f->out; }
                r.entries.push_back (std::move (x));
            }
        if (auto* o = root["controls"].getDynamicObject())
            for (auto& kv : o->getProperties()) r.controls[kv.name.toString()] = kv.value.toString();
        return r;
    }();
    return f;
}
}

const std::vector<Category>& categories() { return friendly().cats; }
const Category* category (const juce::String& id) { for (auto& c : friendly().cats) if (c.id == id) return &c; return nullptr; }
const std::vector<Entry>& entries() { return friendly().entries; }
const Entry* entryFor (const juce::String& prog, const juce::String& name, int mode)
{
    for (auto& e : friendly().entries) if (e.prog == prog && e.name == name && e.mode == mode) return &e;
    for (auto& e : friendly().entries) if (e.prog == prog && e.name == name) return &e;
    return nullptr;
}
std::vector<const Entry*> versionsOf (const Process& p)
{
    std::vector<const Entry*> v;
    for (auto& e : friendly().entries) if (e.prog == p.prog && e.name == p.name) v.push_back (&e);
    std::sort (v.begin(), v.end(), [] (const Entry* a, const Entry* b) { return a->mode < b->mode; });
    return v;
}
juce::String friendlyControl (const juce::String& engineName)
{
    auto it = friendly().controls.find (engineName);
    if (it != friendly().controls.end()) return it->second;
    auto s = engineName.replaceCharacter ('_', ' ').trim().toLowerCase();
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}
}

