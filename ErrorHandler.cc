#include "ErrorHandler.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

namespace kte {
ErrorHandler::ErrorHandler()
{
	// Determine log file path: ~/.local/state/kte/error.log
	const char *home = std::getenv("HOME");
	if (home) {
		fs::path log_dir = fs::path(home) / ".local" / "state" / "kte";
		try {
			if (!fs::exists(log_dir)) {
				fs::create_directories(log_dir);
			}
			log_file_path_ = (log_dir / "error.log").string();
			// Create the log file immediately so it exists in the state directory
			ensure_log_file();
		} catch (...) {
			// If we can't create the directory, disable file logging
			file_logging_enabled_ = false;
		}
	} else {
		// No HOME, disable file logging
		file_logging_enabled_ = false;
	}
}


ErrorHandler::~ErrorHandler()
{
	std::lock_guard<std::mutex> lg(mtx_);
	if (log_file_ && log_file_->is_open()) {
		log_file_->flush();
		log_file_->close();
	}
}


ErrorHandler &
ErrorHandler::Instance()
{
	static ErrorHandler instance;
	return instance;
}


void
ErrorHandler::Report(ErrorSeverity severity, const std::string &component,
                     const std::string &message, const std::string &context)
{
	ErrorRecord record;
	record.timestamp_ns = now_ns();
	record.severity     = severity;
	record.component    = component;
	record.message      = message;
	record.context      = context;

	{
		std::lock_guard<std::mutex> lg(mtx_);

		// Add to in-memory queue
		errors_.push_back(record);
		while (errors_.size() > 100) {
			errors_.pop_front();
		}

		++total_error_count_;
		if (severity == ErrorSeverity::Critical) {
			++critical_error_count_;
		}

		// Write to log file if enabled
		if (file_logging_enabled_) {
			write_to_log(record);
		}
	}
}


void
ErrorHandler::Info(const std::string &component, const std::string &message,
                   const std::string &context)
{
	Report(ErrorSeverity::Info, component, message, context);
}


void
ErrorHandler::Warning(const std::string &component, const std::string &message,
                      const std::string &context)
{
	Report(ErrorSeverity::Warning, component, message, context);
}


void
ErrorHandler::Error(const std::string &component, const std::string &message,
                    const std::string &context)
{
	Report(ErrorSeverity::Error, component, message, context);
}


void
ErrorHandler::Critical(const std::string &component, const std::string &message,
                       const std::string &context)
{
	Report(ErrorSeverity::Critical, component, message, context);
}


bool
ErrorHandler::HasErrors() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return !errors_.empty();
}


bool
ErrorHandler::HasCriticalErrors() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return critical_error_count_ > 0;
}


std::string
ErrorHandler::GetLastError() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	if (errors_.empty())
		return "";

	const ErrorRecord &e = errors_.back();
	std::string result   = "[" + severity_to_string(e.severity) + "] ";
	result               += e.component;
	if (!e.context.empty()) {
		result += " (" + e.context + ")";
	}
	result += ": " + e.message;
	return result;
}


std::size_t
ErrorHandler::GetErrorCount() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return total_error_count_;
}


std::size_t
ErrorHandler::GetErrorCount(ErrorSeverity severity) const
{
	std::lock_guard<std::mutex> lg(mtx_);
	std::size_t count = 0;
	for (const auto &e: errors_) {
		if (e.severity == severity) {
			++count;
		}
	}
	return count;
}


std::vector<ErrorHandler::ErrorRecord>
ErrorHandler::GetRecentErrors(std::size_t max_count) const
{
	std::lock_guard<std::mutex> lg(mtx_);
	std::vector<ErrorRecord> result;
	result.reserve(std::min(max_count, errors_.size()));

	// Return most recent first
	auto it = errors_.rbegin();
	for (std::size_t i = 0; i < max_count && it != errors_.rend(); ++i, ++it) {
		result.push_back(*it);
	}
	return result;
}


void
ErrorHandler::ClearErrors()
{
	std::lock_guard<std::mutex> lg(mtx_);
	errors_.clear();
	total_error_count_    = 0;
	critical_error_count_ = 0;
}


void
ErrorHandler::SetFileLoggingEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lg(mtx_);
	file_logging_enabled_ = enabled;
	if (!enabled && log_file_ && log_file_->is_open()) {
		log_file_->flush();
		log_file_->close();
		log_file_.reset();
	}
}


std::string
ErrorHandler::GetLogFilePath() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return log_file_path_;
}


void
ErrorHandler::write_to_log(const ErrorRecord &record)
{
	// Must be called with mtx_ held
	if (log_file_path_.empty())
		return;

	ensure_log_file();
	if (!log_file_ || !log_file_->is_open())
		return;

	// Format: [timestamp] [SEVERITY] component (context): message
	std::string timestamp = format_timestamp(record.timestamp_ns);
	std::string severity  = severity_to_string(record.severity);

	*log_file_ << "[" << timestamp << "] [" << severity << "] " << record.component;
	if (!record.context.empty()) {
		*log_file_ << " (" << record.context << ")";
	}
	*log_file_ << ": " << record.message << "\n";
	log_file_->flush();
}


void
ErrorHandler::ensure_log_file()
{
	// Must be called with mtx_ held
	if (log_file_ && log_file_->is_open())
		return;

	if (log_file_path_.empty())
		return;

	try {
		log_file_ = std::make_unique<std::ofstream>(log_file_path_,
		                                            std::ios::app | std::ios::out);
		if (!log_file_->is_open()) {
			log_file_.reset();
		}
	} catch (...) {
		log_file_.reset();
	}
}


std::string
ErrorHandler::format_timestamp(std::uint64_t timestamp_ns) const
{
	// Convert nanoseconds to time_t (seconds)
	std::time_t seconds = static_cast<std::time_t>(timestamp_ns / 1000000000ULL);
	std::uint64_t nanos = timestamp_ns % 1000000000ULL;

	std::tm tm_buf{};
#if defined(_WIN32)
	localtime_s(&tm_buf, &seconds);
#else
	localtime_r(&seconds, &tm_buf);
#endif

	std::ostringstream oss;
	oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
	oss << "." << std::setfill('0') << std::setw(3) << (nanos / 1000000ULL);
	return oss.str();
}


std::string
ErrorHandler::severity_to_string(ErrorSeverity severity) const
{
	switch (severity) {
	case ErrorSeverity::Info:
		return "INFO";
	case ErrorSeverity::Warning:
		return "WARNING";
	case ErrorSeverity::Error:
		return "ERROR";
	case ErrorSeverity::Critical:
		return "CRITICAL";
	default:
		return "UNKNOWN";
	}
}


std::uint64_t
ErrorHandler::now_ns()
{
	using namespace std::chrono;
	return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace kte
