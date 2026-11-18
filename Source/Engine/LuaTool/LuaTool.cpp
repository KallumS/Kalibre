#include "LuaTool.h"
#include "ToolProcessor.h"
#include "ToolSession.h"
#include "GfxTool.h"
#include "HostedProcessor.h"
#include "Theme.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>

namespace kal
{
using namespace juce;

std::function<bool (AudioPluginInstance&, const ToolBlock&)> insertToolBlock;

const std::vector<LuaToolSpec>& luaTools()
{
    static const std::vector<LuaToolSpec> list {
        { "starting-blocks", "Starting Blocks", "Starting Blocks.lua",
          "The smallest useful pieces of music, played into the song" },
        { "starting-blocks-notation", "Starting Blocks Notation", "Starting Blocks Notation.lua",
          "Starting Blocks, shown as notation" },
        { "midi-catalogue", "Midi Catalogue", "Midi Catalogue.lua",
          "A catalogue of parts for every instrument, from the choices made" },
        { "orchestration-helper", "Orchestration Helper", "Orchestration Helper.lua",
          "Doublings, pairings and combinations, each with its source", true },
    };
    return list;
}

PluginRef luaToolRef (const LuaToolSpec& s)
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = std::string ("tool:") + s.id;
    r.name = s.name;
    r.manufacturer = "Kalibre";
    return r;
}

std::unique_ptr<AudioPluginInstance> makeLuaTool (const PluginRef& ref)
{
    for (const auto& s : luaTools())
    {
        if (ref.identifier != luaToolRef (s).identifier) continue;
        PluginDescription d;
        d.name = d.descriptiveName = s.name;
        d.pluginFormatName = "Kalibre";
        d.manufacturerName = "Kalibre";
        d.fileOrIdentifier = ref.identifier;
        d.category = "MIDI Effect";
        d.numInputChannels = d.numOutputChannels = 2;
        if (s.gfx) return std::make_unique<HostedProcessor> (std::make_unique<tools::GfxProcessor> (s), d);
        auto tool = std::make_unique<tools::ToolProcessor> (s);
        auto* inner = tool.get();
        auto hosted = std::make_unique<HostedProcessor> (std::move (tool), d);
        inner->owner = hosted.get();
        return hosted;
    }
    return nullptr;
}

template <typename P>
static P* toolOf (AudioPluginInstance& instance)
{
    auto* hosted = dynamic_cast<HostedProcessor*> (&instance);
    return hosted ? dynamic_cast<P*> (&hosted->inner()) : nullptr;
}

String luaToolError (AudioPluginInstance& instance)
{
    if (auto* t = toolOf<tools::ToolProcessor> (instance)) return t->session() ? t->session()->error() : String ("no session");
    if (auto* t = toolOf<tools::GfxProcessor> (instance)) return t->session() ? t->session()->error() : String ("no session");
    return "not a tool";
}

String luaToolRun (AudioPluginInstance& instance, const String& lua)
{
    if (auto* t = toolOf<tools::ToolProcessor> (instance); t && t->session()) return t->session()->run (lua);
    if (auto* t = toolOf<tools::GfxProcessor> (instance); t && t->session()) return t->session()->run (lua);
    return "not a tool";
}

