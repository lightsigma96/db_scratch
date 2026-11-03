#include "../src/wal/wal.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

struct LoggedRecord {
    wal_types::WAL_entry header{};
    std::vector<char> payload;
};

class WALTest : public testing::Test {
  protected:
    std::filesystem::path original_directory;
    std::filesystem::path temporary_directory;
    std::string heap_filename = "heap.bin";
    std::string index_filename = "index.bin";
    std::string wal_filename = "wal.bin";
    std::unique_ptr<buffer_manager::buffer_pool> buffer_pool;
    std::unique_ptr<WAL::WAL> wal;

    void SetUp() override {
        original_directory = std::filesystem::current_path();
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        temporary_directory =
            std::filesystem::temp_directory_path() /
            ("db_scratch_wal_test_" + std::to_string(suffix));
        std::filesystem::create_directories(temporary_directory);
        std::filesystem::current_path(temporary_directory);

        open_database();
        initialize_heap_page();
    }

    void TearDown() override {
        wal.reset();
        buffer_pool.reset();
        std::filesystem::current_path(original_directory);
        std::filesystem::remove_all(temporary_directory);
    }

    void open_database() {
        buffer_pool = std::make_unique<buffer_manager::buffer_pool>(
            heap_filename, index_filename, wal_filename);
        wal = std::make_unique<WAL::WAL>(*buffer_pool);
    }

    static heap_page_types::RID make_rid(std::uint16_t offset, int size) {
        heap_page_types::Slot slot{};
        slot.slot_offset = offset;
        slot.slot_size = size;
        return {0, slot};
    }

    void initialize_heap_page() {
        auto *page =
            buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
        std::memset(page->page_data, 0, buffer_manager_types::page_data_size);
        auto *heap_page =
            reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
        heap_page->initialize();
        page->dirty_bit = false;
        buffer_pool->dp_write_page(page, diskoperator_types::HEAP_PAGE);
        buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
    }

    void write_int_at(std::uint16_t offset, int value) {
        auto *page =
            buffer_pool->page_access(0, diskoperator_types::HEAP_PAGE);
        auto *heap_page =
            reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
        std::memcpy(heap_page->data + offset, &value, sizeof(value));
        page->dirty_bit = true;
        buffer_pool->dp_write_page(page, diskoperator_types::HEAP_PAGE);
        page->dirty_bit = false;
        buffer_pool->un_pin(0, diskoperator_types::HEAP_PAGE);
    }

    std::array<char, buffer_manager_types::page_data_size>
    make_raw_page(std::uint16_t lsn, std::uint16_t offset = 0,
                  int value = 0) const {
        std::array<char, buffer_manager_types::page_data_size> bytes{};
        auto *heap_page =
            reinterpret_cast<heap_page_types::HeapPage *>(bytes.data());
        heap_page->initialize();
        heap_page->page_header.lsn = lsn;
        std::memcpy(heap_page->data + offset, &value, sizeof(value));
        return bytes;
    }

