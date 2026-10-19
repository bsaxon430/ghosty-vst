#pragma once
// Type an exact value into any knob or slider: right-click it (two-finger click on a Mac trackpad, or ctrl-click),
// type the number (e.g. 140 for tempo), Enter applies, Esc cancels. Double-click still resets to the default.
// One TypeIn per editor covers every slider inside it (it listens to mouse clicks globally and only reacts to
// sliders that belong to its own editor).
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghosty {

class TypeIn : private juce::MouseListener {
public:
    explicit TypeIn(juce::Component& editorRoot) : root(editorRoot) { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    ~TypeIn() override { juce::Desktop::getInstance().removeGlobalMouseListener(this); }

private:
    juce::Component& root;

    struct Box : juce::Component {
        juce::TextEditor ed; juce::Component::SafePointer<juce::Slider> s;
        explicit Box(juce::Slider& sl) : s(&sl) {
            ed.setText(sl.getTextFromValue(sl.getValue()), false); ed.setJustification(juce::Justification::centred);
            ed.setFont(juce::FontOptions(16.0f, juce::Font::bold)); ed.setSelectAllWhenFocused(true);
            ed.onReturnKey = [this] { apply(); }; ed.onEscapeKey = [this] { close(); };
            addAndMakeVisible(ed); setSize(120, 34);
        }
        void resized() override { ed.setBounds(getLocalBounds()); }
        void visibilityChanged() override { if (isShowing()) ed.grabKeyboardFocus(); }
        void apply() {
            if (s != nullptr) {
                const auto t = ed.getText().trim();
                if (t.containsAnyOf("0123456789")) {
                    const double v = juce::jlimit(s->getMinimum(), s->getMaximum(), s->getValueFromText(t));
                    s->setValue(v, juce::sendNotificationSync);
                }
            }
            close();
        }
        void close() { if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss(); }
    };

    void mouseDown(const juce::MouseEvent& e) override {
        if (! e.mods.isPopupMenu() || e.eventComponent == nullptr) return;
        if (! root.isParentOf(e.eventComponent) && e.eventComponent != &root) return;
        juce::Component* c = e.eventComponent; juce::Slider* s = nullptr;
        while (c != nullptr && c != &root && s == nullptr) { s = dynamic_cast<juce::Slider*>(c); c = c->getParentComponent(); }
        if (s == nullptr || ! s->isEnabled()) return;
        auto box = std::make_unique<Box>(*s);
        juce::CallOutBox::launchAsynchronously(std::move(box), s->getScreenBounds(), nullptr);
    }
};

} // namespace ghosty
