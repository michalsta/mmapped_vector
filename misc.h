
#ifndef MMAPPED_VECTOR_MISC_H
#define MMAPPED_VECTOR_MISC_H

#include <unistd.h> // for close()

class RAIIFileDescriptor {
public:
    RAIIFileDescriptor(int fd) : fd(fd) {}
    ~RAIIFileDescriptor() {
        if (fd != -1) {
            close(fd);
        }
    }
    inline int get() const {
        return fd;
    }
    void reset(int new_fd) {
        if (fd != -1) {
            close(fd);
        }
        fd = new_fd;
    }
    int release() {
        int result = fd;
        fd = -1;
        return result;
    }
private:
    int fd;
};


#endif // MMAPPED_VECTOR_MISC_H