#include "SyscallWrappers.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdlib>

namespace kte {
namespace syscall {
int
Open(const char *path, int flags, mode_t mode)
{
	int fd;
	do {
		fd = ::open(path, flags, mode);
	} while (fd == -1 && errno == EINTR);
	return fd;
}


int
Close(int fd)
{
	// Do not retry on EINTR: on Linux (and most systems) the descriptor is
	// released even when close() is interrupted, so a retry could close an
	// fd that another thread (e.g. the swap writer) has just opened.
	const int ret = ::close(fd);
	if (ret == -1 && errno == EINTR)
		return 0;
	return ret;
}


int
Fsync(int fd)
{
	int ret;
	do {
		ret = ::fsync(fd);
	} while (ret == -1 && errno == EINTR);
	return ret;
}


int
Fstat(int fd, struct stat *buf)
{
	int ret;
	do {
		ret = ::fstat(fd, buf);
	} while (ret == -1 && errno == EINTR);
	return ret;
}


int
Ftruncate(int fd, off_t length)
{
	int ret;
	do {
		ret = ::ftruncate(fd, length);
	} while (ret == -1 && errno == EINTR);
	return ret;
}


int
Fchmod(int fd, mode_t mode)
{
	int ret;
	do {
		ret = ::fchmod(fd, mode);
	} while (ret == -1 && errno == EINTR);
	return ret;
}


int
Mkstemp(char *template_str)
{
	int fd;
	do {
		fd = ::mkstemp(template_str);
	} while (fd == -1 && errno == EINTR);
	return fd;
}
} // namespace syscall
} // namespace kte