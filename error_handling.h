#ifndef ERROR_HANDLING_H
#define ERROR_HANDLING_H

#include <string>
#include <cstring>
#include <stdexcept>
#include <cerrno>


namespace mmapped_vector {

std::string get_error_message(const std::string& operation) {
    return operation + " failed: " + std::strerror(errno) +
           " (errno: " + std::to_string(errno) + ")";
}

inline void throw_if_error(const std::string& operation) {
    if (errno != 0) {
        throw std::runtime_error(get_error_message(operation));
    }
}

} // namespace mmaped_vector


#endif // ERROR_HANDLING_H
