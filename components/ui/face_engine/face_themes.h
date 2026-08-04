/** @file face_themes.h
 * @brief Factory functions for built-in face themes.
 *
 * Each returns a heap-allocated FacePlugin* that the registry owns.
 * The FaceConfig is passed by value — themes copy what they need.
 */

#pragma once

#include "face_plugin.hpp"

#ifdef __cplusplus
extern "C" {
#endif

FacePlugin *create_owl_face(const FaceConfig &cfg);
FacePlugin *create_minimal_face(const FaceConfig &cfg);
FacePlugin *create_robot_face(const FaceConfig &cfg);
FacePlugin *create_pixel_face(const FaceConfig &cfg);

#ifdef __cplusplus
}
#endif
