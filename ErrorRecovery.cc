// ErrorRecovery.cc - Error recovery mechanisms implementation
#include "ErrorRecovery.h"
#include <mutex>

namespace kte {
CircuitBreaker::CircuitBreaker(const Config &cfg)
	: config_(cfg), state_(State::Closed), failure_count_(0), success_count_(0),
	  last_failure_time_(std::chrono::steady_clock::time_point::min()),
	  state_change_time_(std::chrono::steady_clock::now()) {}


bool
CircuitBreaker::AllowRequest()
{
	std::lock_guard<std::mutex> lg(mtx_);

	const auto now = std::chrono::steady_clock::now();

	switch (state_) {
	case State::Closed:
		// Normal operation, allow all requests
		return true;

	case State::Open: {
		// Check if timeout has elapsed to transition to HalfOpen
		const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			now - state_change_time_
		);
		if (elapsed >= config_.open_timeout) {
			TransitionTo(State::HalfOpen);
			return true; // Allow one request to test recovery
		}
		return false; // Circuit is open, reject request
	}

	case State::HalfOpen:
		// Allow limited requests to test recovery
		return true;
	}

	return false;
}


void
CircuitBreaker::RecordSuccess()
{
	std::lock_guard<std::mutex> lg(mtx_);

	switch (state_) {
	case State::Closed:
		// Reset failure count on success in normal operation
		failure_count_ = 0;
		break;

	case State::HalfOpen:
		++success_count_;
		if (success_count_ >= config_.success_threshold) {
			// Enough successes, close the circuit
			TransitionTo(State::Closed);
		}
		break;

	case State::Open:
		// Shouldn't happen (requests rejected), but handle gracefully
		break;
	}
}


void
CircuitBreaker::RecordFailure()
{
	std::lock_guard<std::mutex> lg(mtx_);

	const auto now     = std::chrono::steady_clock::now();
	last_failure_time_ = now;

	switch (state_) {
	case State::Closed:
		// Check if we need to reset the failure count (window expired)
		if (IsWindowExpired()) {
			failure_count_ = 0;
		}

		++failure_count_;
		if (failure_count_ >= config_.failure_threshold) {
			// Too many failures, open the circuit
			TransitionTo(State::Open);
		}
		break;

	case State::HalfOpen:
		// Failure during recovery test, reopen the circuit
		TransitionTo(State::Open);
		break;

	case State::Open:
		// Already open, just track the failure
		++failure_count_;
		break;
	}
}


void
CircuitBreaker::Reset()
{
	std::lock_guard<std::mutex> lg(mtx_);
	TransitionTo(State::Closed);
}


void
CircuitBreaker::TransitionTo(State new_state)
{
	if (state_ == new_state) {
		return;
	}

	state_             = new_state;
	state_change_time_ = std::chrono::steady_clock::now();

	switch (new_state) {
	case State::Closed:
		failure_count_ = 0;
		success_count_ = 0;
		break;

	case State::Open:
		success_count_ = 0;
		// Keep failure_count_ for diagnostics
		break;

	case State::HalfOpen:
		success_count_ = 0;
		// Keep failure_count_ for diagnostics
		break;
	}
}


bool
CircuitBreaker::IsWindowExpired() const
{
	if (failure_count_ == 0) {
		return false;
	}

	const auto now     = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		now - last_failure_time_
	);

	return elapsed >= config_.window;
}
} // namespace kte