namespace tools
{

ToolProcessor::ToolProcessor (const LuaToolSpec& s)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      spec (s)
{
    out.ensureSize (8192);
    // The blocks the audio thread let go of, let go of here.
    struct Collector : Timer
    {
        explicit Collector (Swap<PlayBlock>& b) : swap (b) { startTimerHz (4); }
        void timerCallback() override { swap.collect(); }
        Swap<PlayBlock>& swap;
    };
    collector = std::make_unique<Collector> (block);
    restartSession();
}

ToolProcessor::~ToolProcessor()
{
    collector.reset();
    current.reset();
}

void ToolProcessor::restartSession()
{
    current.reset();                      // runs the old script's atexit (saves its settings)
    current = std::make_unique<ToolSession> (*this);
}

// ---- the block ---------------------------------------------------------------------

void ToolProcessor::setBlock (std::vector<BlockNote> notes, double beats)
{
    std::sort (notes.begin(), notes.end(), [] (const BlockNote& a, const BlockNote& b) { return a.start < b.start; });
    auto next = std::make_unique<PlayBlock>();
    next->notes = std::move (notes);
    next->beats = beats;
    next->version = ++blockVersion;
    block.post (std::move (next));
    auditionBeats = beats;
}

void ToolProcessor::audition (bool on)
{
    auditionRequest = on ? 1 : 2;
    auditionOn = on;
    auditionShown = 0;
}

double ToolProcessor::auditionFraction() const
{
    const double b = auditionBeats.load();
    return b > 0 ? jlimit (0.0, 1.0, auditionShown.load() / b) : 0.0;
}

std::map<String, String> ToolProcessor::extState()
{
    const ScopedLock sl (extLock);
    return ext;
}

void ToolProcessor::setExt (const String& key, const String& value)
{
    const ScopedLock sl (extLock);
    ext[key] = value;
}

// ---- playing it --------------------------------------------------------------------

static int sampleAt (double beat, double from, double spb, int sample0, int numSamples)
{
    return jlimit (0, jmax (0, numSamples - 1), sample0 + (int) std::floor ((beat - from) * spb));
}

// Lets go of every voice due before `upTo`.
void ToolProcessor::release (Voices& vs, double upTo, double from, double spb, int sample0, MidiBuffer& o)
{
    for (int i = 0; i < vs.n;)
    {
        if (vs.v[(size_t) i].off < upTo)
        {
            const int s = sample0 + jmax (0, (int) std::floor ((vs.v[(size_t) i].off - from) * spb));
            o.addEvent (MidiMessage::noteOff (1, vs.v[(size_t) i].pitch), s);
            vs.v[(size_t) i] = vs.v[(size_t) (--vs.n)];
        }
        else ++i;
    }
}

void ToolProcessor::releaseAll (Voices& vs, int sample, MidiBuffer& o)
{
    for (int i = 0; i < vs.n; ++i) o.addEvent (MidiMessage::noteOff (1, vs.v[(size_t) i].pitch), sample);
    vs.n = 0;
}

// The notes that start in [from, to), in quarter notes. With `repeat` the
// block repeats every beats from 0 (the song's start), lining up with its
// bars; without, it is played once from 0.
void ToolProcessor::play (Voices& vs, const PlayBlock& b, double from, double to, double spb, int sample0, int numSamples,
                          bool repeat, MidiBuffer& o)
{
    release (vs, from + 1e-9, from, spb, sample0, o);   // anything already due
    const double len = b.beats;
    if (len > 0 && ! b.notes.empty())
    {
        const double k0 = repeat ? std::floor (from / len) : 0;
        const double k1 = repeat ? std::floor (to / len) : 0;
        for (double k = k0; k <= k1; k += 1)
        {
            for (const auto& n : b.notes)
            {
                const double t = k * len + n.start;
                if (t < from) continue;
                if (t >= to) break;
                const int s = sampleAt (t, from, spb, sample0, numSamples);
                for (int i = 0; i < vs.n; ++i)   // the same pitch still sounding is let go first
                    if (vs.v[(size_t) i].pitch == n.pitch)
                    {
                        o.addEvent (MidiMessage::noteOff (1, n.pitch), s);
                        vs.v[(size_t) i] = vs.v[(size_t) (--vs.n)];
                        break;
                    }
                o.addEvent (MidiMessage::noteOn (1, n.pitch, (uint8) n.vel), s);
                if (vs.n < (int) vs.v.size()) vs.v[(size_t) (vs.n++)] = { n.pitch, t + jmax (0.001, n.len) };
            }
        }
    }
    for (int i = 0; i < vs.n;)   // notes shorter than this buffer end inside it
    {
        if (vs.v[(size_t) i].off < to)
        {
            const int s = sampleAt (vs.v[(size_t) i].off, from, spb, sample0, numSamples);
            o.addEvent (MidiMessage::noteOff (1, vs.v[(size_t) i].pitch), s);
            vs.v[(size_t) i] = vs.v[(size_t) (--vs.n)];
        }
        else ++i;
    }
}

void ToolProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    out.clear();
    out.addEvents (midi, 0, n, 0);   // what comes in still goes on

