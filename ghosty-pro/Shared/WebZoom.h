#pragma once
// WebZoom (owner 2026-11-01, "like FabFilter"): the plugin window scales its page as one picture instead of re-flowing it.
// The page is laid out at a reference size (the size the window first opens at = the screen) and the WebView's page zoom
// (WKWebView.pageZoom, macOS 11+, the same as Cmd + / -) follows the window: smaller window = everything smaller, in place.
#include <juce_gui_extra/juce_gui_extra.h>
#if JUCE_MAC
 #include <objc/runtime.h>
 #include <objc/message.h>
#endif

namespace ghosty::webzoom {

#if JUCE_MAC
inline void* findNSView(juce::Component& c) {
    for (auto* ch : c.getChildren()) {
        if (auto* nv = dynamic_cast<juce::NSViewComponent*>(ch)) if (void* v = nv->getView()) return v;
        if (void* v = findNSView(*ch)) return v;
    }
    return nullptr;
}
// true when the zoom was applied (the WebView exists and knows pageZoom)
inline bool apply(juce::WebBrowserComponent& web, double z) {
    void* v = findNSView(web); if (v == nullptr) return false;
    id obj = (id) v; SEL sel = sel_registerName("setPageZoom:");
    if (! class_respondsToSelector(object_getClass(obj), sel)) return false;
    ((void (*)(id, SEL, double)) objc_msgSend)(obj, sel, z);
    return true;
}
#else
inline bool apply(juce::WebBrowserComponent&, double) { return false; }
#endif

// the zoom for a window of w x h when the page was designed at refW x refH: whatever keeps the whole layout in view
inline double zoomFor(int w, int h, int refW, int refH) {
    if (refW <= 0 || refH <= 0) return 1.0;
    return juce::jlimit(0.45, 2.0, juce::jmin((double) w / refW, (double) h / refH));
}

} // namespace ghosty::webzoom
