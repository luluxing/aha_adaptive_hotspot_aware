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

struct Hotspots {
  std::vector<std::string> low_keys_;
  std::vector<std::string> up_keys_;

  bool Empty() { 
    return low_keys_.size() == 0 || up_keys_.size() == 0; 
  }

  void Clear() {
    low_keys_.clear();
    up_keys_.clear();
  }

  bool WithinHotspot(const Slice& key) {
    for (size_t i = 0; i < low_keys_.size(); i++) {
      if (key.compare(low_keys_[i]) >= 0 && key.compare(up_keys_[i]) <= 0) {
        return true;
      }
    }
    return false;
  }

  bool RangeInsideHotspot(const Slice& lk, const Slice& uk) {
    assert(lk.compare(uk) <= 0);
    for (size_t i = 0; i < low_keys_.size(); i++) {
      if (lk.compare(low_keys_[i]) >= 0 && uk.compare(up_keys_[i]) <= 0) {
        return true;
      }
    }
    return false;
  }

  bool HasMoreHotspot(int idx) {
    return idx < low_keys_.size();
  }

  const std::string& GetLowKey(int idx) {
    return low_keys_[idx];
  }

  const std::string& GetUpKey(int idx) {
    return up_keys_[idx];
  }
};

} // namespace WOT_NAMESPACE

#endif