    void replace_heap_page_and_reopen(
        const std::array<char, buffer_manager_types::page_data_size> &bytes) {
        wal.reset();
        buffer_pool.reset();
        {
            std::ofstream output(heap_filename,
                                 std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(output.is_open());
            output.write(bytes.data(), bytes.size());
            ASSERT_TRUE(output.good());
        }
        open_database();
    }

    std::array<char, buffer_manager_types::page_data_size>
    read_heap_page() const {
        std::array<char, buffer_manager_types::page_data_size> bytes{};
        std::ifstream input(heap_filename, std::ios::binary);
        EXPECT_TRUE(input.is_open());
        input.read(bytes.data(), bytes.size());
        EXPECT_EQ(input.gcount(),
                  static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    void replace_wal_with_last_lsn(std::uint64_t lsn) {
        wal.reset();
        buffer_pool.reset();
        {
            std::ofstream output(wal_filename,
                                 std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(output.is_open());
            const std::uint64_t record_offset = sizeof(std::uint64_t);
            wal_types::WAL_entry record{};
            record.lsn = lsn;
            record.rid = make_rid(0, 0);
            record.row_size = 0;
            output.write(reinterpret_cast<const char *>(&record_offset),
                         sizeof(record_offset));
            output.write(reinterpret_cast<const char *>(&record), sizeof(record));
            ASSERT_TRUE(output.good());
        }
        open_database();
    }

    std::vector<LoggedRecord> read_records() const {
        std::ifstream input(wal_filename, std::ios::binary | std::ios::ate);
        EXPECT_TRUE(input.is_open());
        const auto end = input.tellg();
        if (end < static_cast<std::streamoff>(sizeof(std::uint64_t)))
            return {};

        input.seekg(0, std::ios::beg);
        std::uint64_t last_offset = 0;
        input.read(reinterpret_cast<char *>(&last_offset),
                   sizeof(last_offset));
        std::vector<LoggedRecord> records;
        std::uint64_t offset = sizeof(std::uint64_t);
        while (input.good() && offset <= last_offset) {
            input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            LoggedRecord record;
            input.read(reinterpret_cast<char *>(&record.header),
                       sizeof(record.header));
            if (!input.good())
                break;
            record.payload.resize(record.header.row_size);
            if (!record.payload.empty()) {
                input.read(record.payload.data(), record.payload.size());
                if (!input.good())
                    break;
            }
            offset += sizeof(record.header) + record.header.row_size;
            records.push_back(std::move(record));
        }
        return records;
    }
};

} // namespace

TEST_F(WALTest, StartsWithZeroLsnAndReplaysAnEmptyLogSafely) {
    EXPECT_EQ(wal->get_lsn(), 0u);
    EXPECT_NO_THROW(wal->ReplayTransaction());
}

TEST_F(WALTest, CommitAppendsARecordAndPersistsThePageLsn) {
    const int value = 42;
    write_int_at(0, value);
    access_methods_types::row_t row;
    row.row.emplace_back(value);

    wal->CommitTransaction(make_rid(0, sizeof(value)), row);

    EXPECT_EQ(wal->get_lsn(), 1u);
    const auto records = read_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].header.lsn, 1u);
    EXPECT_EQ(records[0].header.rid.pid, 0);
    EXPECT_EQ(records[0].header.rid.slot.slot_offset, 0);
    EXPECT_EQ(records[0].header.row_size, sizeof(value));
    int logged_value = 0;
    std::memcpy(&logged_value, records[0].payload.data(), sizeof(value));
    EXPECT_EQ(logged_value, value);

    const auto page_bytes = read_heap_page();
    const auto *page =
        reinterpret_cast<const heap_page_types::HeapPage *>(page_bytes.data());
    EXPECT_EQ(page->page_header.lsn, 1u);
}

TEST_F(WALTest, RejectsMismatchedSerializedSizeWithoutAppendingOrAdvancingLsn) {
    access_methods_types::row_t row;
    row.row.emplace_back(7);

    EXPECT_THROW(wal->CommitTransaction(make_rid(0, 8), row),
                 std::runtime_error);
    EXPECT_EQ(wal->get_lsn(), 0u);
    EXPECT_TRUE(read_records().empty());
}

TEST_F(WALTest, AppendsRecordsWithMonotonicallyIncreasingLsns) {
    access_methods_types::row_t first;
    first.row.emplace_back(10);
    access_methods_types::row_t second;
    second.row.emplace_back(20);

    write_int_at(0, 10);
    wal->CommitTransaction(make_rid(0, sizeof(int)), first);
    write_int_at(sizeof(int), 20);
    wal->CommitTransaction(make_rid(sizeof(int), sizeof(int)), second);

    const auto records = read_records();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].header.lsn, 1u);
    EXPECT_EQ(records[1].header.lsn, 2u);
    EXPECT_EQ(wal->get_lsn(), 2u);
}

TEST_F(WALTest, RestoresTheLatestLsnWhenReopened) {
    access_methods_types::row_t row;
    row.row.emplace_back(99);
    write_int_at(0, 99);
    wal->CommitTransaction(make_rid(0, sizeof(int)), row);

    WAL::WAL reopened(*buffer_pool);
    EXPECT_EQ(reopened.get_lsn(), 1u);
}

