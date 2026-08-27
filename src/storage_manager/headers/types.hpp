#ifndef STORAGE_TYPES
#define STORAGE_TYPES

#include <assert.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdint.h>
#include <variant>
#include <vector>

// namespace diskoperator_types
namespace diskoperator_types {
enum page_type { HEAP_PAGE, INDEX_PAGE };
}

// namespace buffer_manager_types
namespace buffer_manager_types {

constexpr std::size_t buffer_size     = 50;
constexpr std::size_t MAX_HEAP_PAGES  = 100;
constexpr std::size_t MAX_INDEX_PAGES = 100;
typedef size_t        frame_id;
constexpr frame_id    INVALID_FRAME   = static_cast<frame_id>(-1);
constexpr frame_id    INVALID_PAGE_ID = static_cast<frame_id>(-1);
constexpr std::size_t page_data_size  = 4096;

struct Page {
    char page_data[page_data_size];
    int  page_id   = INVALID_PAGE_ID; // gives the max value of size_t
    bool dirty_bit = false;
    int  pin_count = 0;
    diskoperator_types::page_type type;
};

} // namespace buffer_manager_types

// namespace heap_page_types
namespace heap_page_types {

typedef int page_id;

constexpr int MAX_SLOTS = 10;

#pragma pack(push, 1)
struct PageHeader {
    int  free_size;
    int  slot_count     = 0;
    bool is_initialized = false;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct Slot {
    int      slot_size;
    uint16_t slot_offset;
    bool     deleted = false;
};
#pragma pack(pop)

constexpr int HEAP_PAYLOAD_SIZE =
    static_cast<int>(buffer_manager_types::page_data_size - sizeof(PageHeader) -
                     sizeof(Slot) * MAX_SLOTS);
#pragma pack(push, 1)
struct HeapPage {
    PageHeader page_header;
    Slot       slots[MAX_SLOTS];
    char       data[HEAP_PAYLOAD_SIZE];

    // DO NOT INITIALIZE YOURSELF, USED INTERNALLY
    void initialize() {
        page_header.is_initialized = true;
        page_header.free_size      = HEAP_PAYLOAD_SIZE;
        page_header.slot_count     = 0;
    }
};
#pragma pack(pop)

struct RID {

    page_id pid;
    Slot    slot;

    bool operator==(const RID &other) {
        return other.pid == pid;
    }
};

struct RID_Hash {
    size_t operator()(const RID &e) {
        return 0x9e3779b9 ^
               ((e.pid + (3 << 2)) ^ (e.slot.slot_size + (7 << 2)));
    }
};

} // namespace heap_page_types

// namespace btree_page_types
namespace btree_page_types {
constexpr int MAX_KEYS = 3;
typedef int   node_id;

#pragma pack(push, 1)
struct Node {

  private:
    struct Leaf_Node {
        heap_page_types::RID values[MAX_KEYS];
        node_id              next_leaf = buffer_manager_types::INVALID_PAGE_ID;
    };
    struct Internal_Node {
        node_id child_nodes[MAX_KEYS + 1];
    };

  public:
    // node_id pid = buffer_manager_types::INVALID_PAGE_ID;
    bool is_leaf   = true;
    int  key_count = 0;
    int  keys[MAX_KEYS];
    union u_data {
        Leaf_Node     leaf_node;
        Internal_Node internal_node;

        // Defines how data is initiated when some particular struct is used
        u_data(Leaf_Node ln) : leaf_node(ln) {};
        u_data(Internal_Node in) : internal_node(in) {};
        u_data() : leaf_node() {};
    } data;
    void init() {
        this->key_count = 0;
        this->is_leaf   = true;
        if (this->is_leaf) {
            this->data.leaf_node.next_leaf =
                buffer_manager_types::INVALID_PAGE_ID;
        }
    }
    // overall Node constructor based on union type
    Node(bool leaf = true) : is_leaf(leaf), key_count(0), data() {};
};
#pragma pack(pop)

} // namespace btree_page_types

namespace access_methods_types {

inline constexpr size_t STRING_MAX_SIZE = 500;
enum SUPORTED_COLUMN_TYPE { STRING, INTEGER, FLOATING };
typedef enum { EQ, GT, LS, GTE, LSE } op_type;

using VALUE_TYPE = std::variant<std::string, int, float>;
struct row_t {
    std::vector<VALUE_TYPE> row;
};

struct SARG {
    std::string col;
    op_type     op;
    VALUE_TYPE  constant;
};

enum ScanStatus { SUCCESS, EOP, EOPs, ERR, LOCKEDIN };
struct ScanResult {
    ScanStatus           scan_status;
    std::optional<row_t> scan_result;
};

} // namespace access_methods_types
#endif
