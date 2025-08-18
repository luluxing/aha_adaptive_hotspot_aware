#include "wot_refactor_all.h"

int main() {
  // Create a tree with default configuration
  std::atomic<uint64_t> flush_id{1};
  auto tree = WOTTreeFactory::CreateDefaultTree("mydb", flush_id);

  // Use the tree
  tree->Insert("key1", "value1");
  std::string value;
  tree->Query("key1", &value);

  // Print comprehensive information
  WOTTreeManager::PrintTreeInfo(tree.get());
}