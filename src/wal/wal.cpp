#include "./wal.hpp"
#include "../storage_manager/headers/disk_operator.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>

namespace {
constexpr std::uint64_t WAL_HEADER_SIZE = sizeof(std::uint64_t);
}

std::vector<char> WAL::WAL::serialize_row(const access_methods_types::row_t &row) {
    std::vector<char> bytes;

    for (const auto &value : row.row) {
        std::visit(
            [&bytes](auto &&val) {
                using T = std::decay_t<decltype(val)>;

                if constexpr (std::is_same_v<T, int>) {
                    const auto *ptr = reinterpret_cast<const char *>(&val);
                    bytes.insert(bytes.end(), ptr, ptr + sizeof(int));
                } else if constexpr (std::is_same_v<T, float>) {
                    const auto *ptr = reinterpret_cast<const char *>(&val);
                    bytes.insert(bytes.end(), ptr, ptr + sizeof(float));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    const std::size_t copy_size = std::min(val.size(), static_cast<std::size_t>(access_methods_types::STRING_MAX_SIZE));
                    const std::size_t old_size  = bytes.size();
                    bytes.resize(old_size + access_methods_types::STRING_MAX_SIZE, '\0');
                    std::memcpy(bytes.data() + old_size, val.data(), copy_size);
                }
            },
            value);
    }

    return bytes;
}

WAL::WAL::WAL(buffer_manager::buffer_pool &buff_pool)
    : buff_pool(buff_pool), LSN(0) {
    const std::string wal_path = "wal.bin";
    if (!std::filesystem::exists(wal_path))
        return;
    std::ifstream wal_file(wal_path, std::ios::binary | std::ios::ate);
    if (!wal_file.is_open())
        return;

    const auto file_size = static_cast<std::uint64_t>(wal_file.tellg());
    if (file_size < WAL_HEADER_SIZE)
        return;

    wal_file.seekg(0, std::ios::beg);
    std::uint64_t last_entry_offset = 0;
    wal_file.read(reinterpret_cast<char *>(&last_entry_offset), sizeof(last_entry_offset));

    if (!wal_file.good() || last_entry_offset < WAL_HEADER_SIZE || last_entry_offset >= file_size)
        return;

    wal_file.seekg(static_cast<std::streamoff>(last_entry_offset), std::ios::beg);
    wal_types::WAL_entry record{};
    wal_file.read(reinterpret_cast<char *>(&record), sizeof(record));

    if (wal_file.good())
        LSN = record.lsn;
}

void WAL::WAL::CommitTransaction(const heap_page_types::RID &rid, const access_methods_types::row_t &row) {
    std::lock_guard<std::mutex> wal_guard(wal_mutex);
    const std::vector<char> row_bytes = serialize_row(row);

    if (rid.slot.slot_size < 0 ||
        row_bytes.size() != static_cast<std::size_t>(rid.slot.slot_size))
        throw std::runtime_error("Serialized WAL row size does not match slot size");
    if (static_cast<std::size_t>(rid.slot.slot_offset) + row_bytes.size() >
        static_cast<std::size_t>(heap_page_types::HEAP_PAYLOAD_SIZE))
        throw std::runtime_error("WAL row exceeds heap page payload");

    const std::uint64_t next_lsn = LSN + 1;
    if (next_lsn > std::numeric_limits<std::uint16_t>::max())
        throw std::overflow_error("Page LSN exceeds its 16-bit on-page representation");

    wal_types::WAL_entry record{};
    record.lsn      = next_lsn;
    record.rid      = rid;
    record.row_size = static_cast<std::uint32_t>(row_bytes.size());

    // WAL is synchronously flushed before the corresponding data page.
    buff_pool.dp_write_to_wal(record, row_bytes.data());
    LSN = next_lsn;

    auto lock = std::make_unique<buffer_manager_types::BPLock>();
    lock->lock_type = buffer_manager_types::WRITE_LOCK;
    buff_pool.AcquireBufferPoolLock(lock);
    buffer_manager_types::Page *page =
        buff_pool.page_access(rid.pid, diskoperator_types::HEAP_PAGE);
    try {
        auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
        heap_page->page_header.lsn = static_cast<std::uint16_t>(next_lsn);
        buff_pool.dp_write_page(page, diskoperator_types::HEAP_PAGE);
        page->dirty_bit = false;
        buff_pool.un_pin(rid.pid, diskoperator_types::HEAP_PAGE);
        buff_pool.UnlockBufferPoolLock(lock);
    } catch (...) {
        buff_pool.un_pin(rid.pid, diskoperator_types::HEAP_PAGE);
        buff_pool.UnlockBufferPoolLock(lock);
        throw;
    }
}

