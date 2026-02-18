// ErrorHandler.h - Centralized error handling and logging for kte
#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include <memory>
#include <fstream>

namespace kte {
enum class ErrorSeverity {
	Info, // Informational messages
	Warning, // Non-critical issues
	Error, // Errors that affect functionality but allow continuation
	Critical // Critical errors that may cause data loss or crashes
};

// Centralized error handler with logging and in-memory error tracking
class ErrorHandler {
public:
	struct ErrorRecord {
		std::uint64_t timestamp_ns{0};
		ErrorSeverity severity{ErrorSeverity::Error};
		std::string component; // e.g., "SwapManager", "Buffer", "main"
		std::string message;
		std::string context; // e.g., filename, buffer name, operation
	};

	// Get the global ErrorHandler instance
	static ErrorHandler &Instance();

	// Report an error with severity, component, message, and optional context
	void Report(ErrorSeverity severity, const std::string &component,
	            const std::string &message, const std::string &context = "");

	// Convenience methods for common severity levels
	void Info(const std::string &component, const std::string &message,
	          const std::string &context = "");

	void Warning(const std::string &component, const std::string &message,
	             const std::string &context = "");

	void Error(const std::string &component, const std::string &message,
	           const std::string &context = "");

	void Critical(const std::string &component, const std::string &message,
	              const std::string &context = "");

	// Query error state (thread-safe)
	bool HasErrors() const;

	bool HasCriticalErrors() const;

	std::string GetLastError() const;

	std::size_t GetErrorCount() const;

	std::size_t GetErrorCount(ErrorSeverity severity) const;

	// Get recent errors (up to max_count, most recent first)
	std::vector<ErrorRecord> GetRecentErrors(std::size_t max_count = 10) const;

	// Clear in-memory error history (does not affect log file)
	void ClearErrors();

	// Enable/disable file logging (enabled by default)
	void SetFileLoggingEnabled(bool enabled);

	// Get the path to the error log file
	std::string GetLogFilePath() const;

private:
	ErrorHandler();

	~ErrorHandler();

	// Non-copyable, non-movable
	ErrorHandler(const ErrorHandler &) = delete;

	ErrorHandler &operator=(const ErrorHandler &) = delete;

	ErrorHandler(ErrorHandler &&) = delete;

	ErrorHandler &operator=(ErrorHandler &&) = delete;

	void write_to_log(const ErrorRecord &record);

	void ensure_log_file();

	std::string format_timestamp(std::uint64_t timestamp_ns) const;

	std::string severity_to_string(ErrorSeverity severity) const;

	static std::uint64_t now_ns();

	mutable std::mutex mtx_;
	std::deque<ErrorRecord> errors_; // bounded to max 100 entries
	std::size_t total_error_count_{0};
	std::size_t critical_error_count_{0};
	bool file_logging_enabled_{true};
	std::string log_file_path_;
	std::unique_ptr<std::ofstream> log_file_;
};
} // namespace kte