// Copyright 2007 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// Exception class encapsulating OS error codes

#ifndef INC_OS_ERROR_HPP
#define INC_OS_ERROR_HPP

#include <stdexcept>

// Exception wrapper for error numbers used in errno and return values
// of some functions.

class os_error : public std::runtime_error
{
public:
    explicit os_error(std::string call, int code = 0);
    int get_code() { return code_; }
private:
    int code_;
};

// Throw os_error if the argument is not zero.
void os_check_zero(const char *, int);

// Throw os_error if the argument is negative; otherwise return the argument.
int os_check_nonneg(const char *, int);

// Throw os_error with the given code if it is not zero.
void os_check_error(const char *, int);

#endif // !defined(INC_OS_ERROR_HPP)
