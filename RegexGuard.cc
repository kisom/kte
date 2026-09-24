#include "RegexGuard.h"

#include <exception>
#include <pthread.h>

namespace kte {
namespace {
// 1 GiB supports matches spanning several million characters.
constexpr std::size_t kLargeStackBytes = std::size_t{1} << 30;

struct Job {
	const std::function<void()> *fn;
	std::exception_ptr error;
};


void *
run_job(void *arg)
{
	auto *job = static_cast<Job *>(arg);
	try {
		(*job->fn)();
	} catch (...) {
		job->error = std::current_exception();
	}
	return nullptr;
}
} // namespace


void
RunWithLargeStack(const std::function<void()> &fn)
{
	Job job{&fn, nullptr};
	pthread_attr_t attr;
	bool started = false;
	pthread_t tid{};
	if (pthread_attr_init(&attr) == 0) {
		if (pthread_attr_setstacksize(&attr, kLargeStackBytes) == 0)
			started = pthread_create(&tid, &attr, run_job, &job) == 0;
		pthread_attr_destroy(&attr);
	}
	if (!started) {
		fn();
		return;
	}
	pthread_join(tid, nullptr);
	if (job.error)
		std::rethrow_exception(job.error);
}
} // namespace kte