    double bpm = 120, ppq = 0;
    bool playing = false, hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = jmax (1.0, *b);
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
            if (auto sig = pos->getTimeSignature())
            {
                songNum = sig->numerator > 0 ? sig->numerator : 4;
                songDen = sig->denominator > 0 ? sig->denominator : 4;
            }
        }
    songTempo = bpm;
    if (hasPpq) songPosition = ppq;
    const double spb = sampleRate * 60.0 / bpm;   // samples per quarter note
    const double span = n / spb;

    const PlayBlock* b = block.take();

    // In time with the song
    if (withSong && playing && hasPpq && b != nullptr)
    {
        const bool jumped = std::abs (ppq - lastSongEnd) > 1e-3;
        if (jumped || b->version != songVersion) releaseAll (songVoices, 0, out);
        songVersion = b->version;
        play (songVoices, *b, ppq, ppq + span, spb, 0, n, true, out);
        lastSongEnd = ppq + span;
    }
    else if (songVoices.n > 0)
    {
        releaseAll (songVoices, 0, out);
        lastSongEnd = -1;
    }

    // Audition: the block once (or round and round) from the top, at the tempo
    if (auditionRequest.exchange (0) != 0)
    {
        releaseAll (auditionVoices, 0, out);
        auditionAt = 0;
    }
    if (auditionOn && b != nullptr)
    {
        const double len = b->beats;
        const double from = auditionAt, to = from + span;
        if (len <= 0) auditionOn = false;
        else if (to < len)
        {
            play (auditionVoices, *b, from, to, spb, 0, n, false, out);
            auditionAt = to;
        }
        else
        {
            const int split = jlimit (0, n, (int) std::floor ((len - from) * spb));
            play (auditionVoices, *b, from, len, spb, 0, n, false, out);
            releaseAll (auditionVoices, split, out);
            if (loop)
            {
                auditionAt = to - len;
                play (auditionVoices, *b, 0, auditionAt, spb, split, n, false, out);
            }
            else
            {
                auditionOn = false;
                auditionAt = 0;
            }
        }
        auditionShown = auditionAt;
    }
    else if (! auditionOn && auditionVoices.n > 0) releaseAll (auditionVoices, 0, out);

    midi.swapWith (out);   // the audio itself passes through untouched
}

// ---- the project ---------------------------------------------------------------------

void ToolProcessor::getStateInformation (MemoryBlock& dest)
{
    ValueTree root ("KalibreTool");
    root.setProperty ("tool", spec.id, nullptr);
    root.setProperty ("editorWidth", editorWidth, nullptr);
    root.setProperty ("editorHeight", editorHeight, nullptr);
    ValueTree e ("ext");
    for (auto& kv : extState())
    {
        ValueTree item ("item");
        item.setProperty ("key", kv.first, nullptr);
        item.setProperty ("value", kv.second, nullptr);
        e.addChild (item, -1, nullptr);
    }
    root.addChild (e, -1, nullptr);
    MemoryOutputStream stream (dest, false);
    root.writeToStream (stream);
}

void ToolProcessor::setStateInformation (const void* data, int size)
{
    const auto root = ValueTree::readFromData (data, (size_t) size);
    if (! root.hasType ("KalibreTool")) return;
    editorWidth = root.getProperty ("editorWidth", 0);
    editorHeight = root.getProperty ("editorHeight", 0);
    std::map<String, String> keep;
    for (auto item : root.getChildWithName ("ext"))
        keep[item.getProperty ("key").toString()] = item.getProperty ("value").toString();
    withSong = keep["kalibre/withSong"] == "1";
    // The script reads its settings when it starts, so it starts again.
    auto restart = [this, keep] {
        current.reset();               // its atexit saves the old settings...
        {
            const ScopedLock sl (extLock);
            ext = keep;                // ...which the loaded ones replace
        }
        current = std::make_unique<ToolSession> (*this);
    };
    if (MessageManager::getInstance()->isThisTheMessageThread()) restart();
    else MessageManager::callAsync (restart);
}

