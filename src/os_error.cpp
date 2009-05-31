// Copyright 2007 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// Exception class encapsulating OS error codes

#include "os_error.hpp"

#include <cerrno>
#include <cstring>
#include <string>

os_error::os_error(std::string function, int code)
    : std::runtime_error(function.append(": ")
			 .append(std::strerror(code ? code : errno))),
      code_(code ? code : errno)
{}

void os_check_zero(const char * function, int result)
{
    if (result != 0)
	throw os_error(function);
}

int os_check_nonneg(const char * function, int result)
{
    if (result < 0)
	throw os_error(function);
    return result;
}

void os_check_error(const char * function, int code)
{
    if (code != 0)
	throw os_error(function, code);
}
