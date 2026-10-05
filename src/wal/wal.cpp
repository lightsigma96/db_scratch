#include "./wal.hpp"
#include "../storage_manager/headers/disk_operator.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
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
                        const std::size_t copy_size =
                                std::min(val.size(), static_cast<std::size_t>(access_methods_types::STRING_MAX_SIZE));
                        const std::size_t old_size = bytes.size();
                        bytes.resize(old_size + access_methods_types::STRING_MAX_SIZE, '\0');
                        std::memcpy(bytes.data() + old_size, val.data(), copy_size);
                    }
                },
                value);
    }

    return bytes;
}

WAL::WAL::WAL(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods)
    : buff_pool(buff_pool), access_methods(access_methods), LSN(0) {
    const std::string wal_path = "wal.bin";
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
    const std::vector<char> row_bytes = serialize_row(row);

    if (row_bytes.size() != rid.slot.slot_size)
        throw std::runtime_error("Serialized WAL row size does not match slot size");

    ++LSN;

    Disk_operator::WALDiskRecord record{};
    record.lsn      = LSN;
    record.rid      = rid;
    record.row_size = static_cast<std::uint32_t>(row_bytes.size());

    buff_pool.dp_write_to_wal(record, row_bytes.data());

    buff_pool.buffer_pool_lock.lock();
    buffer_manager_types::Page page = *buff_pool.page_access(rid.pid, diskoperator_types::HEAP_PAGE);
    buff_pool.buffer_pool_lock.unlock();

    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page.page_data);
    heap_page->page_header.lsn = static_cast<std::uint16_t>(LSN);
    buff_pool.dp_write_page(&page, diskoperator_types::HEAP_PAGE);
}

void WAL::WAL::apply_redo(const heap_page_types::RID *rid, const char *row_bytes) {
    buff_pool.buffer_pool_lock.lock();
    buffer_manager_types::Page page = *buff_pool.page_access(rid->pid, diskoperator_types::HEAP_PAGE);
    buff_pool.buffer_pool_lock.unlock();

    auto *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(page.page_data);
    std::memcpy(heap_page->data + rid->slot.slot_offset, row_bytes, rid->slot.slot_size);
    heap_page->page_header.lsn = static_cast<std::uint16_t>(LSN);
    buff_pool.dp_write_page(&page, diskoperator_types::HEAP_PAGE);
}

void WAL::WAL::ReplayTransaction() {
    const std::string wal_path = "wal.bin";
    std::ifstream wal_file(wal_path, std::ios::binary);
    if (!wal_file.is_open())
        throw std::runtime_error("ERROR IN OPENING WAL FILE");

    std::uint64_t last_entry_offset = 0;
    wal_file.read(reinterpret_cast<char *>(&last_entry_offset), sizeof(last_entry_offset));
    if (!wal_file.good())
        return;

    std::uint64_t offset = WAL_HEADER_SIZE;
    while (offset <= last_entry_offset) {
        wal_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

        wal_types::WAL_entry record{};
        wal_file.read(reinterpret_cast<char *>(&record), sizeof(record));
        if (!wal_file.good())
            break;

        std::vector<char> row(record.row_size);
        if (record.row_size > 0) {
            wal_file.read(row.data(), record.row_size);
            if (!wal_file.good())
                break;
        }

        buff_pool.buffer_pool_lock.lock();
        buffer_manager_types::Page page = *buff_pool.page_access(record.rid.pid, diskoperator_types::HEAP_PAGE);
        buff_pool.buffer_pool_lock.unlock();

        const auto *heap_page = reinterpret_cast<const heap_page_types::HeapPage *>(page.page_data);
        if (heap_page->page_header.lsn < record.lsn)
            apply_redo(&record.rid, row.data());

        offset += sizeof(record) + record.row_size;
    }
}
