/** @file face_plugin.cpp
 * @brief Shared FaceEvent -> status word table, used by every theme so
 *        wording is consistent regardless of the active face.
 */

#include "face_plugin.hpp"
#include <cstddef>

const char *face_event_label(FaceEvent event)
{
    switch (event) {
        case FaceEvent::Idle:          return NULL;
        case FaceEvent::Recording:     return "REC";
        case FaceEvent::VoiceActive:   return "LISTENING";
        case FaceEvent::Silence:       return "SILENCE";
        case FaceEvent::Saving:        return "SAVING";
        case FaceEvent::Uploading:     return "SENDING";
        case FaceEvent::UploadSuccess: return "SENT";
        case FaceEvent::UploadError:   return "ERROR";
        case FaceEvent::LowBattery:    return "LOW BATT";
        default:                       return NULL;
    }
}
