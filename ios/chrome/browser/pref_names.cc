// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/pref_names.h"

namespace ios {
namespace prefs {

// Preferences in ios::prefs:: must have the same value as the corresponding
// preference on desktop.
// These preferences must not be registered by //ios code.
// See chrome/common/pref_names.cc for a detailed description of each
// preference.

const char kAcceptLanguages[] = "intl.accept_languages";

}  // namespace prefs
}  // namespace ios
