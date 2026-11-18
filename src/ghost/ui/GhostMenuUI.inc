// ================================================================================================
// build 11 SOUND PICKER - included by GhostEasyView.cpp inside namespace ghost::ui (it uses that file's look helpers).
//   PresetMenu : the drop-down on the patch name (and on the blend board's slots): ONE column of TYPES (BASS, LEAD, PAD ...
//                with their counts) and, beside it, ONE flat A-Z list of every sound of the chosen type; a search box on top;
//                the library is indexed once (EasyView::Impl::library()).
// It is our own component on the canvas (not a native pop-up menu) because a search box needs the keyboard.
// ================================================================================================

struct PresetMenu : public juce::Component
{
    struct SearchBox : public juce::TextEditor
    {
        std::function<bool(const juce::KeyPress &)> onKey;
        bool keyPressed(const juce::KeyPress &k) override
        {
            if (onKey && onKey(k))
                return true;
            return juce::TextEditor::keyPressed(k);
        }
    };
    using Pick = EasyView::Impl::PickResult;
    using Opts = EasyView::Impl::MenuOpts;

    static constexpr float kTypeW = 270.f, kRow = 36.f, kPad = 16.f, kSearchH = 44.f, kPanelH = 560.f, kPanelW = 900.f;

    EasyView::Impl &I;
    SearchBox search;
    juce::String query;
    std::vector<int> results;
    int hi{0};
    float resScroll{0.f};
    int selType{0};
    float listScroll{0.f};
    Opts opts;
    std::function<void(const Pick &)> cb;
    R panel;
    bool hoverIsResult{false};
    int hoverSlot{-1}, hoverRow{-1}; // slot 0 = the types column, 1 = the list

