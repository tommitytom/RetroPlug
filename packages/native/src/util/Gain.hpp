#pragma once

#include <cmath>


/** dB -> linear gain, with a hard zero below -90 dB so a trim-to-mute really is silence rather than
 *  -90 dB of it. Every system backend needs this; it lived in four anonymous namespaces, identically. */
inline float dbToLin(float dB) {
    return dB > -90.0f ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}

