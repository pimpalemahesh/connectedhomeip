// Minimal stub for the global OTA requestor accessor.
//
// MultiImageOTAProcessorImpl references chip::GetRequestorInstance() from
// IsFirstImageRun()/ConfirmCurrentImage(), which the host tests do not exercise.
// Defining it here avoids linking the full ota-requestor source set (and its
// transitive server/app dependencies).
#include <app/clusters/ota-requestor/OTARequestorInterface.h>

namespace chip {

OTARequestorInterface * GetRequestorInstance()
{
    return nullptr;
}

} // namespace chip
