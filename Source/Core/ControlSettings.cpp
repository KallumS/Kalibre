#include "ControlSettings.h"

#include <nlohmann/json.hpp>

namespace kal
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (ControlSettings, clockOut, sendClock, sendMtc, mtcRate, followClock, link,
                                                 linkStartStop, followMtc, sendMmc, followMmc, networkMidi, mackieIn, mackieOut,
                                                 huiIn, huiOut, oscOn, oscPort, oscReplyHost, oscReplyPort, mcpOn, mcpPort)

std::string saveSettings (const ControlSettings& s) { return nlohmann::json (s).dump (1, '\t'); }

std::optional<ControlSettings> loadSettings (const std::string& text)
{
    try { return nlohmann::json::parse (text).get<ControlSettings>(); }
    catch (...) { return std::nullopt; }
}
} // namespace kal