TEST_F(WALTest, ReplayRedoRestoresARecordOntoAnOlderPage) {
    access_methods_types::row_t row;
    row.row.emplace_back(1234);
    write_int_at(0, 1234);
    wal->CommitTransaction(make_rid(0, sizeof(int)), row);

    replace_heap_page_and_reopen(make_raw_page(0));
    wal->ReplayTransaction();

    const auto page_bytes = read_heap_page();
    const auto *page =
        reinterpret_cast<const heap_page_types::HeapPage *>(page_bytes.data());
    int recovered = 0;
    std::memcpy(&recovered, page->data, sizeof(recovered));
    EXPECT_EQ(recovered, 1234);
    EXPECT_EQ(page->page_header.lsn, 1u);
}

TEST_F(WALTest, ReplayPreservesMultipleUpdatesToDifferentSlotsOnTheSamePage) {
    access_methods_types::row_t first;
    first.row.emplace_back(11);
    access_methods_types::row_t second;
    second.row.emplace_back(22);

    write_int_at(0, 11);
    wal->CommitTransaction(make_rid(0, sizeof(int)), first);
    write_int_at(sizeof(int), 22);
    wal->CommitTransaction(make_rid(sizeof(int), sizeof(int)), second);

    replace_heap_page_and_reopen(make_raw_page(0));
    wal->ReplayTransaction();

    const auto page_bytes = read_heap_page();
    const auto *page =
        reinterpret_cast<const heap_page_types::HeapPage *>(page_bytes.data());
    int first_value = 0, second_value = 0;
    std::memcpy(&first_value, page->data, sizeof(first_value));
    std::memcpy(&second_value, page->data + sizeof(int), sizeof(second_value));
    EXPECT_EQ(first_value, 11);
    EXPECT_EQ(second_value, 22);
    EXPECT_EQ(page->page_header.lsn, 2u);
}

TEST_F(WALTest, ReplayIsIdempotent) {
    access_methods_types::row_t row;
    row.row.emplace_back(73);
    write_int_at(0, 73);
    wal->CommitTransaction(make_rid(0, sizeof(int)), row);

    replace_heap_page_and_reopen(make_raw_page(0));
    wal->ReplayTransaction();
    const auto first_replay = read_heap_page();

    wal->ReplayTransaction();
    const auto second_replay = read_heap_page();
    EXPECT_EQ(first_replay, second_replay);
}

TEST_F(WALTest, ReplayDoesNotOverwriteAPageWithANewerLsn) {
    access_methods_types::row_t row;
    row.row.emplace_back(1);
    write_int_at(0, 1);
    wal->CommitTransaction(make_rid(0, sizeof(int)), row);

    replace_heap_page_and_reopen(make_raw_page(9, 0, 900));
    wal->ReplayTransaction();

    const auto page_bytes = read_heap_page();
    const auto *page =
        reinterpret_cast<const heap_page_types::HeapPage *>(page_bytes.data());
    int value = 0;
    std::memcpy(&value, page->data, sizeof(value));
    EXPECT_EQ(value, 900);
    EXPECT_EQ(page->page_header.lsn, 9u);
}

TEST_F(WALTest, ReplayIgnoresATruncatedRecordPayload) {
    access_methods_types::row_t row;
    row.row.emplace_back(88);
    write_int_at(0, 88);
    wal->CommitTransaction(make_rid(0, sizeof(int)), row);

    wal.reset();
    buffer_pool.reset();
    std::filesystem::resize_file(
        wal_filename, sizeof(std::uint64_t) + sizeof(wal_types::WAL_entry));
    open_database();
    wal->ReplayTransaction();

    const auto page_bytes = read_heap_page();
    const auto *page =
        reinterpret_cast<const heap_page_types::HeapPage *>(page_bytes.data());
    int value = 0;
    std::memcpy(&value, page->data, sizeof(value));
    EXPECT_EQ(value, 88); // Existing page is left untouched on an incomplete log record.
}

