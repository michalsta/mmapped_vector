/**
 * @file allocators.h
 * @brief File containing the Allocator class and its specializations.
 * @author Michał Startek
 * @version 0.1
 * @copyright Copyright (c) Michał Startek 2024
 */

#ifndef ALLOCATORS_H
#define ALLOCATORS_H

#include <cstddef>
#include <string>
#include <sys/mman.h>
#include <mutex>
#include <variant>
#include <fcntl.h>
#include <sys/stat.h>

#include "error_handling.h"



namespace mmapped_vector {

template <typename T, bool thread_safe = false>
class Allocator
{
protected:
    T* ptr;
    size_t size;
    size_t capacity;
    std::conditional_t<thread_safe, std::mutex, std::monostate> guard;
    //std::mutex guard;
public:
    Allocator();
    virtual ~Allocator();

    virtual T* resize(size_t new_size) = 0;
    size_t get_size() const;
    size_t get_capacity() const;
    T* get_ptr() const;
    
};

template <typename T, bool thread_safe> Allocator<T, thread_safe>::Allocator() : ptr(nullptr), size(0), capacity(0), guard() {};
template <typename T, bool thread_safe> Allocator<T, thread_safe>::~Allocator() {};


template<typename T, bool thread_safe> inline
size_t Allocator<T, thread_safe>::get_size() const {
    return this->size;
}

template <typename T, bool thread_safe> inline
size_t Allocator<T, thread_safe>::get_capacity() const {
    return this->capacity;
}

template <typename T, bool thread_safe> inline
T* Allocator<T, thread_safe>::get_ptr() const {
    return this->ptr;
}

template <typename T, bool thread_safe = false>
class MmapAllocator : public Allocator<T, thread_safe>
{
public:
    MmapAllocator();
    MmapAllocator(int flags);
    ~MmapAllocator() override;

    T* resize(size_t new_size) override;
};



template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::MmapAllocator() : MmapAllocator<T, thread_safe>(MAP_ANONYMOUS | MAP_SHARED) {};


template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::MmapAllocator(int flags) : Allocator<T, thread_safe>() {
    auto fun_body = [&]()
    {
        this->ptr = static_cast<T*>(mmap(nullptr, 16 * sizeof(T), PROT_READ | PROT_WRITE, flags, -1, 0));
        if (this->ptr == MAP_FAILED) {
            throw std::runtime_error("MmapAllocator: mmap failed: " + mmapped_vector::get_error_message("mmap"));
        }
        this->size = 0;
        this->capacity = 16;
    };

    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::MmapAllocator(int flags) : Allocator<T, thread_safe>() {
    std::scoped_lock lock(this->guard);
    this->ptr = static_cast<T*>(mmap(nullptr, 16 * sizeof(T), PROT_READ | PROT_WRITE, flags, -1, 0));
    if (this->ptr == MAP_FAILED) {
        throw std::runtime_error("MmapAllocator: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
    this->size = 0;
    this->capacity = 16;
}


template <typename T, bool thread_safe>
MmapAllocator<T, false>::~MmapAllocator<T, false>() {
    if (this->ptr) {
        munmap(this->ptr, this->size * sizeof(T));
        this->ptr = nullptr;
        this->size = 0;
        this->capacity = 0;
    }
}

template <typename T, bool thread_safe>
MmapAllocator<T, true>::~MmapAllocator() {
    std::scoped_lock lock(this->guard);
    if (this->ptr) {
        munmap(this->ptr, this->size * sizeof(T));
        this->ptr = nullptr;
        this->size = 0;
        this->capacity = 0;
    }
}

template <typename T, bool thread_safe>
T* MmapAllocator<T, false>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED)
        throw_if_error("mremap");
StdAllocator<T, thread_safe>::StdAllocator() : Allocator<T, thread_safe>() {ate <typename T, bool thread_safe>
T* StdAllocator<T, false>::resize(size_t new_size) {
    if (new_size == this->size) return this->ptr;

    T* new_ptr = static_cast<T*>(realloc(this->ptr, new_size * sizeof(T)));
    if (new_ptr == nullptr)MmappedFileAllocator<T, thread_safe>::MmappedFileAllocator(std::string filename) : MmappedFileAllocator(filename, O_RDWR | O_CREAT | O_TRUNC, 0666, 0) {}
};

template <typename T, bool thread_safe>
MmappedFileAllocator<T, thread_safe>::MmappedFileAllocator(std::string filename) : MmappedFileAllocator(filename, O_RDWR | O_CREAT | O_TRUNC, 0666, 0) {}


template <typename T, bool thread_safe>
MmappedFileAllocator<T, thread_safe>::MmappedFileAllocator(std::string filename, int fopen_flags, int file_permissions, int mmap_flags) : Allocator<T, thread_safe>() {
    file_descriptor = open(filename.c_str(), fopen_flags, file_permissions);
    if (file_descriptor == -1) {
        throw std::runtime_error(mmaped_vector::get_error_message("File open"));
    }

    struct stat file_stat;
    if (fstat(file_descriptor, &file_stat) == -1) {
        close(file_descriptor);
        throw std::runtime_error(mmaped_vector::get_error_message("File stat"));
    }

    this->size = file_stat.st_size / sizeof(T);
    if (this->size > 0) {
        this->ptr = static_cast<T*>(mmap(nullptr, this->size * sizeof(T), PROT_READ | PROT_WRITE, mmap_flags, file_descriptor, 0));
        if (this->ptr == MAP_FAILED) {
            close(file_descriptor);
            throw std::runtime_error(mmaped_vector::get_error_message("Memory map"));
        }
    } else {
        this->ptr = nullptr;
    }
}

} // namespace mmaped_vector

#endif // ALLOCATORS_H
