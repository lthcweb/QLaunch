// Website icons, fetched from the site itself the way a browser does (no third-party icon service).
#pragma once

#include "core/base.h"
#include "skin/image.h"

// Downloads the icon of an http(s) page: the best <link rel="icon"> or apple-touch-icon for the wanted size,
// else /favicon.ico. Blocks for up to a few seconds; call it on a background thread with COM initialized.
bool DownloadFavicon(const std::wstring& url, int size, Bitmap& out);
