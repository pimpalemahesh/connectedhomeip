// Mock of <platform/ESP32/ESP32Utils.h> for host unit tests. NOT the real header.
//
// MultiImageOTAProcessorImpl.cpp needs PlatformMgr/ConfigurationMgr (which the
// real ESP32Utils.h pulls in transitively) and the chip::DeviceLayer::Internal
// namespace to exist (for a `using namespace` directive). It does not call into
// ESP32Utils itself on the paths exercised by the host tests.
#pragma once

#include <platform/CHIPDeviceLayer.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
