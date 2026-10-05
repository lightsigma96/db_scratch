#include "../src/storage_manager/headers/buffer_manager.hpp"
#include "../src/storage_manager/headers/types.hpp"
#include "../src/wal/wal.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
constexpr const char *HEAP_FILE = "wal_test_heap.bin";
constexpr const char *INDEX_FILE = "wal_test_index.bin";
constexpr const char *WAL_FILE = "wal.bin";
constexpr std::uint64_t WAL_HEADER_SIZE = sizeof(std::uint64_t);

class WALTest : public testing::Test {
  protected:
    std::unique_ptr<buffer_manager::buffer_pool> buffer_pool;
    std::unique_ptr<access_methods::Access_methods> access_methods;

    void SetUp() override {
        std::remove(HEAP_FILE);
        std::remove(INDEX_FILE);
        std::remove(WAL_FILE);

        buffer_pool = std::make_unique<buffer_manager::buffer_pool>(HEAP_FILE, INDEX_FILE, WAL_FILE);
        access_methods = std::make_unique<access_methods::Access_methods>();
    }

    void TearDown() override {
        buffer_pool.reset();
        access_methods.reset();

        std::remove(HEAP_FILE);
        std::remove(INDEX_FILE);
        std::remove(WAL_FILE);
    }

    static access_methods_types::row_t test_row() {
        access_methods_types::row_t row;
        row.row.emplace_back(42);
        row.row.emplace_back(3.5f);
        row.row.emplace_back(std::string("wal-test"));
        return row;
    }

    static heap_page_types::RID write_test_row(buffer_manager::buffer_pool &bp,
                                               const access_methods_types::row_t &row) {
        auto *page = bp.page_access(0, diskoperator_types::HEAP_PAGE);
        auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
        heap_page->initialize();

        const std::uint16_t offset = static_cast<std::uint16_t>(heap_page->page_header.free_size -
            (sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE));

        heap_page->page_header.free_size = offset;
        heap_page->page_header.slot_count = 1;
        heap_page->slots[0].slot_size =
            sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
        heap_page->slots[0].slot_offset = offset;

        heap_page->data[offset + 0] = 0;

        bp.un_pin(0, diskoperator_types::HEAP_PAGE);

        std::vector<char> bytes;
        bytes.resize(heap_page->slots[0].slot_size);
        int i = 42;
        float f = 3.5f;
        std::string s = "wal-test";
        std::memcpy(bytes.data(), &i, sizeof(i));
        std::memcpy(bytes.data() + sizeof(i), &f, sizeof(f));
        std::memcpy(bytes.data() + sizeof(i) + sizeof(f), s.data(), s.size());

        bp.page_access(0, diskoperator_types::HEAP_PAGE);
        heap_page = reinterpret_cast<heap_page_types::HeapPage *>(
            bp.page_access(0, diskoperator_types::HEAP_PAGE)->page_data);
        std::memcpy(heap_page->data + offset, bytes.data(), bytes.size());

        heap_page_types::RID rid{0, heap_page->slots[0]};
        bp.dp_write_page(bp.page_access(0, diskoperator_types::HEAP_PAGE), diskoperator_types::HEAP_PAGE);
        bp.un_pin(0, diskoperator_types::HEAP_PAGE);
        return rid;
    }
};

TEST_F(WALTest, OffsetPointsToLatestRecord) {
    const auto row = test_row();
    heap_page_types::Slot slot{};
    slot.slot_size = sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
    slot.slot_offset = 100;
    heap_page_types::RID rid{0, slot};

    WAL::WAL wal(*buffer_pool, *access_methods);
    wal.CommitTransaction(rid, row);

    std::ifstream file(WAL_FILE, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    std::uint64_t offset = 0;
    file.read(reinterpret_cast<char *>(&offset), sizeof(offset));
    ASSERT_TRUE(file.good());
    EXPECT_EQ(offset, WAL_HEADER_SIZE);
}

TEST_F(WALTest, LSNUpdatedAfterTransaction) {
    const auto row = test_row();
    heap_page_types::Slot slot{};
    slot.slot_size = sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
    slot.slot_offset = 100;
    heap_page_types::RID rid{0, slot};

    WAL::WAL wal(*buffer_pool, *access_methods);
    wal.CommitTransaction(rid, row);

    auto *page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
    EXPECT_EQ(heap_page->page_header.lsn, 1);
    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
}

TEST_F(WALTest, ReplayRestoresRow) {
    const auto row = test_row();
    heap_page_types::Slot slot{};
    slot.slot_size = sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
    slot.slot_offset = 100;
    heap_page_types::RID rid{0, slot};

    WAL::WAL wal(*buffer_pool, *access_methods);
    wal.CommitTransaction(rid, row);

    auto *page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
    std::memset(heap_page->data + slot.slot_offset, 0, slot.slot_size);
    heap_page->page_header.lsn = 0;
    buffer_pool->dp_write_page(page, diskoperator_types::HEAP_PAGE);
    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);

    wal.ReplayTransaction();

    page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);

    int i = 0;
    float f = 0;
    char s[access_methods_types::STRING_MAX_SIZE] = {};
    std::memcpy(&i, heap_page->data + slot.slot_offset, sizeof(i));
    std::memcpy(&f, heap_page->data + slot.slot_offset + sizeof(i), sizeof(f));
    std::memcpy(s, heap_page->data + slot.slot_offset + sizeof(i) + sizeof(f), sizeof(s));

    EXPECT_EQ(i, 42);
    EXPECT_FLOAT_EQ(f, 3.5f);
    EXPECT_EQ(std::string(s, 8), "wal-test");
    EXPECT_EQ(heap_page->page_header.lsn, 1);

    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
}

TEST_F(WALTest, ReplaySkippedWhenPageLSNIsCurrent) {
    const auto row = test_row();
    heap_page_types::Slot slot{};
    slot.slot_size = sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
    slot.slot_offset = 100;
    heap_page_types::RID rid{0, slot};

    WAL::WAL wal(*buffer_pool, *access_methods);
    wal.CommitTransaction(rid, row);

    auto *page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
    heap_page->page_header.lsn = 1;
    std::memset(heap_page->data + slot.slot_offset, 0x5a, slot.slot_size);
    buffer_pool->dp_write_page(page, diskoperator_types::HEAP_PAGE);
    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);

    wal.ReplayTransaction();

    page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);

    for (int i = 0; i < slot.slot_size; i++) {
        EXPECT_EQ(static_cast<unsigned char>(heap_page->data[slot.slot_offset + i]), 0x5a);
    }

    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
}

TEST_F(WALTest, ReplayAppliedWhenPageLSNIsOlder) {
    const auto row = test_row();
    heap_page_types::Slot slot{};
    slot.slot_size = sizeof(int) + sizeof(float) + access_methods_types::STRING_MAX_SIZE;
    slot.slot_offset = 100;
    heap_page_types::RID rid{0, slot};

    WAL::WAL wal(*buffer_pool, *access_methods);
    wal.CommitTransaction(rid, row);

    auto *page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
    heap_page->page_header.lsn = 0;
    std::memset(heap_page->data + slot.slot_offset, 0, slot.slot_size);
    buffer_pool->dp_write_page(page, diskoperator_types::HEAP_PAGE);
    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);

    wal.ReplayTransaction();

    page = buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
    heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);

    EXPECT_EQ(heap_page->page_header.lsn, 1);
    EXPECT_NE(heap_page->data[slot.slot_offset], 0);

    buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
}
