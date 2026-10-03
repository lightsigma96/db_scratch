#include "./wal.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

/* WAL record format : lsn-rid-tuple_state(row) */

// TODO (AI): take row here instead of operation, and in create_entry update according to that
void WAL::WAL::CommitTransaction(const heap_page_types::RID &rid, const char *operation) {
    wal_types::WAL_entry wal_e;

    wal_e.rid = rid;
    LSN++;
    snprintf(wal_e.msg, MAX_QUERY_SIZE_WAL, "%d%s\n", LSN, operation);

    buff_pool.dp_write_to_wal(wal_e);

    // acquire lock (copy Page to hold lock for less time, as dp_write_page just needs bytes)(TODO(AI) : Verify)
    buff_pool.buffer_pool_lock.lock();
    buffer_manager_types::Page p = *buff_pool.page_access(rid.pid, diskoperator_types::HEAP_PAGE);
    buff_pool.buffer_pool_lock.unlock();

    heap_page_types::HeapPage *hp = reinterpret_cast<heap_page_types::HeapPage *>(&p.page_data);
    hp->page_header.lsn           = LSN;
    buff_pool.dp_write_page(&p, diskoperator_types::HEAP_PAGE);
}

void WAL::WAL::apply_redo(const heap_page_types::RID *rid, const char *row_bytes) {
    buff_pool.buffer_pool_lock.lock();
    buffer_manager_types::Page p = *buff_pool.page_access(rid->pid, diskoperator_types::HEAP_PAGE);
    buff_pool.buffer_pool_lock.unlock();

    heap_page_types::HeapPage *hp = reinterpret_cast<heap_page_types::HeapPage *>(&p.page_data);
    memcpy(hp->data + rid->slot.slot_offset, row_bytes, rid->slot.slot_size);
}

void WAL::WAL::ReplayTransaction() {

    std::ifstream walfile("../../wal.bin");

    if (!walfile.is_open()) {
        throw std::runtime_error("ERROR IN OPENING WAL FILE");
    };

    std::string line;
    const char *delim = "-";
    while (std::getline(walfile, line, '\n')) {
        char                 *lsn = strtok(line.data(), delim);
        heap_page_types::RID *rid = reinterpret_cast<heap_page_types::RID *>(strtok(line.data(), delim));
        char                 *row = strtok(line.data(), delim);

        auto heap_page = reinterpret_cast<heap_page_types::HeapPage *>(buff_pool.page_access(rid->pid, diskoperator_types::HEAP_PAGE));

        if (heap_page->page_header.lsn < std::stoll(lsn)) {
            // redo operation
            // maybe add checks to see if schema of row matches to avoid writing incorrect types
            apply_redo(rid, row);
        }
    }
}
