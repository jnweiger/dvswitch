// Copyright 2005 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// Automatic cleanup of file descriptors

#ifndef INC_AUTO_FD_HPP
#define INC_AUTO_FD_HPP

#include "auto_handle.hpp"

#include <cassert>

#include <unistd.h>

struct auto_fd_closer
{
    void operator()(int fd) const
	{
	    if (fd >= 0)
	    {
		int result = close(fd);
		assert(result == 0);
	    }
	}
};
struct auto_fd_factory
{
    int operator()() const { return -1; }
};
typedef auto_handle<int, auto_fd_closer, auto_fd_factory> auto_fd;

#endif // !INC_AUTO_FD_HPP
