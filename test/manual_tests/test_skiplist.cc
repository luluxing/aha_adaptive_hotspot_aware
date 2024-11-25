#include "leveldb/skiplist.h"
#include "leveldb/include/slice.h"

using namespace WOT_NAMESPACE;
typedef uint64_t Key;

struct Comparator {
  int operator()(const Key& a, const Key& b) const {
    if (a < b) {
      return -1;
    } else if (a > b) {
      return +1;
    } else {
      return 0;
    }
  }
};

void test_empty_skiplist() {
  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  assert(!list.Contains(10));

  SkipList<Key, Comparator>::Iterator iter(&list);
  assert(!iter.Valid());
  iter.SeekToFirst();
  assert(!iter.Valid());
  iter.Seek(100);
  assert(!iter.Valid());
  iter.SeekToLast();
  assert(!iter.Valid());
}

int main() {
  test_empty_skiplist();
  return 0;
}