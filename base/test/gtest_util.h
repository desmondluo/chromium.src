// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_GTEST_UTIL_H_
#define BASE_TEST_GTEST_UTIL_H_

#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"

namespace base {

class FilePath;

// First value is test case name, second one is test name.
typedef std::pair<std::string, std::string> SplitTestName;

// Returns a vector of gtest-based tests compiled into
// current executable.
std::vector<SplitTestName> GetCompiledInTests();

// Writes the list of gtest-based tests compiled into
// current executable as a JSON file. Returns true on success.
bool WriteCompiledInTestsToFile(const FilePath& path) WARN_UNUSED_RESULT;

}  // namespace base

#endif  // BASE_TEST_GTEST_UTIL_H_
