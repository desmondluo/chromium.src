// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_DIALOGS_SELECTED_FILE_INFO_H_
#define UI_BASE_DIALOGS_SELECTED_FILE_INFO_H_

#include <vector>

#include "base/file_path.h"
#include "base/string16.h"
#include "ui/base/ui_export.h"

namespace ui {

// Struct used for passing selected file info to WebKit.
struct UI_EXPORT SelectedFileInfo {
  // Selected file's user friendly path as seen in the UI.
  FilePath file_path;

  // The actual local path to the selected file. This can be a snapshot file
  // with a human unreadable name like /blah/.d41d8cd98f00b204e9800998ecf8427e.
  // |real_path| can differ from |file_path| for drive files (e.g.
  // /drive_cache/temporary/d41d8cd98f00b204e9800998ecf8427e vs.
  // /special/drive/foo.txt).
  // If not set, defaults to |file_path|.
  FilePath local_path;

  // This field is optional. The display name contains only the base name
  // portion of a file name (ex. no path separators), and used for displaying
  // selected file names. If this field is empty, the base name portion of
  // |path| is used for displaying.
  FilePath::StringType display_name;

  SelectedFileInfo();
  SelectedFileInfo(const FilePath& in_file_path,
                   const FilePath& in_local_path);
  ~SelectedFileInfo();
};

}  // namespace ui

#endif  // UI_BASE_DIALOGS_SELECTED_FILE_INFO_H_
