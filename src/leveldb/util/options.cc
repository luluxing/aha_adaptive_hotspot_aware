// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/include/options.h"

#include "leveldb/include/comparator.h"
#include "leveldb/include/env.h"

namespace WOT_NAMESPACE {

Options::Options() : comparator(BytewiseComparator()), env(Env::Default()) {}

}  // namespace WOT_NAMESPACE
