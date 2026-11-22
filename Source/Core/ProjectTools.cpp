#include "ProjectTools.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace kal::projecttools
{

namespace
{
template <typename F> void forEachFile (Project& p, F f)
{
    auto clipFiles = [&] (Clip& c)
    {
        if (c.isMidi()) return;
        f (c.audio.file);
        f (c.audio.renderedFile);
    };
    for (auto& t : p.tracks)
    {
        for (auto& v : t.versions)
        {
            for (auto& c : v.clips) clipFiles (c);
            for (auto& tk : v.takes) for (auto& c : tk.clips) clipFiles (c);
        }
        f (t.frozenFile);
        for (auto& m : t.modulators) f (m.tableFile);
    }
    f (p.master.frozenFile);
    f (p.video.file);
}

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    return s;
}

std::string escape (const std::string& s)
{
    std::string out;
    for (char ch : s)
        switch (ch)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += ch;
        }
    return out;
}

std::string pathUrl (const std::string& path)
{
    std::string out = "file://";
    if (path.empty() || (path[0] != '/' && path[0] != '\\')) out += "/";   // C:\\... is file:///C:/...
    for (unsigned char ch : path)
    {
        if (ch == '\\') { out += '/'; continue; }
        if (std::isalnum (ch) || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~') out += (char) ch;
        else { char buf[4]; std::snprintf (buf, sizeof buf, "%%%02X", ch); out += buf; }
    }
    return out;
}
} // namespace

std::vector<std::string> referencedFiles (const Project& project)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto& p = const_cast<Project&> (project);
    forEachFile (p, [&] (std::string& f)
    {
        if (! f.empty() && seen.insert (f).second) out.push_back (f);
    });
    return out;
}

void replaceFiles (Project& p, const std::map<std::string, std::string>& map)
{
    forEachFile (p, [&] (std::string& f)
    {
        if (auto it = map.find (f); it != map.end()) f = it->second;
    });
}