// Draws ImGui's triangles with JUCE. ImGui's own smoothing is off, so every
// shape is flat-coloured triangles (drawn as one path per run of a colour, so
// JUCE smooths the outline and no seams show between them) and text is
// quads cut from the font atlas (drawn as the atlas's alpha in their colour).
void drawImGui(juce::Graphics &g, ImDrawData *dd, const juce::Image &atlas)
{
    if (!dd || !atlas.isValid()) return;
    const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    const float aw = (float)atlas.getWidth(), ah = (float)atlas.getHeight();
    auto colour = [](ImU32 c) {
        return juce::Colour((juce::uint8)(c & 255), (juce::uint8)((c >> 8) & 255),
                            (juce::uint8)((c >> 16) & 255), (juce::uint8)((c >> 24) & 255));
    };
    for (int l = 0; l < dd->CmdListsCount; ++l) {
        const ImDrawList *list = dd->CmdLists[l];
        const ImDrawVert *vtx = list->VtxBuffer.Data;
        const ImDrawIdx *idx = list->IdxBuffer.Data;
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(juce::Rectangle<float>(cmd.ClipRect.x, cmd.ClipRect.y,
                                                      cmd.ClipRect.z - cmd.ClipRect.x,
                                                      cmd.ClipRect.w - cmd.ClipRect.y)
                                   .getSmallestIntegerContainer());
            juce::Path path;
            ImU32 pathCol = 0;
            auto flush = [&] {
                if (!path.isEmpty()) {
                    g.setColour(colour(pathCol));
                    g.fillPath(path);
                    path.clear();
                }
            };
            const unsigned end = cmd.IdxOffset + cmd.ElemCount;
            for (unsigned i = cmd.IdxOffset; i + 3 <= end;) {
                const ImDrawVert &a = vtx[cmd.VtxOffset + idx[i]];
                const ImDrawVert &b = vtx[cmd.VtxOffset + idx[i + 1]];
                const ImDrawVert &c = vtx[cmd.VtxOffset + idx[i + 2]];
                bool solid = a.uv.x == white.x && a.uv.y == white.y && b.uv.x == white.x &&
                             b.uv.y == white.y && c.uv.x == white.x && c.uv.y == white.y;
                if (solid) {
                    if (a.col != pathCol) { flush(); pathCol = a.col; }
                    path.addTriangle(a.pos.x, a.pos.y, b.pos.x, b.pos.y, c.pos.x, c.pos.y);
                    i += 3;
                    continue;
                }
                // A glyph: ImGui draws it as two triangles, top-left, top-right,
                // bottom-right first, so the first one spans the whole rectangle.
                flush();
                i += 6;
                float x0 = std::min({a.pos.x, b.pos.x, c.pos.x}), x1 = std::max({a.pos.x, b.pos.x, c.pos.x});
                float y0 = std::min({a.pos.y, b.pos.y, c.pos.y}), y1 = std::max({a.pos.y, b.pos.y, c.pos.y});
                float u0 = std::min({a.uv.x, b.uv.x, c.uv.x}), u1 = std::max({a.uv.x, b.uv.x, c.uv.x});
                float v0 = std::min({a.uv.y, b.uv.y, c.uv.y}), v1 = std::max({a.uv.y, b.uv.y, c.uv.y});
                juce::Rectangle<int> src((int)std::lround(u0 * aw), (int)std::lround(v0 * ah),
                                         (int)std::lround((u1 - u0) * aw), (int)std::lround((v1 - v0) * ah));
                if (src.isEmpty() || x1 <= x0 || y1 <= y0) continue;
                g.setColour(colour(a.col));
                g.drawImageTransformed(atlas.getClippedImage(src),
                                       juce::AffineTransform::scale((x1 - x0) / (float)src.getWidth(),
                                                                    (y1 - y0) / (float)src.getHeight())
                                           .translated(x0, y0),
                                       true);
            }
            flush();
        }
    }
}

