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
	int ret;
	do {
		ret = ::close(fd);
	} while (ret == -1 && errno == EINTR);
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