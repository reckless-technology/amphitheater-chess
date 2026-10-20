#pragma once
/// @file Faces.h The two families' faces handed to Skia from the files the binary carries: Noto Serif for the
/// interface, Monaspace Xenon for engine output and FEN.

#include "Embedded.h"
#include "SkiaCanvas.h"

namespace amphitheater::app
{

/** Hand the faces to Skia; once, before anything draws text. */
inline void InstallFaces()
{
    reckless::gfx::SetSerifFaces(reckless::embedded::Find("NotoSerif-Regular.ttf"),
                                 reckless::embedded::Find("NotoSerif-Bold.ttf"));
    reckless::gfx::SetMonospaceFaces(reckless::embedded::Find("MonaspaceXenon-Regular.otf"),
                                     reckless::embedded::Find("MonaspaceXenon-Bold.otf"));
}

} // namespace amphitheater::app