// ImGui's key for a JUCE key code, for the keys a window needs.
static ImGuiKey imguiKey(const juce::KeyPress &k)
{
    const int c = k.getKeyCode();
    if (c == juce::KeyPress::tabKey) return ImGuiKey_Tab;
    if (c == juce::KeyPress::returnKey) return ImGuiKey_Enter;
    if (c == juce::KeyPress::escapeKey) return ImGuiKey_Escape;
    if (c == juce::KeyPress::spaceKey) return ImGuiKey_Space;
    if (c == juce::KeyPress::backspaceKey) return ImGuiKey_Backspace;
    if (c == juce::KeyPress::deleteKey) return ImGuiKey_Delete;
    if (c == juce::KeyPress::leftKey) return ImGuiKey_LeftArrow;
    if (c == juce::KeyPress::rightKey) return ImGuiKey_RightArrow;
    if (c == juce::KeyPress::upKey) return ImGuiKey_UpArrow;
    if (c == juce::KeyPress::downKey) return ImGuiKey_DownArrow;
    if (c == juce::KeyPress::homeKey) return ImGuiKey_Home;
    if (c == juce::KeyPress::endKey) return ImGuiKey_End;
    if (c == juce::KeyPress::pageUpKey) return ImGuiKey_PageUp;
    if (c == juce::KeyPress::pageDownKey) return ImGuiKey_PageDown;
    if (c == juce::KeyPress::numberPadAdd) return ImGuiKey_KeypadAdd;
    if (c == juce::KeyPress::numberPadSubtract) return ImGuiKey_KeypadSubtract;
    if (c >= '0' && c <= '9') return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (ImGuiKey)(ImGuiKey_A + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (ImGuiKey)(ImGuiKey_A + (c - 'a'));
    if (c == '=' || c == '+') return ImGuiKey_Equal;
    if (c == '-' || c == '_') return ImGuiKey_Minus;
    return ImGuiKey_None;
}

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(ToolProcessor &p) : juce::AudioProcessorEditor(p), m_proc(p)
    {
        // read the remembered size first: setting the limits resizes the
        // editor, and resized() records whatever size that is
        int w = p.editorWidth > 0 ? p.editorWidth : 860;
        int h = p.editorHeight > 0 ? p.editorHeight : 700;
        setWantsKeyboardFocus(true);
        setResizable(true, true);
        ToolSession *s = p.session();
        int minW = s && s->minWidth() > 0 ? (int)s->minWidth() : 320;
        int minH = s && s->minHeight() > 0 ? (int)s->minHeight() : 240;
        setResizeLimits(juce::jmax(320, minW), juce::jmax(240, minH), 4096, 4096);
        setSize(w, h);
        startTimerHz(30);
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(theme::ground);
        if (ToolSession *s = m_proc.session()) drawImGui(g, s->drawData(), s->atlas());
    }

    void resized() override
    {
        m_proc.editorWidth = getWidth();
        m_proc.editorHeight = getHeight();
    }

    // ---- input, handed to the script's ImGui ----
    void mouseMove(const juce::MouseEvent &e) override { pos(e); }
    void mouseEnter(const juce::MouseEvent &e) override { pos(e); }
    void mouseExit(const juce::MouseEvent &) override
    {
        if (ToolSession *s = m_proc.session()) s->mouseMove(-FLT_MAX, -FLT_MAX);
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        grabKeyboardFocus();
        pos(e);
        if (ToolSession *s = m_proc.session()) s->mouseButton(button(e), true);
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        pos(e);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        pos(e);
        if (ToolSession *s = m_proc.session()) s->mouseButton(button(e), false);
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    {
        pos(e);
        if (ToolSession *s = m_proc.session()) s->mouseWheel(w.deltaX * 5, w.deltaY * 5);
    }
    void modifierKeysChanged(const juce::ModifierKeys &m) override
    {
        if (ToolSession *s = m_proc.session()) s->modifiers(m);
    }
    bool keyPressed(const juce::KeyPress &k) override
    {
        ToolSession *s = m_proc.session();
        if (!s) return false;
        s->modifiers(k.getModifiers());
        ImGuiKey key = imguiKey(k);
        if (key != ImGuiKey_None) {
            s->key(key, true);
            s->key(key, false);
        }
        juce::juce_wchar c = k.getTextCharacter();
        if (c >= 32 && !k.getModifiers().isCommandDown() && !k.getModifiers().isCtrlDown())
            s->character(c);
        // Space with nothing focused is left to the host (play/stop)
        if (key == ImGuiKey_Space && !s->wantsKeyboard()) return false;
        return true;
    }
    void focusGained(FocusChangeType) override { if (ToolSession *s = m_proc.session()) s->focus(true); }
    void focusLost(FocusChangeType) override { if (ToolSession *s = m_proc.session()) s->focus(false); }

private:
    void timerCallback() override
    {
        if (ToolSession *s = m_proc.session()) {
            float scale = 1;
            if (auto *peer = getPeer()) scale = (float)peer->getPlatformScaleFactor();
            if (auto *d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
                scale = juce::jmax(scale, (float)d->scale);
            s->frame((float)getWidth(), (float)getHeight(), scale);
            repaint();
        }
    }
    void pos(const juce::MouseEvent &e)
    {
        if (ToolSession *s = m_proc.session()) {
            s->modifiers(e.mods);
            s->mouseMove(e.position.x, e.position.y);
        }
    }
    static int button(const juce::MouseEvent &e)
    {
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) return 1;
        if (e.mods.isMiddleButtonDown()) return 2;
        return 0;
    }

    ToolProcessor &m_proc;
};

AudioProcessorEditor* ToolProcessor::createEditor() { return new Editor (*this); }

} // namespace tools
} // namespace kal
