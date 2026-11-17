#include "Controllers.h"

namespace kal::controllers
{

std::string name (int cc)
{
    switch (cc)
    {
        case 0:   return "Bank Select";
        case 1:   return "Modulation";
        case 2:   return "Breath Controller";
        case 4:   return "Foot Controller";
        case 5:   return "Portamento Time";
        case 6:   return "Data Entry";
        case 7:   return "Volume";
        case 8:   return "Balance";
        case 10:  return "Pan";
        case 11:  return "Expression";
        case 12:  return "Effect Control 1";
        case 13:  return "Effect Control 2";
        case 16: case 17: case 18: case 19:
            return "General Purpose " + std::to_string (cc - 15);
        case 64:  return "Sustain Pedal";
        case 65:  return "Portamento On/Off";
        case 66:  return "Sostenuto Pedal";
        case 67:  return "Soft Pedal";
        case 68:  return "Legato Footswitch";
        case 69:  return "Hold 2";
        case 70:  return "Sound Variation";
        case 71:  return "Resonance (Timbre)";
        case 72:  return "Release Time";
        case 73:  return "Attack Time";
        case 74:  return "Brightness (Cutoff)";
        case 75:  return "Decay Time";
        case 76:  return "Vibrato Rate";
        case 77:  return "Vibrato Depth";
        case 78:  return "Vibrato Delay";
        case 79:  return "Sound Controller 10";
        case 80: case 81: case 82: case 83:
            return "General Purpose " + std::to_string (cc - 75);
        case 84:  return "Portamento Control";
        case 88:  return "High Resolution Velocity";
        case 91:  return "Reverb Send";
        case 92:  return "Tremolo Depth";
        case 93:  return "Chorus Send";
        case 94:  return "Detune";
        case 95:  return "Phaser Depth";
        case 96:  return "Data Increment";
        case 97:  return "Data Decrement";
        case 98:  return "NRPN LSB";
        case 99:  return "NRPN MSB";
        case 100: return "RPN LSB";
        case 101: return "RPN MSB";
        case 120: return "All Sound Off";
        case 121: return "Reset All Controllers";
        case 122: return "Local Control";
        case 123: return "All Notes Off";
        case 124: return "Omni Mode Off";
        case 125: return "Omni Mode On";
        case 126: return "Mono Mode On";
        case 127: return "Poly Mode On";
        default: break;
    }
    if (cc >= 32 && cc <= 63)
    {
        const auto of = name (cc - 32);
        return of == "Undefined" ? "LSB for CC " + std::to_string (cc - 32) : of + " (LSB)";
    }
    return "Undefined";
}

std::string label (int cc)
{
    const auto n = name (cc);
    return "CC " + std::to_string (cc) + (n == "Undefined" ? std::string() : " " + n);
}

const std::vector<int>& common()
{
    static const std::vector<int> list { 1, 11, 7, 2, 21, 64, 10, 4, 5, 65, 68, 71, 72, 73, 74, 91, 93 };
    return list;
}

} // namespace kal::controllers
