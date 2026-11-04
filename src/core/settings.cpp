#include "settings.h"
#include "platform.h"

#include <algorithm>

const CharsetPreset kCharsetPresets[] = {
    { "Sparse",          L"      .:-=+*#%@" },
    { "Standard",        L" .:-=+*#%@" },
    { "Classic Unicode", L" .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$" },
    { "Binary (0/1)",    L" 01" },
    { "Matrix Rain",     L" ｦｱｳｴｵｶｷｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ1234567890:*+-<>|" },
    { "Custom...",       nullptr },
};
const int kCharsetPresetCount = (int)std::size(kCharsetPresets);

std::wstring Settings::charset() const {
    if (charsetPreset >= 0 && charsetPreset < kCharsetPresetCount && kCharsetPresets[charsetPreset].chars)
        return kCharsetPresets[charsetPreset].chars;
    std::wstring custom = platform::widen(customCharset);
    return custom.empty() ? L" .:+=#@" : custom;
}

void Settings::clamp() {
    brightness    = std::clamp(brightness, -100, 100);
    contrast      = std::clamp(contrast, 0.1f, 3.0f);
    blurSize      = std::clamp(blurSize, 1, 31) | 1; // odd
    minBrightness = std::clamp(minBrightness, 0, 255);
    colorMode     = std::clamp(colorMode, 0, 5);
    for (float& c : color) c = std::clamp(c, 0.0f, 1.0f);
    antialiasMode = std::clamp(antialiasMode, 0, 3);
    charsetPreset = std::clamp(charsetPreset, 0, kCharsetPresetCount - 1);
    if (customCharset.size() > 255) customCharset.resize(255);
    minFont       = std::clamp(minFont, 5.0f, 150.0f);
    maxFont       = std::clamp(maxFont, 30.0f, 150.0f);
    zoomSpeed     = std::clamp(zoomSpeed, 0.01f, 1.0f);
    hysteresis    = std::clamp(hysteresis, 0.0f, 0.3f);
    maxOccupancy  = std::clamp(maxOccupancy, 0.05f, 0.6f);
    minOccupancy  = std::clamp(minOccupancy, 0.005f, 0.2f);
}