std::string fileNameOf (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

std::map<std::string, std::string> relinkByName (const std::vector<std::string>& missing, const std::vector<std::string>& candidates)
{
    std::map<std::string, std::string> byName;
    for (auto& c : candidates) byName.emplace (lower (fileNameOf (c)), c);   // the first found wins
    std::map<std::string, std::string> out;
    for (auto& m : missing)
        if (auto it = byName.find (lower (fileNameOf (m))); it != byName.end()) out[m] = it->second;
    return out;
}

Project asTemplate (const Project& source, bool keepMusic)
{
    Project p = source;
    p.name = "Untitled";
    if (keepMusic) return p;
    for (auto& t : p.tracks)
    {
        TrackVersion v;
        v.id = t.versions.empty() ? 0 : t.version().id;
        v.name = "Version 1";
        t.versions = { v };
        t.activeVersion = 0;
        t.automation.clear();
        t.frozen = false;
        t.frozenFile.clear();
        t.armed = false;
    }
    p.master.automation.clear();
    p.markers.clear();
    p.sections.clear();
    p.video = {};
    p.metadata = {};
    p.loop.on = p.punch.on = false;
    return p;
}

//==============================================================================
std::vector<unsigned char> id3v2 (const Metadata& m)
{
    std::vector<unsigned char> frames;
    auto frame = [&frames] (const char* id, const std::string& text)
    {
        if (text.empty()) return;
        const std::size_t size = text.size() + 1;   // the encoding byte, then ISO-8859-1/UTF-8 text
        for (int i = 0; i < 4; ++i) frames.push_back ((unsigned char) id[i]);
        for (int shift : { 24, 16, 8, 0 }) frames.push_back ((unsigned char) ((size >> shift) & 0xFF));
        frames.push_back (0); frames.push_back (0);          // flags
        frames.push_back (3);                                // UTF-8 (read by everything that reads v2.3 in practice)
        frames.insert (frames.end(), text.begin(), text.end());
    };
    frame ("TIT2", m.title);
    frame ("TPE1", m.artist);
    frame ("TALB", m.album);
    frame ("TYER", m.year > 0 ? std::to_string (m.year) : std::string());
    frame ("TCON", m.genre);
    frame ("TRCK", m.trackNumber > 0 ? std::to_string (m.trackNumber) : std::string());
    frame ("TSRC", m.isrc);
    if (! m.comment.empty())
    {
        // COMM: encoding, language, an empty description, then the text.
        const std::string body = std::string ("eng") + '\0' + m.comment;
        const std::size_t size = body.size() + 1;
        for (char ch : std::string ("COMM")) frames.push_back ((unsigned char) ch);
        for (int shift : { 24, 16, 8, 0 }) frames.push_back ((unsigned char) ((size >> shift) & 0xFF));
        frames.push_back (0); frames.push_back (0);
        frames.push_back (3);
        frames.insert (frames.end(), body.begin(), body.end());
    }
    if (frames.empty()) return {};
    std::vector<unsigned char> tag { 'I', 'D', '3', 3, 0, 0 };
    const std::size_t size = frames.size();
    // The header's size is "syncsafe": seven bits a byte.
    for (int shift : { 21, 14, 7, 0 }) tag.push_back ((unsigned char) ((size >> shift) & 0x7F));
    tag.insert (tag.end(), frames.begin(), frames.end());
    return tag;
}

//==============================================================================
std::string fcpXml (const Project& p, std::function<double (const std::string&)> fileSeconds, int fps)
{
    auto frames = [fps] (double seconds) { return (long long) std::llround (seconds * fps); };
    const std::string rate = "<rate><timebase>" + std::to_string (fps) + "</timebase><ntsc>FALSE</ntsc></rate>";
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE xmeml>\n<xmeml version=\"5\">\n";
    x << " <sequence id=\"sequence-1\">\n  <name>" << escape (p.metadata.title.empty() ? p.name : p.metadata.title) << "</name>\n";
    x << "  <duration>" << frames (p.tempo.secondsAt (p.endTick())) << "</duration>\n  " << rate << "\n";
    x << "  <media>\n   <audio>\n    <format><samplecharacteristics><depth>24</depth><samplerate>" << (long) p.sampleRate
      << "</samplerate></samplecharacteristics></format>\n";
    std::map<std::string, int> fileIds;
    int clipId = 0;
    for (auto& t : p.tracks)
    {
        if (! t.holdsClips()) continue;
        bool any = false;
        for (auto& c : t.clips()) if (! c.isMidi() && ! c.muted) any = true;
        if (! any) continue;
        x << "    <track>\n";
        for (auto& c : t.clips())
        {
            if (c.isMidi() || c.muted) continue;
            const auto& file = c.audio.renderedFile.empty() ? c.audio.file : c.audio.renderedFile;
            const double start = p.tempo.secondsAt (c.start), end = p.tempo.secondsAt (c.end());
            const double in = c.audio.sourceStart + (p.tempo.secondsAt (c.start) - p.tempo.secondsAt (c.start - c.offset));
            const double length = fileSeconds ? fileSeconds (file) : 0.0;
            x << "     <clipitem id=\"clipitem-" << ++clipId << "\">\n";
            x << "      <name>" << escape (c.name.empty() ? t.name : c.name) << "</name>\n";
            x << "      <enabled>TRUE</enabled>\n      <duration>" << frames (length) << "</duration>\n      " << rate << "\n";
            x << "      <start>" << frames (start) << "</start>\n      <end>" << frames (end) << "</end>\n";
            x << "      <in>" << frames (in) << "</in>\n      <out>" << frames (in + (end - start)) << "</out>\n";
            const bool known = fileIds.count (file) > 0;
            if (! known) fileIds[file] = (int) fileIds.size() + 1;
            x << "      <file id=\"file-" << fileIds[file] << "\"";
            if (known) x << "/>\n";
            else
            {
                x << ">\n       <name>" << escape (fileNameOf (file)) << "</name>\n       <pathurl>" << escape (pathUrl (file)) << "</pathurl>\n";
                x << "       " << rate << "\n       <duration>" << frames (length) << "</duration>\n";
                x << "       <media><audio><samplecharacteristics><depth>24</depth><samplerate>" << (long) c.audio.fileSampleRate
                  << "</samplerate></samplecharacteristics><channelcount>" << c.audio.fileChannels << "</channelcount></audio></media>\n";
                x << "      </file>\n";
            }
            x << "      <sourcetrack><mediatype>audio</mediatype><trackindex>1</trackindex></sourcetrack>\n";
            x << "     </clipitem>\n";
        }
        x << "    </track>\n";
    }
    x << "   </audio>\n  </media>\n </sequence>\n</xmeml>\n";
    return x.str();
}

} // namespace kal::projecttools
