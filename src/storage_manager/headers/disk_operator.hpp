#ifndef DISK_OPERATOR
#define DISK_OPERATOR

#include "types.hpp"
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
    int                   fsync_flush_counter;

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
        if (!std::filesystem::exists(wal_filename)) {
            wal_file = fopen(wal_filename.c_str(), "w+b");
        } else {
            wal_file = fopen(wal_filename.c_str(), "r+b");
        }
        db_path             = db_filename;
        index_path          = index_filename;
        wal_file_path       = wal_filename;
        PAGE_SIZE           = page_size;
        fsync_flush_counter = 0;
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
        FILE *file = (type == diskoperator_types::HEAP_PAGE) ? db_file : index_file;
        switch (type) {
            case diskoperator_types::HEAP_PAGE:
                file = db_file;
            case diskoperator_types::INDEX_PAGE:
                file = index_file;
            case diskoperator_types::WAL_PAGE:
                file = wal_file;
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
        // add a simple counter on whose particular value we will flush and fsync, as every time is bad causes the same overhead for little
        // or large data

        if (type != diskoperator_types::WAL_PAGE && fsync_flush_counter % 10 == 0) {
            fflush(file);
            fsync(file->_fileno);
        } else if (type == diskoperator_types::WAL_PAGE) {
            fflush(file);
            fsync(file->_fileno);
        }
    }

    uintmax_t last_pid(diskoperator_types::page_type type) {
        if (type == diskoperator_types::HEAP_PAGE) {
            return std::filesystem::file_size(db_path) / PAGE_SIZE;
        } else if (type == diskoperator_types::INDEX_PAGE) {
            return std::filesystem::file_size(index_path) / PAGE_SIZE;
        } else if (type == diskoperator_types::WAL_PAGE) {
            return std::filesystem::file_size(wal_file_path) / PAGE_SIZE;
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
