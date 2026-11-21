/*
    Library - the samples and MIDI files in the folders the musician chose,
    as the browser's Sounds tab lists them (decision 0094).

    The Index is what the scanner learned about each file (its length, its
    tempo, a waveform or its notes), kept between runs so a file is only
    read again when it changes. The Settings are the musician's: the folders,
    the favourites, and how the list is shown (in folders or one list, A to
    Z or shuffled). `rows` turns the two into what the list shows.
    No JUCE in here: the scanner and the window are the App's.
*/

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kal::drumsound { class Cache; }

namespace kal::library
{

enum class Kind { Audio, Midi };

struct Note
{
    float start = 0, length = 0;   // in beats from the file's start
    int pitch = 60;
    bool operator== (const Note&) const = default;
};

inline constexpr double silentDb = -100.0;

struct Item
{
    std::string path;              // absolute
    Kind kind = Kind::Audio;
    std::int64_t size = 0;         // bytes, and when it was last changed (ms since 1970):
    std::int64_t modified = 0;     // the file is read again when either differs
    bool readable = true;          // false: it couldn't be read; left out of the list

    // Audio
    double seconds = 0, sampleRate = 0;
    int channels = 0;
    std::vector<std::uint8_t> peaks;   // the waveform: the loudest of each column, 0-255

    // Both: the tempo it was made at, 0 when unknown (from its name, its
    // ACID chunk, or a MIDI file's first tempo; a sample's, failing those,
    // heard in its hits).
    double bpm = 0;
    enum class TempoFrom { Unknown, Name, File, Heard };
    TempoFrom bpmFrom = TempoFrom::Unknown;
    bool loop = false;     // heard: its length a whole number of bars at that tempo
    // Sure enough to stretch it to the song: written down, or heard in a loop.
    bool tempoTrusted() const { return bpm > 0 && (bpmFrom != TempoFrom::Heard || loop); }

    // Audio, as heard by the scan (0095): how loud (integrated LUFS, and the
    // loudest sample in dBFS; `silent` when there's no reading), and the note
    // it plays (MIDI number, -1 when it has no one pitch: drums, chords, noise).
    double lufs = silentDb, peakDb = silentDb;
    int note = -1;
    double cents = 0;
    // Its shape as a hit (0106; audiotools::hitShape): when it starts, how fast it
    // rises, how far it dies away, how bright it is; -1 when not heard.
    double onsetMs = -1, attackMs = -1, tailDb = 0, zcr = -1;
    // What the scan that read it knew how to hear: older entries are read again.
    int analysis = 0;

    // MIDI
    double beats = 0;                  // its length in beats
    std::vector<Note> notes;

    bool operator== (const Item&) const = default;
};

// The scan's version: an item read by an older scan is read again (0095).
// Version 3 (0106) adds the hit's shape; a version 2 entry has only that
// added, from its first seconds, not the whole of it heard again.
inline constexpr int analysisVersion = 3;

// Columns in an Item's waveform.
inline constexpr int peakColumns = 160;

struct Index
{
    std::map<std::string, Item> items;   // by path
};

struct Settings
{
    std::vector<std::string> folders;    // the folders scanned, as chosen
    bool asked = false;                  // the first-run question has been asked
    std::set<std::string> favourites;    // files and folders
    bool flat = false;                   // every item in one list, no folders
    bool favouritesOnly = false;
    bool shuffled = false;
    std::uint32_t seed = 1;              // the shuffle: a new one each time it's asked for
    bool sync = true;                    // previews follow the song's tempo
    bool autoPlay = true;                // choosing a sound plays it (off: click its picture to hear it)
    std::set<std::string> open;          // folders shown open
    bool drumsOnly = false;              // only drum sounds (0106)
    int drumKind = -1;                   // and only this kind (a drumsound::Kind), -1 every kind
    bool operator== (const Settings&) const = default;
};

// What the list shows, top to bottom.
struct Row
{
    enum class Type { Folder, Item } type = Type::Item;
    std::string path;
    int depth = 0;
    bool open = false;     // a folder's
    int count = 0;         // a folder's: how many items it holds, every level down
};

// Extensions (lower case, with the dot) Kalibre can read: audio and MIDI.
bool isMidiExtension (const std::string& ext);
std::string extensionOf (const std::string& path);       // ".wav", lower case
std::string nameOf (const std::string& path);            // "Kick 01.wav"
std::string parentOf (const std::string& path);          // without the last '/'
bool isInside (const std::string& path, const std::string& folder);

// A tempo written in a file's name: "Drums 96bpm", "loop_120_BPM", "Pad 87.5 bpm".
// 0 when there's none, or it's outside 40-300.
double tempoFromName (const std::string& name);

// True when the cached item doesn't describe the file as it is now.
bool needsScan (const Item* cached, std::int64_t size, std::int64_t modified);

// "C3 +12", the note and how far off it is; "" when there's none.
std::string noteName (int note, double cents);
// What the browser shows of a sample's loudness: "-12.3 LUFS", or its peak when too short to measure.
std::string loudnessText (const Item&);

// A favourite itself, or inside a favourite folder.
bool isFavourite (const Settings&, const std::string& path);

// The rows to show. `filter` keeps items whose name holds it (any case);
// a filter or Favourites Only opens every folder that has something to show.
// With Drums on (0106), only drums: `drums` keeps their verdicts between calls.
std::vector<Row> rows (const Index&, const Settings&, const std::string& filter = {}, drumsound::Cache* drums = nullptr);

// The items alone, in the order `rows` shows them (for Up and Down, and tests).
std::vector<std::string> itemsInOrder (const std::vector<Row>&);

// A file's waveform from its samples' loudest values (0-1), as `peakColumns` columns.
std::vector<std::uint8_t> peaksFrom (const std::vector<float>& loudest);

// Saved as JSON.
std::string toJson (const Settings&);
bool fromJson (const std::string&, Settings&);
std::string toJson (const Index&);
bool fromJson (const std::string&, Index&);

} // namespace kal::library
