/*
    The Controller's audio commands (decision 0036): normalise, strip
    silence, audio quantise and sample edits. The looking is done by
    Core/AudioTools; this reads and writes the files.
*/

#include "Controller.h"
#include "AudioFiles.h"

#include "AudioTools.h"
#include "Edit.h"

namespace kal
{
using namespace juce;

File Controller::fileOf (const Clip& c, bool rendered) const
{
    const auto& name = rendered && ! c.audio.renderedFile.empty() ? c.audio.renderedFile : c.audio.file;
    return File::isAbsolutePath (name) ? File (name) : projectFolder().getChildFile (name);
}

bool Controller::readAudio (const File& f, AudioBuffer<float>& out, double& rate)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->lengthInSamples <= 0 || r->lengthInSamples > (int64) 48000 * 60 * 60) return false;
    out.setSize ((int) jlimit (1u, 2u, r->numChannels), (int) r->lengthInSamples);
    r->read (&out, 0, (int) r->lengthInSamples, 0, true, out.getNumChannels() > 1);
    rate = r->sampleRate;
    return true;
}

bool Controller::editAudio (Id clipId, const String& label, int start, int end,
                            std::function<void (AudioBuffer<float>&, int, int)> op)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->isMidi()) return false;
    if (cl->locked) { say ("The clip is locked."); return false; }
    const auto src = fileOf (*cl, false);
    AudioBuffer<float> buf;
    double rate = 0;
    if (! readAudio (src, buf, rate)) { say ("The clip's audio couldn't be read.", true); return false; }
    start = jlimit (0, buf.getNumSamples(), start);
    end = jlimit (start, buf.getNumSamples(), end);
    if (end <= start) return false;
    op (buf, start, end);

    // "Vocal edit 3.wav" beside the recordings; the original stays where it is.
    const auto base = src.getFileNameWithoutExtension().upToLastOccurrenceOf (" edit", false, false);
    const auto out = recordingsFolder().getNonexistentChildFile (base + " edit", ".wav");
    auto writer = audiofile::floatWav (out, rate, buf.getNumChannels());   // decision 0079
    if (! writer || ! writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()))
    {
        say ("The edited audio couldn't be written.", true);
        return false;
    }
    writer.reset();
    const bool hadFx = ! cl->audio.renderedFile.empty();
    const auto path = out.getFullPathName().toStdString();
    apply (label, [clipId, path] (Project& p)
    {
        if (auto* x = p.clip (clipId)) { x->audio.file = path; x->audio.renderedFile.clear(); }
    });
    say (label + " done; the original file is kept." + (hadFx ? " Render the clip's offline effects again to hear them." : ""), false, true);
    return true;
}

void Controller::normaliseClip (Id clipId)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->isMidi()) return;
    AudioBuffer<float> buf;
    double rate = 0;
    if (! readAudio (fileOf (*cl), buf, rate)) { say ("The clip's audio couldn't be read.", true); return; }
    // Only the part of the file the clip plays.
    const int a = jlimit (0, buf.getNumSamples(), (int) (cl->audio.sourceStart * rate));
    const int b = cl->audio.sourceLength > 0 ? jlimit (a, buf.getNumSamples(), (int) ((cl->audio.sourceStart + cl->audio.sourceLength) * rate))
                                             : buf.getNumSamples();
    const float pk = audiotools::peak (buf.getArrayOfReadPointers(), buf.getNumChannels(), a, b);
    if (pk <= 0) { say ("The clip is silent."); return; }
    const double db = audiotools::normaliseGainDb (pk, -0.3);
    apply ("Normalise", [clipId, db] (Project& p) { if (auto* x = p.clip (clipId)) x->audio.gainDb = db; });
    say ("Normalised: the clip's gain is " + String (db, 1) + " dB, its loudest peak at -0.3 dBFS. The file isn't changed.", false, true);
}

void Controller::stripSilence (Id clipId, double thresholdDb)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->isMidi()) return;
    if (cl->loopLength > 0) { say ("Strip Silence works on clips that don't loop."); return; }
    AudioBuffer<float> buf;
    double rate = 0;
    if (! readAudio (fileOf (*cl), buf, rate)) { say ("The clip's audio couldn't be read.", true); return; }
    const auto spans = audiotools::sounding (buf.getArrayOfReadPointers(), buf.getNumChannels(), buf.getNumSamples(), rate, thresholdDb);
    std::vector<std::pair<double, double>> secs;
    for (auto s : spans) secs.push_back ({ s.start / rate, s.end / rate });
    size_t made = 0;
    apply ("Strip Silence", [clipId, secs, &made] (Project& p) { made = edit::keepSourceSpans (p, clipId, secs).size(); });
    say ("Strip Silence left " + String ((int) made) + (made == 1 ? " clip." : " clips."), false, true);
}

std::vector<double> Controller::hitsIn (Id clipId, double sensitivity)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->isMidi()) return {};
    AudioBuffer<float> buf;
    double rate = 0;
    if (! readAudio (fileOf (*cl, false), buf, rate)) return {};
    std::vector<double> out;
    for (int s : audiotools::transients (buf.getArrayOfReadPointers(), buf.getNumChannels(), buf.getNumSamples(), rate, sensitivity))
        out.push_back (s / rate);
    return out;
}

void Controller::quantiseAudio (Id clipId, double strength)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->isMidi()) return;
    const auto hits = hitsIn (clipId, 0.5);
    if (hits.empty()) { say ("No hits were found in the clip."); return; }
    const Tick g = grid;
    apply ("Quantise Audio", [clipId, hits, g, strength] (Project& p)
    {
        auto* x = p.clip (clipId);
        if (! x) return;
        const Tick contentStart = x->start - x->offset;
        const Tick contentLength = x->loopLength > 0 ? x->loopLength : x->offset + x->length;
        x->audio.markers = audiotools::quantiseMarkers (x->audio, hits, p.tempo, contentStart, contentLength, g, strength);
        // Granular keeps the hits sharp (decision 0022).
        if (x->audio.warp == WarpMode::Off) x->audio.warp = WarpMode::Granular;
    });
    say ("Quantised " + String ((int) hits.size()) + " hits to the grid with warp markers. Undo, or clear the markers, to go back.", false, true);
}

void Controller::renderOfflineFx (Id clipId)
{
    const auto* cl = project.clip (clipId);
    if (! cl || cl->offlineFx.chain.empty()) return;
    const File src = fileOf (*cl, false);
    const auto out = recordingsFolder().getNonexistentChildFile (src.getFileNameWithoutExtension() + " fx", ".wav");
    String error;
    if (engine.renderOfflineFx (*cl, src, out, error))
    {
        apply ("Render Offline Effects", [clipId, out] (Project& p) { if (auto* x = p.clip (clipId)) x->audio.renderedFile = out.getFullPathName().toStdString(); });
        say ("Rendered. The clip plays the processed audio; the original is kept, and removing the effects brings it back.", false, true);
    }
    else say ("The effects couldn't be rendered. " + error, true);
}

} // namespace kal
