#include "Keys.h"

namespace kal
{
using namespace juce;

void Keys::add (const String& id, const String& name, KeyPress defaultKey, std::function<void()> run)
{
    commands.push_back ({ id, name, defaultKey, defaultKey, std::move (run) });
}

bool Keys::handle (const KeyPress& k)
{
    for (auto& c : commands)
        if (c.key.isValid() && c.key == k && c.run) { c.run(); return true; }
    return false;
}

KeyPress Keys::keyFor (const String& id) const
{
    for (auto& c : commands) if (c.id == id) return c.key;
    return {};
}

String Keys::textFor (const String& id) const
{
    const auto k = keyFor (id);
    if (! k.isValid()) return {};
    String s;
    const auto m = k.getModifiers();
   #if JUCE_MAC
    if (m.isCtrlDown()) s << String::fromUTF8 ("\xe2\x8c\x83");
    if (m.isAltDown()) s << String::fromUTF8 ("\xe2\x8c\xa5");
    if (m.isShiftDown()) s << String::fromUTF8 ("\xe2\x87\xa7");
    if (m.isCommandDown()) s << String::fromUTF8 ("\xe2\x8c\x98");
   #else
    if (m.isCommandDown()) s << "Ctrl+";
    if (m.isAltDown()) s << "Alt+";
    if (m.isShiftDown()) s << "Shift+";
   #endif
    const int code = k.getKeyCode();
    if (code == KeyPress::spaceKey) return s + "Space";
   #if JUCE_MAC
    if (code == KeyPress::returnKey) return s + String::fromUTF8 ("\xe2\x86\xa9");
    if (code == KeyPress::backspaceKey) return s + String::fromUTF8 ("\xe2\x8c\xab");
    // As the Mac's menus name them (decision 0133): forward delete, Home, End.
    if (code == KeyPress::deleteKey) return s + String::fromUTF8 ("\xe2\x8c\xa6");
    if (code == KeyPress::homeKey) return s + String::fromUTF8 ("\xe2\x86\x96");
    if (code == KeyPress::endKey) return s + String::fromUTF8 ("\xe2\x86\x98");
   #else
    // Windows names its keys in words (decision 0129).
    if (code == KeyPress::returnKey) return s + "Enter";
    if (code == KeyPress::backspaceKey) return s + "Backspace";
    if (code == KeyPress::deleteKey) return s + "Delete";
    if (code == KeyPress::homeKey) return s + "Home";
    if (code == KeyPress::endKey) return s + "End";
   #endif
    if (code == KeyPress::escapeKey) return s + "Esc";
    const auto desc = KeyPress (code).getTextDescription();
    return s + (desc.length() == 1 ? desc.toUpperCase() : desc.substring (0, 1).toUpperCase() + desc.substring (1));
}

void Keys::set (const String& id, const KeyPress& k)
{
    for (auto& c : commands)
        if (c.id != id && c.key == k) c.key = {};   // a key does one thing
    for (auto& c : commands)
        if (c.id == id) c.key = k;
    save();
    if (onChange) onChange();
}

void Keys::reset (const String& id)
{
    for (auto& c : commands) if (c.id == id) set (id, c.defaultKey);
}

void Keys::resetAll()
{
    for (auto& c : commands) c.key = c.defaultKey;
    save();
    if (onChange) onChange();
}

void Keys::load()
{
    if (! file.existsAsFile()) return;
    const auto v = JSON::parse (file.loadFileAsString());
    if (auto* o = v.getDynamicObject())
        for (auto& c : commands)
            if (o->hasProperty (c.id))
            {
                const auto text = o->getProperty (c.id).toString();
                c.key = text.isEmpty() ? KeyPress() : KeyPress::createFromDescription (text);
            }
}

void Keys::save() const
{
    if (file == File()) return;
    DynamicObject::Ptr o = new DynamicObject();
    // Only what differs from the defaults is kept.
    for (auto& c : commands)
        if (! (c.key == c.defaultKey)) o->setProperty (c.id, c.key.isValid() ? c.key.getTextDescription() : String());
    file.getParentDirectory().createDirectory();
    file.replaceWithText (JSON::toString (var (o.get())));
}

} // namespace kal
