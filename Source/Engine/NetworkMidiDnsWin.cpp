// Kalibre's network MIDI session announced on Windows (decision 0129), as the
// Mac announces its own: an AppleMIDI service (_apple-midi._udp) through
// Windows' own DNS-SD (DnsServiceRegister, Windows 10 1809 and later), so a
// Mac's Audio MIDI Setup or an iPad lists it without typing an address.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windns.h>

#include <juce_core/juce_core.h>

#include <string>

namespace kal::networkmidi::detail
{
namespace
{
DNS_SERVICE_INSTANCE* instance = nullptr;
DNS_SERVICE_CANCEL cancel {};
bool registered = false;

void WINAPI done (DWORD, PVOID, PDNS_SERVICE_INSTANCE result)
{
    if (result != nullptr) DnsServiceFreeInstance (result);
}
} // namespace

void withdraw()
{
    if (instance == nullptr) return;
    if (registered)
    {
        DNS_SERVICE_REGISTER_REQUEST request {};
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.pServiceInstance = instance;
        request.pRegisterCompletionCallback = done;
        DnsServiceDeRegister (&request, nullptr);
    }
    DnsServiceFreeInstance (instance);
    instance = nullptr;
    registered = false;
}

bool announce (const juce::String& name, int port)
{
    withdraw();
    // "Kalibre on PC._apple-midi._udp.local", on this computer's own .local name.
    const juce::String service = name + "._apple-midi._udp.local";
    const juce::String host = juce::SystemStats::getComputerName() + ".local";
    instance = DnsServiceConstructInstance (service.toWideCharPointer(), host.toWideCharPointer(), nullptr, nullptr, (WORD) port, 0, 0, 0, nullptr, nullptr);
    if (instance == nullptr) return false;
    DNS_SERVICE_REGISTER_REQUEST request {};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;   // every network
    request.pServiceInstance = instance;
    request.pRegisterCompletionCallback = done;
    request.unicastEnabled = FALSE;
    registered = DnsServiceRegister (&request, &cancel) == DNS_REQUEST_PENDING;
    return registered;
}
} // namespace kal::networkmidi::detail
