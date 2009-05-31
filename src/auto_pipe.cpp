// Copyright 2007 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// RAII for pipes

#include "auto_pipe.hpp"
#include "os_error.hpp"

#include <fcntl.h>
#include <unistd.h>

auto_pipe::auto_pipe(int reader_flags, int writer_flags)
{
    int pipe_ends[2];
    os_check_zero("pipe", pipe(pipe_ends));
    reader.reset(pipe_ends[0]);
    writer.reset(pipe_ends[1]);
    if (reader_flags)
	os_check_nonneg("fcntl", fcntl(reader.get(), F_SETFL, reader_flags));
    if (writer_flags)
	os_check_nonneg("fcntl", fcntl(writer.get(), F_SETFL, writer_flags));
}