    void applyColours()
    {
        search.setColour(juce::TextEditor::backgroundColourId, col::bg);
        search.setColour(juce::TextEditor::textColourId, col::ink);
        search.setColour(juce::TextEditor::outlineColourId, col::line);
        search.setColour(juce::TextEditor::focusedOutlineColourId, col::cyan);
        search.applyColourToAllText(col::ink);
    }
    explicit PresetMenu(EasyView::Impl &i) : I(i)
    {
        setVisible(false);
        setWantsKeyboardFocus(false);
        search.setMultiLine(false);
        search.setReturnKeyStartsNewLine(false);
        search.setFont(Fr(16.f, 1));
        applyColours();
        search.setColour(juce::CaretComponent::caretColourId, col::cyan);
        search.setIndents(12, 10);
        search.onTextChange = [this] { runSearch(); };
        search.onEscapeKey = [this] { close(); };
        search.onReturnKey = [this] { pickResult(hi); };
        search.onKey = [this](const juce::KeyPress &k) {
            if (query.isEmpty())
                return false;
            if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey)
            {
                hi = juce::jlimit(0, std::max(0, (int)results.size() - 1), hi + (k == juce::KeyPress::downKey ? 1 : -1));
                const float top = hi * kRow, h = listRect().getHeight();
                if (top < resScroll)
                    resScroll = top;
                else if (top + kRow > resScroll + h)
                    resScroll = top + kRow - h;
                repaint();
                return true;
            }
            return false;
        };
        addChildComponent(search);
    }

    bool isOpen() const { return isVisible(); }
    const EasyView::Impl::Index &lib() const { return I.library(); }
    const std::vector<int> &listItems() const
    {
        static const std::vector<int> none;
        const auto &L = lib();
        return selType >= 0 && selType < (int)L.types.size() ? L.types[(size_t)selType].items : none;
    }

    // ---- open / close ----
    void open(R anchor, const Opts &o, std::function<void(const Pick &)> onPick)
    {
        opts = o;
        cb = std::move(onPick);
        const auto &L = lib();
        float x = std::clamp(anchor.getX(), 8.f, (float)EasyView::W - 8.f - kPanelW);
        float y = anchor.getBottom() + 6.f;
        if (y + kPanelH > (float)EasyView::H - 8.f)
            y = (float)EasyView::H - 8.f - kPanelH;
        y = std::max(8.f, y);
        panel = R(x, y, kPanelW, kPanelH);
        setBounds(0, 0, EasyView::W, EasyView::H);
        // open on the type of the sound that is playing, with that sound in view
        selType = 0;
        listScroll = 0.f;
        const int cur = I.currentIndex();
        if (cur >= 0 && cur < (int)L.typeOf.size())
        {
            selType = L.typeOf[(size_t)cur];
            const auto &it = L.types[(size_t)selType].items;
            for (size_t k = 0; k < it.size(); ++k)
                if (it[k] == cur)
                    listScroll = std::max(0.f, (float)k * kRow - listRect().getHeight() * 0.4f);
        }
        else
            for (size_t t = 0; t < L.types.size(); ++t)
                if (L.types[t].name == "BASS")
                    selType = (int)t;
        query = {};
        results.clear();
        hi = 0;
        resScroll = 0.f;
        search.setTextToShowWhenEmpty((opts.title.empty() ? juce::String("search ") : juce::String(opts.title) + "  -  search ") +
                                          juce::String(L.all.size()) + " sounds",
                                      col::dim);
        search.clear();
        search.setBounds(juce::Rectangle<float>(panel.getX() + kPad, panel.getY() + kPad, panel.getWidth() - 2.f * kPad, kSearchH).toNearestInt());
        setVisible(true);
        toFront(false);
        search.setVisible(true);
        search.grabKeyboardFocus();
        repaint();
    }
    void close()
    {
        if (!isVisible())
            return;
        setVisible(false);
        search.clear();
        query = {};
        cb = nullptr;
        if (auto *p = getParentComponent())
            p->repaint();
    }

    // ---- search ----
    void runSearch()
    {
        query = search.getText().trim();
        results.clear();
        hi = 0;
        resScroll = 0.f;
        if (query.isNotEmpty())
        {
            const auto &L = lib();
            std::vector<std::string> tok;
            {
                juce::StringArray sa;
                sa.addTokens(query.toLowerCase(), " ", "");
                sa.removeEmptyStrings();
                for (auto &s : sa)
                    tok.push_back(s.toStdString());
            }
            std::vector<int> early, later;
            for (size_t i = 0; i < L.all.size(); ++i)
            {
                bool ok = true;
                for (auto &t : tok)
                    if (L.hay[i].find(t) == std::string::npos)
                    {
                        ok = false;
                        break;
                    }
                if (!ok)
                    continue;
                (L.lname[i].compare(0, tok[0].size(), tok[0]) == 0 ? early : later).push_back((int)i);
                if (early.size() + later.size() >= 2000)
                    break;
            }
            results = early;
            results.insert(results.end(), later.begin(), later.end());
        }
        repaint();
    }
    int resultCount() const { return (int)results.size(); }
    void pickResult(int k)
    {
        if (k < 0 || k >= (int)results.size())
            return;
        pickItem(results[(size_t)k]);
    }

    // ---- the two columns ----
    int nSpecial() const { return (opts.current ? 1 : 0) + (opts.empty ? 1 : 0); }
    int typeRows() const { return nSpecial() + (int)lib().types.size(); }
    R typesRect() const
    {
        return R(panel.getX() + kPad, panel.getY() + kPad + kSearchH + 12.f, kTypeW, panel.getHeight() - 2.f * kPad - kSearchH - 12.f);
    }
    R listRect() const
    {
        if (query.isNotEmpty())
            return R(panel.getX() + kPad, panel.getY() + kPad + kSearchH + 12.f, panel.getWidth() - 2.f * kPad,
                     panel.getHeight() - 2.f * kPad - kSearchH - 12.f);
        const float x = panel.getX() + kPad + kTypeW + 14.f;
        return R(x, panel.getY() + kPad + kSearchH + 12.f, panel.getRight() - kPad - x, panel.getHeight() - 2.f * kPad - kSearchH - 12.f);
    }

    // ---- drawing ----
    void drawRow(juce::Graphics &g, R r, const juce::String &text, const juce::String &right, bool bold, bool opened, bool hot, bool cur, bool special)
    {
        const float rad = 10.f;
        if (cur)
        {
            g.setGradientFill(accentGrad(r));
            fillChamfer(g, r, rad);
        }
        else if (opened)
        {
            g.setColour(col::violet.withAlpha(0.55f));
            fillChamfer(g, r, rad);
        }
        else if (hot)
        {
            g.setColour(col::cell);
            fillChamfer(g, r, rad);
        }
        else if (special)
        {
            g.setColour(col::cyan.withAlpha(0.14f));
            fillChamfer(g, r, rad);
        }
        g.setColour(cur ? col::bg : (special ? col::cyan : col::ink));
        g.setFont(F(12.5f, (cur || bold || special) ? 2 : 1));
        const float rw = right.isEmpty() ? 0.f : (bold ? 64.f : 240.f);
        dt(g, text, r.reduced(14, 0).withTrimmedRight(rw), juce::Justification::centredLeft);
        if (right.isNotEmpty())
        {
            g.setColour(cur ? col::bg : (opened ? col::ink : col::dim));
            g.setFont(Fr(13.f, 1));
            dt(g, right, r.reduced(14, 0).removeFromRight(rw), juce::Justification::centredRight);
        }
    }
    void drawScrollRail(juce::Graphics &g, R cr, float tot, float scroll)
    {
        if (tot > cr.getHeight())
        {
            g.setColour(col::line.withAlpha(0.6f));
            const float th = std::max(24.f, cr.getHeight() * cr.getHeight() / tot);
            fillChamfer(g, R(cr.getRight() - 4.f, cr.getY() + (cr.getHeight() - th) * scroll / (tot - cr.getHeight()), 3.f, th), 1.5f);
        }
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.5f));
        panelBg(g, panel.expanded(3.5f), 18.f);
        const auto &L = lib();
        const int curIdx = I.currentIndex();
        g.setColour(col::bg.withAlpha(0.55f));
        fillChamfer(g, listRect().expanded(2.f), 10.f);
        auto lr = listRect();
        if (query.isNotEmpty())
        {
            g.saveState();
            g.reduceClipRegion(lr.toNearestInt());
            if (results.empty())
            {
                g.setColour(col::dim);
                g.setFont(F(12.f, 1));
                dt(g, "no sound matches \"" + query + "\"", lr, juce::Justification::centred);
            }
            const int first = std::max(0, (int)(resScroll / kRow));
            for (int k = first; k < (int)results.size(); ++k)
            {
                R r(lr.getX() + 2.f, lr.getY() + (float)k * kRow - resScroll, lr.getWidth() - 12.f, kRow - 4.f);
                if (r.getY() > lr.getBottom())
                    break;
                const auto &p = L.all[(size_t)results[(size_t)k]];
                drawRow(g, r, p.name, L.types[(size_t)L.typeOf[(size_t)results[(size_t)k]]].name, false, false, k == hi || (hoverIsResult && hoverRow == k),
                        results[(size_t)k] == curIdx, false);
            }
            g.restoreState();
            drawScrollRail(g, lr, (float)results.size() * kRow, resScroll);
            g.setColour(col::dim);
            g.setFont(Fr(12.5f, 1));
            dt(g, juce::String((int)results.size()) + (results.size() >= 2000 ? "+ " : " ") + "found   -   up / down + enter, or click",
               R(panel.getX() + kPad + 4.f, panel.getBottom() - kPad - 2.f, panel.getWidth() - 2.f * kPad, 16.f), juce::Justification::centredLeft);
            return;
        }
        // the types column
        {
            auto tr = typesRect();
            int row = 0;
            auto next = [&]() { return R(tr.getX(), tr.getY() + (float)(row++) * (kRow + 2.f), tr.getWidth(), kRow); };
            if (opts.current)
            {
                const bool hot = hoverSlot == 0 && hoverRow == row;
                drawRow(g, next(), "CURRENT SOUND", {}, false, false, hot, false, true);
            }
            if (opts.empty)
            {
                const bool hot = hoverSlot == 0 && hoverRow == row;
                drawRow(g, next(), "EMPTY THIS SLOT", {}, false, false, hot, false, true);
            }
            for (size_t t = 0; t < L.types.size(); ++t)
            {
                const bool hot = hoverSlot == 0 && hoverRow == row;
                drawRow(g, next(), juce::String(L.types[t].name), juce::String((int)L.types[t].items.size()), true, (int)t == selType, hot, false, false);
            }
        }
        // the flat list of the chosen type
        {
            const auto &items = listItems();
            g.saveState();
            g.reduceClipRegion(lr.toNearestInt());
            const int first = std::max(0, (int)(listScroll / kRow));
            for (int k = first; k < (int)items.size(); ++k)
            {
                R r(lr.getX() + 2.f, lr.getY() + (float)k * kRow - listScroll, lr.getWidth() - 12.f, kRow - 4.f);
                if (r.getY() > lr.getBottom())
                    break;
                const auto &p = L.all[(size_t)items[(size_t)k]];
                drawRow(g, r, p.name, juce::String(p.category), false, false, hoverSlot == 1 && hoverRow == k, items[(size_t)k] == curIdx, false);
            }
            g.restoreState();
            drawScrollRail(g, lr, (float)items.size() * kRow, listScroll);
        }
    }

    // ---- mouse ----
    bool hit(juce::Point<float> p, int &slot, int &row) const
    {
        slot = row = -1;
        if (query.isNotEmpty())
        {
            auto lr = listRect();
            if (!lr.contains(p))
                return false;
            slot = 1;
            row = (int)((p.y - lr.getY() + resScroll) / kRow);
            return row >= 0 && row < (int)results.size();
        }
        auto tr = typesRect();
        if (tr.contains(p))
        {
            slot = 0;
            row = (int)((p.y - tr.getY()) / (kRow + 2.f));
            return row >= 0 && row < typeRows();
        }
        auto lr = listRect();
        if (lr.contains(p))
        {
            slot = 1;
            row = (int)((p.y - lr.getY() + listScroll) / kRow);
            return row >= 0 && row < (int)listItems().size();
        }
        return false;
    }
    void mouseMove(const juce::MouseEvent &e) override
    {
        int s, r;
        const bool ok = hit(e.position, s, r);
        const int ns = ok ? s : -1, nr = ok ? r : -1;
        if (ns != hoverSlot || nr != hoverRow || hoverIsResult != (query.isNotEmpty()))
        {
            hoverSlot = ns;
            hoverRow = nr;
            hoverIsResult = query.isNotEmpty();
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        hoverSlot = hoverRow = -1;
        repaint();
    }
    void pickItem(int idx)
    {
        const auto &p = lib().all[(size_t)idx];
        auto done = std::move(cb);
        Pick pk;
        pk.kind = Pick::PATCH;
        pk.path = path_to_string(p.path);
        pk.name = p.name;
        close();
        if (done)
            done(pk);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!e.mouseWasClicked())
            return;
        if (!panel.expanded(6.f).contains(e.position))
        {
            close();
            return;
        }
        int s, r;
        if (!hit(e.position, s, r))
            return;
        if (query.isNotEmpty())
        {
            pickResult(r);
            return;
        }
        if (s == 1)
        {
            pickItem(listItems()[(size_t)r]);
            return;
        }
        int k = r;
        if (opts.current)
        {
            if (k == 0)
            {
                finishSpecial(Pick::CURRENT);
                return;
            }
            --k;
        }
        if (opts.empty)
        {
            if (k == 0)
            {
                finishSpecial(Pick::EMPTY);
                return;
            }
            --k;
        }
        if (k >= 0 && k < (int)lib().types.size())
        {
            selType = k;
            listScroll = 0.f;
            repaint();
        }
    }
    void finishSpecial(Pick::Kind kind)
    {
        auto done = std::move(cb);
        Pick pk;
        pk.kind = kind;
        close();
        if (done)
            done(pk);
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    {
        const float d = -w.deltaY * 260.f;
        if (query.isNotEmpty())
        {
            const float mx = std::max(0.f, (float)results.size() * kRow - listRect().getHeight());
            resScroll = std::clamp(resScroll + d, 0.f, mx);
            repaint();
            return;
        }
        if (listRect().expanded(4.f, 0.f).contains(e.position))
        {
            const float mx = std::max(0.f, (float)listItems().size() * kRow - listRect().getHeight());
            listScroll = std::clamp(listScroll + d, 0.f, mx);
            repaint();
        }
    }
    bool keyPressed(const juce::KeyPress &k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }
};
