#ifndef BUFFER_MANAGER_WOT_NAMESPACE_H_
#define BUFFER_MANAGER_WOT_NAMESPACE_H_

#include <cstdint>

#include "tree_page.h"

namespace WOT_NAMESPACE {

class BufferManager;

// Create a new cache with a fixed size capacity.  This implementation
// of Cache uses a least-recently-used eviction policy.
BufferManager* NewBufferManager(size_t capacity, const std::string& file_name);

class BufferManager {
 public:
  BufferManager() = default;

  BufferManager(const BufferManager&) = delete;
  BufferManager& operator=(const BufferManager&) = delete;

  // Destroys all existing entries by calling the "deleter"
  // function that was passed to the constructor.
  virtual ~BufferManager();

  virtual Page Lookup(uint32_t pg_id) = 0;
  virtual Page Pin(uint32_t pg_id) = 0;
  virtual void Unpin(uint32_t pg_id) = 0;
  virtual void UnpinAndRelease(uint32_t pg_id) = 0;
  virtual void Delete(uint32_t pg_id) = 0;
  virtual uint32_t Allocate() = 0;
  virtual uint32_t AllocateRoot(uint32_t root_id) = 0;

  virtual void PrintStat() = 0;
};

}  // namespace WOT_NAMESPACE

#endif // BUFFER_MANAGER_WOT_NAMESPACE_H_