#ifndef WOT_REFACTOR_ALL_H
#define WOT_REFACTOR_ALL_H

/**
 * WOT B+ Tree Refactored Implementation
 * 
 * This header file provides access to the complete refactored WOT B+ Tree implementation.
 * The original monolithic implementation has been broken down into focused, maintainable components.
 * 
 * Component Overview:
 * - wot_types_refactor_: Core types, enums, and basic structures
 * - wot_tree_node_refactor_: Node class and node-specific operations  
 * - wot_tree_iterator_refactor_: Tree iteration functionality
 * - wot_buffer_operations_refactor_: Buffer management and operations
 * - wot_page_split_refactor_: Page splitting algorithms and policies
 * - wot_adaptation_refactor_: Background work and adaptation strategies
 * - wot_utilities_refactor_: Utility functions and helpers
 * - wot_btree_refactor_: Main BplusTree class implementation
 * 
 * Benefits of Refactoring:
 * 1. Improved maintainability through separation of concerns
 * 2. Better testability with focused, smaller components
 * 3. Enhanced code reusability across different tree implementations
 * 4. Reduced compilation dependencies and faster build times
 * 5. Clearer interfaces and abstraction boundaries
 * 
 * Usage:
 * Include this header to access the complete refactored implementation.
 * For specific components, include individual headers as needed.
 */

// Core types and definitions
#include "wot_types_refactor_.h"

// Component headers in dependency order
#include "wot_utilities_refactor_.h"
#include "wot_buffer_operations_refactor_.h"
#include "wot_page_split_refactor_.h"
#include "wot_tree_node_refactor_.h"
#include "wot_tree_iterator_refactor_.h"
#include "wot_adaptation_refactor_.h"
#include "wot_btree_refactor_.h"

namespace WOT_NAMESPACE {

/**
 * Factory class for creating WOT B+ Tree instances
 */
class WOTTreeFactory {
public:
    /**
     * Create a new WOT B+ Tree instance
     * @param options LevelDB-style options for configuration
     * @param dbname Database name/path
     * @param flush_id Atomic counter for file numbering
     * @param is_lsmt Whether this is a pure LSMT tree
     * @param is_buffer_tree Whether this uses buffer tree structure
     * @return Unique pointer to the created tree
     */
    static std::unique_ptr<BplusTree> CreateTree(
        const leveldb::Options& options,
        const std::string& dbname,
        std::atomic<uint64_t>& flush_id,
        bool is_lsmt = false,
        bool is_buffer_tree = false) {
        
        return std::make_unique<ConcreteBplusTree>(
            options, dbname, flush_id, is_lsmt, is_buffer_tree);
    }
    
    /**
     * Create a tree with default configuration
     */
    static std::unique_ptr<BplusTree> CreateDefaultTree(
        const std::string& dbname,
        std::atomic<uint64_t>& flush_id) {
        
        leveldb::Options options;
        TreeConfig config = ConfigUtils::CreateDefaultConfig();
        
        // Apply tree config to leveldb options
        options.write_buffer_size = config.write_buffer_size;
        // Add other config mappings as needed
        
        return CreateTree(options, dbname, flush_id, false, true);
    }
};

/**
 * Utility class for tree operations and management
 */
class WOTTreeManager {
public:
    /**
     * Validate tree structure integrity
     */
    static bool ValidateTree(BplusTree* tree) {
        if (!tree) return false;
        
        // Basic validation checks
        if (tree->TreeHeight() == 0) return false;
        if (!tree->GetRoot()) return false;
        if (!tree->GetBufferManager()) return false;
        if (!tree->GetLockManager()) return false;
        
        // More detailed validation would be implemented here
        return true;
    }
    
    /**
     * Print comprehensive tree statistics
     */
    static void PrintTreeInfo(BplusTree* tree) {
        if (!tree) {
            std::cout << "NULL tree\n";
            return;
        }
        
        std::cout << "=== WOT B+ Tree Information ===\n";
        std::cout << "Height: " << tree->TreeHeight() << "\n";
        std::cout << "Root Page ID: " << tree->GetRootPageId() << "\n";
        std::cout << "Node Table Size: " << tree->GetNodeTable().size() << "\n";
        std::cout << "Memory Usage: " << tree->MemoryUsage() << " bytes\n";
        std::cout << "Sequence Number: " << tree->GetSequenceNumber() << "\n";
        
        if (tree->GetLSMT()) {
            std::cout << "Has Root LSMT: Yes\n";
        } else {
            std::cout << "Has Root LSMT: No\n";
        }
        
        std::cout << "Configuration:\n";
        const auto& config = tree->GetConfig();
        std::cout << "  Node LSMT Level Limit: " << config.node_lsmt_level_limit << "\n";
        std::cout << "  LSMT Level Limit: " << config.lsmt_level_limit << "\n";
        std::cout << "  Leaf Limit: " << config.leaf_limit << "\n";
        std::cout << "  Write Buffer Size: " << config.write_buffer_size << "\n";
        
        std::cout << "Performance Statistics:\n";
        DebugUtils::PrintPerformanceStats(tree->GetReaderStats(), tree->GetWriterStats());
    }
    
    /**
     * Export tree structure to file for analysis
     */
    static void ExportTree(BplusTree* tree, const std::string& filename) {
        DebugUtils::DumpTreeToFile(tree, filename);
    }
};

} // namespace WOT_NAMESPACE

#endif // WOT_REFACTOR_ALL_H