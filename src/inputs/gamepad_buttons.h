#pragma once
#include <QtGlobal>

namespace gamepad {
constexpr quint32 Up = 0x200, Down = 0x100, Left = 0x800, Right = 0x400;
constexpr quint32 PadA = 0x8000, PadB = 0x4000, PadX = 0x2000, PadY = 0x1000;
constexpr quint32 L = 0x20, R = 0x10, ZL = 0x80, ZR = 0x40;
constexpr quint32 L3 = 0x800000, R3 = 0x400000;
constexpr quint32 Plus = 0x8, Minus = 0x4, Home = 0x2, Tv = 0x200000;

struct Control {
    const char* name;
    quint32 mask;
};
// Every button, in the order the Inputs page shows them.
inline constexpr Control Controls[] = {
    {"\u2191", Up}, {"\u2190", Left},  {"\u2193", Down}, {"\u2192", Right},
    {"A", PadA},     {"B", PadB},        {"X", PadX},       {"Y", PadY},
    {"L", L},        {"R", R},           {"ZL", ZL},        {"ZR", ZR},
    {"L3", L3},      {"R3", R3},         {"+", Plus},       {"\u2212", Minus},
    {"HOME", Home},  {"TV", Tv},
};

// Raw stick readings: the centre, and the range that maps to -1 to 1.
constexpr int StickCentre = 2050, StickMin = 900, StickMax = 3200;
constexpr double StickHalfRange = 1150.0;
// Accelerometer counts in 1 g, and gyro counts in degrees per second.
constexpr double AccelPerG = 8000.0;
constexpr double GyroDegreesPerCount = 1200.0 / 154000.0;
} // namespace gamepad
