#pragma once
// Closing a plug-in window while its page is still loading must never crash the DAW.
// JUCE 8.0.4's Mac WebBrowserComponent leaves its WKWebView's "__JUCE__" message handler and juce:// resource handler
// pointing at C++ objects that die with the window; WebKit can still call them a moment later (a page still loading, a
// timer posting a message) and the DAW goes down (std::bad_function_call, seen in the Ghost House host test 2026-11-07).
// detach() is called from the editor's destructor: it stops the page loading and unhooks the message handler first.
#include <juce_gui_extra/juce_gui_extra.h>
#if JUCE_MAC
 #include <objc/runtime.h>
 #include <objc/message.h>
#endif

namespace ghosty::webview {

#if JUCE_MAC
inline void* findWKWebView(juce::Component& c) {
    for (auto* ch : c.getChildren()) {
        if (auto* nv = dynamic_cast<juce::NSViewComponent*>(ch)) if (void* v = nv->getView()) return v;
        if (void* v = findWKWebView(*ch)) return v;
    }
    return nullptr;
}
inline void detach(juce::WebBrowserComponent& web) {
    id wv = (id) findWKWebView(web); if (wv == nil) return;
    auto responds = [](id o, const char* s) { return o != nil && class_respondsToSelector(object_getClass(o), sel_registerName(s)); };
    if (responds(wv, "stopLoading")) ((void (*)(id, SEL)) objc_msgSend)(wv, sel_registerName("stopLoading"));
    if (! responds(wv, "configuration")) return;
    id cfg = ((id (*)(id, SEL)) objc_msgSend)(wv, sel_registerName("configuration"));
    id ucc = responds(cfg, "userContentController") ? ((id (*)(id, SEL)) objc_msgSend)(cfg, sel_registerName("userContentController")) : nil;
    if (ucc == nil) return;
    id name = ((id (*)(id, SEL, const char*)) objc_msgSend)((id) objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "__JUCE__");
    if (responds(ucc, "removeScriptMessageHandlerForName:")) ((void (*)(id, SEL, id)) objc_msgSend)(ucc, sel_registerName("removeScriptMessageHandlerForName:"), name);
    if (responds(ucc, "removeAllUserScripts")) ((void (*)(id, SEL)) objc_msgSend)(ucc, sel_registerName("removeAllUserScripts"));
}
#else
inline void detach(juce::WebBrowserComponent&) {}
#endif

} // namespace ghosty::webview
