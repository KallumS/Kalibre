#include "Check.h"
#include "Platform.h"

using namespace kal;

TEST ("platform: a key and a click as each system writes it: Option-click on the Mac, Alt+click on Windows (0129)")
{
    CHECK_EQ (platform::optionClick ("click", platform::mac), std::string ("Option-click"));
    CHECK_EQ (platform::optionClick ("drag", platform::pc), std::string ("Alt+drag"));
    CHECK_EQ (platform::commandKey ("D", platform::mac), std::string ("Command-D"));
    CHECK_EQ (platform::commandKey ("D", platform::pc), std::string ("Ctrl+D"));
    CHECK_EQ (std::string (platform::pc.showInFiles), std::string ("Show in File Explorer"));
    CHECK_EQ (std::string (platform::mac.showInFiles), std::string ("Show in Finder"));
    CHECK_EQ (platform::inSettings ("Plug-in Manager", platform::mac), std::string ("Kalibre > Plug-in Manager"));
    CHECK_EQ (platform::inSettings ("Plug-in Manager", platform::pc), std::string ("File > Settings > Plug-in Manager"));
    // The words in use are this system's.
    CHECK_EQ (std::string (platform::words().option), std::string (platform::windows ? "Alt" : "Option"));
}
