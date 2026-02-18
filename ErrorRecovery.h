// ErrorRecovery.h - Error recovery mechanisms for kte
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <cerrno>

namespace kte {
// Classify errno values as transient (retryable) or permanent
inline bool
IsTransientError(int err)
{
	switch (err) {
	case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK: 
#endif
	case EBUSY:
	case EIO: // I/O error (may be transient on network filesystems)
	case ETIMEDOUT:
	case ENOSPC: // Disk full (may become available)
	case EDQUOT: // Quota exceeded (may become available)
		return true;
	default:
		return false;
	}
}


// RetryPolicy defines retry behavior for transient failures
struct RetryPolicy {
	std::size_t max_attempts{3}; // Maximum retry attempts
	std::chrono::milliseconds initial_delay{100}; // Initial delay before first retry
	double backoff_multiplier{2.0}; // Exponential backoff multiplier
	std::chrono::milliseconds max_delay{5000}; // Maximum delay between retries

	// Default policy: 3 attempts, 100ms initial, 2x backoff, 5s max
	static RetryPolicy Default()
	{
		return RetryPolicy{};
	}


	// Aggressive policy for critical operations: more attempts, faster retries
	static RetryPolicy Aggressive()
	{
		return RetryPolicy{5, std::chrono::milliseconds(50), 1.5, std::chrono::milliseconds(2000)};
	}


	// Conservative policy for non-critical operations: fewer attempts, slower retries
	static RetryPolicy Conservative()
	{
		return RetryPolicy{2, std::chrono::milliseconds(200), 2.5, std::chrono::milliseconds(10000)};
	}
};

// Retry a function with exponential backoff for transient errors
// Returns true on success, false on permanent failure or exhausted retries
// The function `fn` should return true on success, false on failure, and set errno on failure
template<typename Func>
bool
RetryOnTransientError(Func fn, const RetryPolicy &policy, std::string &err)
{
	std::size_t attempt             = 0;
	std::chrono::milliseconds delay = policy.initial_delay;

	while (attempt < policy.max_attempts) {
		++attempt;
		errno = 0;
		if (fn()) {
			return true; // Success
		}

		int saved_errno = errno;
		if (!IsTransientError(saved_errno)) {
			// Permanent error, don't retry
			return false;
		}

		if (attempt >= policy.max_attempts) {
			// Exhausted retries
			err += " (exhausted " + std::to_string(policy.max_attempts) + " retry attempts)";
			return false;
		}

		// Sleep before retry
		std::this_thread::sleep_for(delay);

		// Exponential backoff
		delay = std::chrono::milliseconds(
			static_cast<long long>(delay.count() * policy.backoff_multiplier)
		);
		if (delay > policy.max_delay) {
			delay = policy.max_delay;
		}
	}

	return false;
}


// CircuitBreaker prevents repeated attempts to failing operations
// States: Closed (normal), Open (failing, reject immediately), HalfOpen (testing recovery)
class CircuitBreaker {
public:
	enum class State {
		Closed, // Normal operation, allow all requests
		Open, // Failing, reject requests immediately
		HalfOpen // Testing recovery, allow limited requests
	};

	struct Config {
		std::size_t failure_threshold; // Failures before opening circuit
		std::chrono::seconds open_timeout; // Time before attempting recovery (Open → HalfOpen)
		std::size_t success_threshold; // Successes in HalfOpen before closing
		std::chrono::seconds window; // Time window for counting failures

		Config()
			: failure_threshold(5), open_timeout(30), success_threshold(2), window(60) {}
	};


	explicit CircuitBreaker(const Config &cfg = Config());


	// Check if operation is allowed (returns false if circuit is Open)
	bool AllowRequest();

	// Record successful operation
	void RecordSuccess();

	// Record failed operation
	void RecordFailure();

	// Get current state
	State GetState() const
	{
		return state_;
	}


	// Get failure count in current window
	std::size_t GetFailureCount() const
	{
		return failure_count_;
	}


	// Reset circuit to Closed state (for testing or manual intervention)
	void Reset();

private:
	void TransitionTo(State new_state);

	bool IsWindowExpired() const;

	Config config_;
	State state_;
	std::size_t failure_count_;
	std::size_t success_count_;
	std::chrono::steady_clock::time_point last_failure_time_;
	std::chrono::steady_clock::time_point state_change_time_;
	mutable std::mutex mtx_;
};
} // namespace kte