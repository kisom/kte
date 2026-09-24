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


void
ForEachRegexMatch(const std::string &line, const std::regex &rx,
                  const std::function<void(std::size_t, std::size_t)> &on_match)
{
	auto scan = [&] {
		for (auto it = std::sregex_iterator(line.begin(), line.end(), rx); it != std::sregex_iterator(); ++it)
			on_match(static_cast<std::size_t>(it->position()), static_cast<std::size_t>(it->length()));
	};
	// Stack use grows with match length; a few hundred bytes stays far below
	// any thread's stack for every pattern we measured.
	constexpr std::size_t kInlineLimit = 512;
	if (line.size() <= kInlineLimit)
		scan();
	else
		RunWithLargeStack(scan);
}
} // namespace kte
