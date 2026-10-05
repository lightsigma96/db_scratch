#ifndef DISK_OPERATOR
#define DISK_OPERATOR

#include "types.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

class Disk_operator {
  private:
    std::filesystem::path db_path;
    std::filesystem::path index_path;
    std::filesystem::path wal_file_path;
    FILE                 *db_file;
    FILE                 *index_file;
    FILE                 *wal_file;
    int                   PAGE_SIZE;

  public:
    Disk_operator(const std::string &db_filename, const std::string &index_filename, const std::string &wal_filename, int page_size) {

        // look up if this if-else can be avoided by function which creates if not exists else just open
        if (!std::filesystem::exists(db_filename)) {
            db_file = fopen(db_filename.c_str(), "w+b");
        } else {
            db_file = fopen(db_filename.c_str(), "r+b");
        }

        if (!std::filesystem::exists(index_filename)) {
            index_file = fopen(index_filename.c_str(), "w+b");
        } else {
            index_file = fopen(index_filename.c_str(), "r+b");
        }
        // Always open WAL: daemon may create/truncate wal.bin before Disk_operator runs.
        wal_file = fopen(wal_filename.c_str(), "a+b");
        if (!wal_file) {
            throw std::runtime_error("Could not open wal file");
        }
        if (std::fseeko(wal_file, 0, SEEK_END) != 0)
            throw std::runtime_error("Could not seek WAL");
        if (ftello(wal_file) == 0) {
            const std::uint64_t invalid_offset = 0;
            if (fwrite(&invalid_offset, sizeof(invalid_offset), 1, wal_file) != 1)
                throw std::runtime_error("Could not initialize WAL header");
            fflush(wal_file);
            fsync(fileno(wal_file));
        }
        db_path       = db_filename;
        index_path    = index_filename;
        wal_file_path = wal_filename;
        PAGE_SIZE     = page_size;
    }

    void read_page(int pid, char *buffer, diskoperator_types::page_type type) {
        int offset = pid * PAGE_SIZE;

        if (type == diskoperator_types::HEAP_PAGE) {
            fseek(db_file, offset, SEEK_SET);
            if (fread(buffer, PAGE_SIZE, 1, db_file) == -1) {
                throw std::runtime_error("Could not read page");
            };
            std::cout << "\nRead a page\n";
        } else if (type == diskoperator_types::INDEX_PAGE) {
            fseek(index_file, offset, SEEK_SET);
            if (fread(buffer, PAGE_SIZE, 1, index_file) == -1) {
                throw std::runtime_error("Could not read page");
            };
            std::cout << "\nRead a page\n";
        }
    }

    void write_page(int pid, const char *write_data, diskoperator_types::page_type type) {
        FILE *file;
        switch (type) {
            case diskoperator_types::HEAP_PAGE:
                file = db_file;
            case diskoperator_types::INDEX_PAGE:
                file = index_file;
            default:
                throw std::runtime_error("ERROR : DISK_OPERATOR couldnt decide file");
        }

        int offset = pid * PAGE_SIZE;

        fseek(file, offset, SEEK_SET);
        if (fwrite(write_data, PAGE_SIZE, 1, file) == 0) {
            throw std::runtime_error("Could not write page");
        }

        /* if (dirty_bit) {
            dirty_bit = false;
        } */
        //  as every time is bad causes the same overhead for little or large data, this is called very less

        fflush(file);
        fsync(file->_fileno);
    }

    struct WALDiskRecord {
        std::uint64_t lsn;
        heap_page_types::RID rid;
        std::uint32_t row_size;
    };

    void write_to_wal(const WALDiskRecord &record, const char *row_bytes) {
        const std::uint64_t record_offset = static_cast<std::uint64_t>(ftello(wal_file));
        if (record_offset < sizeof(std::uint64_t))
            throw std::runtime_error("WAL file is missing its header");

        if (fseeko(wal_file, 0, SEEK_SET) != 0)
            throw std::runtime_error("Could not seek WAL header");

        if (fwrite(&record_offset, sizeof(record_offset), 1, wal_file) != 1)
            throw std::runtime_error("Could not update WAL header");

        if (fseeko(wal_file, 0, SEEK_END) != 0)
            throw std::runtime_error("Could not seek WAL end");

        if (fwrite(&record, sizeof(record), 1, wal_file) != 1)
            throw std::runtime_error("Could not write WAL record");

        if (record.row_size > 0 &&
            fwrite(row_bytes, record.row_size, 1, wal_file) != 1)
            throw std::runtime_error("Could not write WAL row");

        fflush(wal_file);
        if (fsync(fileno(wal_file)) != 0)
            throw std::runtime_error("Could not fsync WAL");
    }

    uintmax_t last_pid(diskoperator_types::page_type type) {
        if (type == diskoperator_types::HEAP_PAGE) {
            return std::filesystem::file_size(db_path) / PAGE_SIZE;
        } else if (type == diskoperator_types::INDEX_PAGE) {
            return std::filesystem::file_size(index_path) / PAGE_SIZE;
        }
        throw std::runtime_error("ERROR GETTING LAST PID");
    }
    ~Disk_operator() {
        if (db_file) {
            fclose(db_file);
        }
    }
};

#endif
