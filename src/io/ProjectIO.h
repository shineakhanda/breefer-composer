#pragma once

#include "core/Project.h"

#include <QString>

namespace bc::io {

inline constexpr const char* kProjectExtension = "bcproj";

bool saveProject(const Project& project, const QString& path, QString* error);
bool loadProject(const QString& path, Project& out, QString* error);

} // namespace bc::io
