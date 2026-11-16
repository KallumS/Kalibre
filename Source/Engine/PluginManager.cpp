#include "PluginManager.h"

#include "ClapFormat.h"
#include "MorphProcessor.h"

namespace kal
{
using namespace juce;

static File supportFolder()
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
   #if JUCE_MAC
               .getChildFile ("Application Support")
   #endif
               .getChildFile ("Kalibre");
    f.createDirectory();
    return f;
}

PluginManager::PluginManager()
{
    addDefaultFormatsToManager (formatManager);
    formatManager.addFormat (std::make_unique<clap::ClapPluginFormat>());

    listFile = supportFolder().getChildFile ("Plug-ins.xml");
    pedalFile = supportFolder().getChildFile ("Scanning.txt");
    if (auto xml = parseXML (listFile)) list.recreateFromXml (*xml);
}

PluginManager::~PluginManager()
{
    stopTimer();
    save();
}

void PluginManager::save()
{
    if (auto xml = list.createXml()) xml->writeTo (listFile);
}

void PluginManager::scan (std::function<void (float, const String&)> progress)
{
    if (isTimerRunning()) return;
    onProgress = std::move (progress);
    queue.clear();
    queueIndex = 0;
    for (auto* format : formatManager.getFormats())
    {
        auto paths = format->getDefaultLocationsToSearch();
        if (! format->canScanForPlugins()) continue;
        Pending p;
        p.format = format;
        p.scanner = std::make_unique<PluginDirectoryScanner> (list, *format, paths, true, pedalFile, false);
        queue.push_back (std::move (p));
    }
    startTimer (1);
}

void PluginManager::cancelScan()
{
    stopTimer();
    queue.clear();
    save();
    if (onProgress) onProgress (1.0f, {});
}

void PluginManager::timerCallback()
{
    if (queueIndex >= queue.size())
    {
        stopTimer();
        queue.clear();
        save();
        if (onProgress) onProgress (1.0f, String (list.getNumTypes()) + " plug-ins");
        return;
    }
    auto& p = queue[queueIndex];
    String name;
    const auto next = p.scanner->getNextPluginFileThatWillBeScanned();
    const bool more = p.scanner->scanNextFile (true, name);
    if (onProgress)
    {
        const float part = 1.0f / (float) queue.size();
        onProgress (jmin (0.99f, part * ((float) queueIndex + p.scanner->getProgress())),
                    p.format->getName() + ": " + File::createFileWithoutCheckingPath (next).getFileNameWithoutExtension());
    }
    if (! more) ++queueIndex;
}

//==============================================================================
PluginRef PluginManager::refFor (const PluginDescription& d)
{
    PluginRef r;
    r.format = d.pluginFormatName.toStdString();
    r.identifier = d.fileOrIdentifier.toStdString();
    r.name = d.name.toStdString();
    r.manufacturer = d.manufacturerName.toStdString();
    r.uid = d.uniqueId;
    r.isInstrument = d.isInstrument;
    if (auto xml = d.createXml()) r.description = xml->toString (XmlElement::TextFormat().singleLine().withoutHeader()).toStdString();
    return r;
}

bool PluginManager::descriptionFor (const PluginRef& r, PluginDescription& d)
{
    if (! r.description.empty())
        if (auto xml = parseXML (String (r.description)))
            if (d.loadFromXml (*xml)) return true;
    d.pluginFormatName = r.format;
    d.fileOrIdentifier = r.identifier;
    d.name = r.name;
    d.manufacturerName = r.manufacturer;
    d.uniqueId = d.deprecatedUid = r.uid;
    d.isInstrument = r.isInstrument;
    return ! r.identifier.empty();
}

DeviceRole PluginManager::roleFor (const PluginDescription& d)
{
    if (d.isInstrument) return DeviceRole::Instrument;
    const auto cat = d.category.toLowerCase();
    if (cat.contains ("note effect") || cat.contains ("midi") || (d.numInputChannels == 0 && d.numOutputChannels == 0))
        return DeviceRole::MidiFx;
    return DeviceRole::AudioFx;
}

std::unique_ptr<AudioPluginInstance> PluginManager::create (const PluginRef& ref, double sampleRate, int blockSize,
                                                            String& error)
{
    if (ref.format == "Kalibre")
        return makeBuiltIn (ref, *this);

    PluginDescription d;
    if (! descriptionFor (ref, d))
    {
        error = "Nothing is known about this plug-in.";
        return nullptr;
    }
    // The installed copy may have moved since the project was saved: find it by its id.
    if (! formatManager.doesPluginStillExist (d))
        for (auto& k : list.getTypes())
            if (k.uniqueId == d.uniqueId && k.pluginFormatName == d.pluginFormatName) { d = k; break; }
    return formatManager.createPluginInstance (d, sampleRate, blockSize, error);
}

} // namespace kal
