/*
 * LspProcessClient.h - process-based LSP client (initial stub)
 */
#ifndef KTE_LSP_PROCESS_CLIENT_H
#define KTE_LSP_PROCESS_CLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <list>

#include "json.h"

#include "LspClient.h"
#include "JsonRpcTransport.h"

namespace kte::lsp {
class LspProcessClient : public LspClient {
public:
	LspProcessClient(std::string serverCommand, std::vector<std::string> serverArgs);

	~LspProcessClient() override;

	bool initialize(const std::string &rootPath) override;

	void shutdown() override;

	void didOpen(const std::string &uri, const std::string &languageId,
	             int version, const std::string &text) override;

	void didChange(const std::string &uri, int version,
	               const std::vector<TextDocumentContentChangeEvent> &changes) override;

	void didClose(const std::string &uri) override;

	void didSave(const std::string &uri) override;

	// Language Features (wire-up via dispatcher; minimal callbacks for now)
	void completion(const std::string &uri, Position pos,
	                CompletionCallback cb) override;

	void hover(const std::string &uri, Position pos,
	           HoverCallback cb) override;

	void definition(const std::string &uri, Position pos,
	                LocationCallback cb) override;

	bool isRunning() const override;

	std::string getServerName() const override;


	void setDiagnosticsHandler(DiagnosticsHandler h) override
	{
		diagnosticsHandler_ = std::move(h);
	}

private:
	std::string command_;
	std::vector<std::string> args_;
	std::unique_ptr<JsonRpcTransport> transport_;
	bool running_         = false;
	bool debug_           = false;
	int inFd_             = -1; // read from server (server stdout)
	int outFd_            = -1; // write to server (server stdin)
	pid_t childPid_       = -1;
	int nextRequestIntId_ = 1;
	std::string pendingInitializeId_{}; // echo exactly as sent (string form)

	// Incoming processing
	std::thread reader_;
	std::atomic<bool> stopReader_{false};
	DiagnosticsHandler diagnosticsHandler_{};

	// Simple request dispatcher: map request id -> callback
	struct PendingRequest {
		std::string method;
		// If error is present, errorJson points to it; otherwise nullptr
		std::function<void(const nlohmann::json & result, const nlohmann::json * errorJson)> callback;
		// Optional timeout
		std::chrono::steady_clock::time_point deadline{}; // epoch if no timeout
		// Order tracking for LRU eviction
		std::list<std::string>::iterator orderIt{};
	};

	std::unordered_map<std::string, PendingRequest> pending_;
	// Maintain insertion order (oldest at front)
	std::list<std::string> pendingOrder_;
	std::mutex pendingMutex_;

	// Timeout/watchdog for pending requests
	std::thread timeoutThread_;
	std::atomic<bool> stopTimeout_{false};
	int64_t requestTimeoutMs_ = 0; // 0 = disabled
	size_t maxPending_        = 0; // 0 = unlimited

	bool spawnServerProcess();

	void terminateProcess();

	static std::string toFileUri(const std::string &path);

	void sendInitialize(const std::string &rootPath);

	void startReader();

	void stopReader();

	void readerLoop();

	void handleIncoming(const std::string &json);

	// Helper to send a request with params and register a response callback
	int sendRequest(const std::string &method, const nlohmann::json &params,
	                std::function<void(const nlohmann::json & result, const nlohmann::json * errorJson)> cb);

	// Start/stop timeout thread
	void startTimeoutWatchdog();

	void stopTimeoutWatchdog();

public:
	// Test hook: inject a raw JSON message as if received from server
	void debugInjectMessageForTest(const std::string &raw)
	{
		handleIncoming(raw);
	}


	// Test hook: add a pending request entry manually
	void debugAddPendingForTest(const std::string &id, const std::string &method,
	                            std::function<void(const nlohmann::json & result,
	                            const nlohmann::json *errorJson)

	>
	cb
	)
	{
		std::lock_guard<std::mutex> lk(pendingMutex_);
		pendingOrder_.push_back(id);
		auto it = pendingOrder_.end();
		--it;
		PendingRequest pr{method, std::move(cb), {}, it};
		pending_[id] = std::move(pr);
	}


	// Test hook: override timeout
	void setRequestTimeoutMsForTest(int64_t ms)
	{
		requestTimeoutMs_ = ms;
	}


	// Test hook: set max pending
	void setMaxPendingForTest(size_t maxPending)
	{
		maxPending_ = maxPending;
	}


	// Test hook: set running flag (to allow sendRequest in tests without spawning)
	void setRunningForTest(bool r)
	{
		running_ = r;
	}


	// Test hook: send a raw request using internal machinery
	int debugSendRequestForTest(const std::string &method, const nlohmann::json &params,
	                            std::function<void(const nlohmann::json & result,
	                            const nlohmann::json *errorJson)

	>
	cb
	)
	{
		return sendRequest(method, params, std::move(cb));
	}
};
} // namespace kte::lsp

#endif // KTE_LSP_PROCESS_CLIENT_H