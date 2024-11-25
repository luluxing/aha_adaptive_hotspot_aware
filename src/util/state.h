#ifndef STATE_WOT_NAMESAPCE_H_
#define STATE_WOT_NAMESAPCE_H_

#include <inttypes.h>
#include <map>
#include "leveldb/include/slice.h"

namespace WOT_NAMESPACE {

struct slice_cmp {
  bool operator()(const Slice& a, const Slice& b) const {
    return a.compare(b) < 0;
  }
};

typedef std::map<Slice, uint32_t, slice_cmp> SlicePageMap;

enum class FileState : uint8_t {
  kNotExist,
  kNotOpen,
  kOk,
  kEmptyFile,
};

enum class OpState : uint8_t {
  kOk,
  kOverflow,
  kNotSupported,
  kAborted,
  kNotFound,
};

enum class TreeState : uint8_t {
  kNoAdapt,
  kToWriteOpt,
  kToReadOpt,
  kDoneAdaptToWrite,
  kDoneAdaptToRead,
};

enum class LSMTStatus : uint8_t {
  kBuffer,
  kRootBuffer,
  kLeaf,
  kSmallLeaf,
  kStandalone,
};

enum class WOTState : uint8_t {
  kBtree,
  kLSMT,
  kWOT,
};

enum class IterState : uint8_t {
  kInvalid,
  kMem,
  kImm,
  kLSMT,
  kTree,
};

} // namespace WOT_NAMESPACE

#endif