void WAL::WAL::apply_redo(const heap_page_types::RID *rid, const char *row_bytes,
                          std::uint64_t record_lsn) {
    auto lock = std::make_unique<buffer_manager_types::BPLock>();
    lock->lock_type = buffer_manager_types::WRITE_LOCK;
    buff_pool.AcquireBufferPoolLock(lock);
    buffer_manager_types::Page *page =
        buff_pool.page_access(rid->pid, diskoperator_types::HEAP_PAGE);
    try {
        auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);
        if (rid->slot.slot_size > 0) {
            std::memcpy(heap_page->data + rid->slot.slot_offset, row_bytes,
                        rid->slot.slot_size);
        }
        heap_page->page_header.lsn = static_cast<std::uint16_t>(record_lsn);
        buff_pool.dp_write_page(page, diskoperator_types::HEAP_PAGE);
        page->dirty_bit = false;
        buff_pool.un_pin(rid->pid, diskoperator_types::HEAP_PAGE);
        buff_pool.UnlockBufferPoolLock(lock);
    } catch (...) {
        buff_pool.un_pin(rid->pid, diskoperator_types::HEAP_PAGE);
        buff_pool.UnlockBufferPoolLock(lock);
        throw;
    }
}

void WAL::WAL::ReplayTransaction() {
    std::lock_guard<std::mutex> wal_guard(wal_mutex);
    std::ifstream wal_file("wal.bin", std::ios::binary | std::ios::ate);
    if (!wal_file.is_open())
        throw std::runtime_error("ERROR IN OPENING WAL FILE");

    const auto end_position = wal_file.tellg();
    if (end_position < static_cast<std::streamoff>(WAL_HEADER_SIZE))
        return;
    const auto file_size = static_cast<std::uint64_t>(end_position);

    wal_file.seekg(0, std::ios::beg);
    std::uint64_t last_entry_offset = 0;
    wal_file.read(reinterpret_cast<char *>(&last_entry_offset), sizeof(last_entry_offset));
    if (!wal_file.good() || last_entry_offset < WAL_HEADER_SIZE ||
        last_entry_offset >= file_size)
        return;

    std::uint64_t offset = WAL_HEADER_SIZE;
    while (offset <= last_entry_offset) {
        if (offset > file_size ||
            sizeof(wal_types::WAL_entry) > file_size - offset)
            break;

        wal_file.clear();
        wal_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        wal_types::WAL_entry record{};
        wal_file.read(reinterpret_cast<char *>(&record), sizeof(record));
        if (!wal_file.good())
            break;

        const std::uint64_t payload_offset = offset + sizeof(record);
        if (record.row_size > file_size - payload_offset ||
            record.rid.slot.slot_size < 0 ||
            record.row_size != static_cast<std::uint32_t>(record.rid.slot.slot_size) ||
            record.lsn > std::numeric_limits<std::uint16_t>::max() ||
            static_cast<std::size_t>(record.rid.slot.slot_offset) + record.row_size >
                static_cast<std::size_t>(heap_page_types::HEAP_PAYLOAD_SIZE))
            break;

        std::vector<char> row(record.row_size);
        if (record.row_size > 0) {
            wal_file.read(row.data(), record.row_size);
            if (!wal_file.good())
                break;
        }

        bool needs_redo = false;
        {
            auto lock = std::make_unique<buffer_manager_types::BPLock>();
            lock->lock_type = buffer_manager_types::READ_LOCK;
            buff_pool.AcquireBufferPoolLock(lock);
            buffer_manager_types::Page *page =
                buff_pool.page_access(record.rid.pid, diskoperator_types::HEAP_PAGE);
            const auto *heap_page =
                reinterpret_cast<const heap_page_types::HeapPage *>(page->page_data);
            needs_redo = heap_page->page_header.lsn < record.lsn;
            buff_pool.un_pin(record.rid.pid, diskoperator_types::HEAP_PAGE);
            buff_pool.UnlockBufferPoolLock(lock);
        }

        if (needs_redo)
            apply_redo(&record.rid, row.data(), record.lsn);

        offset = payload_offset + record.row_size;
    }
}