TEST_F(WALTest, SerializesMixedColumnTypesIntoTheExpectedFixedWidthPayload) {
    access_methods_types::row_t row;
    const int integer_value = 17;
    const float float_value = 2.5F;
    const std::string string_value = "alpha";
    row.row.emplace_back(integer_value);
    row.row.emplace_back(float_value);
    row.row.emplace_back(string_value);

    const int payload_size =
        sizeof(integer_value) + sizeof(float_value) +
        static_cast<int>(access_methods_types::STRING_MAX_SIZE);
    wal->CommitTransaction(make_rid(0, payload_size), row);

    const auto records = read_records();
    ASSERT_EQ(records.size(), 1u);
    ASSERT_EQ(records[0].payload.size(),
              static_cast<std::size_t>(payload_size));

    int logged_integer = 0;
    float logged_float = 0;
    std::memcpy(&logged_integer, records[0].payload.data(),
                sizeof(logged_integer));
    std::memcpy(&logged_float,
                records[0].payload.data() + sizeof(logged_integer),
                sizeof(logged_float));
    EXPECT_EQ(logged_integer, integer_value);
    EXPECT_FLOAT_EQ(logged_float, float_value);
    EXPECT_EQ(std::string(records[0].payload.data() + sizeof(int) + sizeof(float)),
              string_value);
}


TEST_F(WALTest, ConcurrentCommitsAppendDistinctMonotonicLsns) {
    constexpr int record_count = 12;
    for (int i = 0; i < record_count; ++i)
        write_int_at(static_cast<std::uint16_t>(i * sizeof(int)), 100 + i);

    std::vector<std::thread> writers;
    writers.reserve(record_count);
    for (int i = 0; i < record_count; ++i) {
        writers.emplace_back([&, i] {
            access_methods_types::row_t row;
            row.row.emplace_back(100 + i);
            wal->CommitTransaction(
                make_rid(static_cast<std::uint16_t>(i * sizeof(int)), sizeof(int)),
                row);
        });
    }
    for (auto &writer : writers)
        writer.join();

    const auto records = read_records();
    ASSERT_EQ(records.size(), static_cast<std::size_t>(record_count));
    for (int i = 0; i < record_count; ++i)
        EXPECT_EQ(records[i].header.lsn, static_cast<std::uint64_t>(i + 1));
    EXPECT_EQ(wal->get_lsn(), static_cast<std::uint64_t>(record_count));
}

TEST_F(WALTest, RejectsARowThatWouldOverflowTheHeapPayload) {
    access_methods_types::row_t row;
    row.row.emplace_back(5);
    const auto invalid_offset = static_cast<std::uint16_t>(
        heap_page_types::HEAP_PAYLOAD_SIZE - 2);

    EXPECT_THROW(wal->CommitTransaction(make_rid(invalid_offset, sizeof(int)), row),
                 std::runtime_error);
    EXPECT_EQ(wal->get_lsn(), 0u);
    EXPECT_TRUE(read_records().empty());
}

TEST_F(WALTest, TruncatesStringsToTheFixedColumnWidth) {
    access_methods_types::row_t row;
    const std::string long_value(access_methods_types::STRING_MAX_SIZE + 50, 'x');
    row.row.emplace_back(long_value);

    wal->CommitTransaction(
        make_rid(0, static_cast<int>(access_methods_types::STRING_MAX_SIZE)), row);

    const auto records = read_records();
    ASSERT_EQ(records.size(), 1u);
    ASSERT_EQ(records[0].payload.size(),
              access_methods_types::STRING_MAX_SIZE);
    EXPECT_TRUE(std::all_of(records[0].payload.begin(),
                            records[0].payload.end(),
                            [](char value) { return value == 'x'; }));
}


TEST_F(WALTest, RejectsLsnOverflowBeforeAppendingAnotherRecord) {
    replace_wal_with_last_lsn(
        std::numeric_limits<std::uint16_t>::max());
    ASSERT_EQ(wal->get_lsn(),
              std::numeric_limits<std::uint16_t>::max());

    access_methods_types::row_t empty_row;
    EXPECT_THROW(wal->CommitTransaction(make_rid(0, 0), empty_row),
                 std::overflow_error);
    EXPECT_EQ(wal->get_lsn(),
              std::numeric_limits<std::uint16_t>::max());
    EXPECT_EQ(read_records().size(), 1u);
}
