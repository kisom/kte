/*
 * LspProcessClient.cc - process-based LSP client (Phase 1 minimal)
 */
#include "LspProcessClient.h"

#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <thread>

#include "json.h"

namespace kte::lsp {
LspProcessClient::LspProcessClient(std::string serverCommand, std::vector<std::string> serverArgs)
	: command_(std::move(serverCommand)), args_(std::move(serverArgs)), transport_(new JsonRpcTransport())
{
	if (const char *dbg = std::getenv("KTE_LSP_DEBUG"); dbg && *dbg) {
		debug_ = true;
	}
	if (const char *to = std::getenv("KTE_LSP_REQ_TIMEOUT_MS"); to && *to) {
		char *end   = nullptr;
		long long v = std::strtoll(to, &end, 10);
		if (end && *end == '\0' && v >= 0) {
			requestTimeoutMs_ = v;
		}
	}
	if (const char *mp = std::getenv("KTE_LSP_MAX_PENDING"); mp && *mp) {
		char *end   = nullptr;
		long long v = std::strtoll(mp, &end, 10);
		if (end && *end == '\0' && v >= 0) {
			maxPending_ = static_cast<size_t>(v);
		}
	}
}


LspProcessClient::~LspProcessClient()
{
	shutdown();
}


bool
LspProcessClient::spawnServerProcess()
{
	int toChild[2]; // parent writes toChild[1] -> child's stdin
	int fromChild[2]; // child writes fromChild[1] -> parent's stdout reader
	if (pipe(toChild) != 0) {
		if (debug_)
			std::fprintf(stderr, "[kte][lsp] pipe(toChild) failed: %s\n", std::strerror(errno));
		return false;
	}
	if (pipe(fromChild) != 0) {
		::close(toChild[0]);
		::close(toChild[1]);
		if (debug_)
			std::fprintf(stderr, "[kte][lsp] pipe(fromChild) failed: %s\n", std::strerror(errno));
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		// fork failed
		::close(toChild[0]);
		::close(toChild[1]);
		::close(fromChild[0]);
		::close(fromChild[1]);
		if (debug_)
			std::fprintf(stderr, "[kte][lsp] fork failed: %s\n", std::strerror(errno));
		return false;
	}
	if (pid == 0) {
		// Child: set up stdio
		::dup2(toChild[0], STDIN_FILENO);
		::dup2(fromChild[1], STDOUT_FILENO);
		// Close extra fds
		::close(toChild[0]);
		::close(toChild[1]);
		::close(fromChild[0]);
		::close(fromChild[1]);
		// Build argv
		std::vector<char *> argv;
		argv.push_back(const_cast<char *>(command_.c_str()));
		for (auto &s: args_)
			argv.push_back(const_cast<char *>(s.c_str()));
		argv.push_back(nullptr);
		// Exec
		execvp(command_.c_str(), argv.data());
		// If exec fails
		// Note: in child; cannot easily log to parent. Attempt to write to stderr.
		std::fprintf(stderr, "[kte][lsp] execvp failed for '%s': %s\n", command_.c_str(), std::strerror(errno));
		_exit(127);
	}

	// Parent: keep ends
	childPid_ = pid;
	outFd_    = toChild[1]; // write to child's stdin
	inFd_     = fromChild[0]; // read from child's stdout
	// Close the other ends we don't use
	::close(toChild[0]);
	::close(fromChild[1]);
	// Set CLOEXEC on our fds
	fcntl(outFd_, F_SETFD, FD_CLOEXEC);
	fcntl(inFd_, F_SETFD, FD_CLOEXEC);
	if (debug_) {
		std::ostringstream oss;
		oss << command_;
		for (const auto &a: args_) {
			oss << ' ' << a;
		}
		const char *pathEnv = std::getenv("PATH");
		std::fprintf(stderr, "[kte][lsp] spawned pid=%d argv=[%s] inFd=%d outFd=%d PATH=%s\n",
		             static_cast<int>(childPid_), oss.str().c_str(), inFd_, outFd_,
		             pathEnv ? pathEnv : "<null>");
	}
	transport_->connect(inFd_, outFd_);
	return true;
}


void
LspProcessClient::terminateProcess()
{
	if (outFd_ >= 0) {
		::close(outFd_);
		outFd_ = -1;
	}
	if (inFd_ >= 0) {
		::close(inFd_);
		inFd_ = -1;
	}
	if (childPid_ > 0) {
		// Try to wait non-blocking; if still running, send SIGTERM
		int status = 0;
		pid_t r    = waitpid(childPid_, &status, WNOHANG);
		if (r == 0) {
			// still running
			kill(childPid_, SIGTERM);
			waitpid(childPid_, &status, 0);
		}
		childPid_ = -1;
	}
}


void
LspProcessClient::sendInitialize(const std::string &rootPath)
{
	int idNum            = nextRequestIntId_++;
	pendingInitializeId_ = std::to_string(idNum);
	nlohmann::json j;
	j["jsonrpc"] = "2.0";
	j["id"]      = idNum;
	j["method"]  = "initialize";
	nlohmann::json params;
	params["processId"] = static_cast<int>(getpid());
	params["rootUri"]   = toFileUri(rootPath);
	// Minimal client capabilities for now
	nlohmann::json caps;
	caps["textDocument"]["synchronization"]["didSave"] = true;
	params["capabilities"]                             = std::move(caps);
	j["params"]                                        = std::move(params);
	transport_->send("initialize", j.dump());
}


bool
LspProcessClient::initialize(const std::string &rootPath)
{
	if (running_)
		return true;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] initialize: rootPath=%s\n", rootPath.c_str());
	if (!spawnServerProcess())
		return false;
	running_ = true;
	sendInitialize(rootPath);
	startReader();
	startTimeoutWatchdog();
	return true;
}


void
LspProcessClient::shutdown()
{
	if (!running_)
		return;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] shutdown\n");
	// Send shutdown request then exit notification (best-effort)
	int id = nextRequestIntId_++;
	{
		nlohmann::json j;
		j["jsonrpc"] = "2.0";
		j["id"]      = id;
		j["method"]  = "shutdown";
		transport_->send("shutdown", j.dump());
	}
	{
		nlohmann::json j;
		j["jsonrpc"] = "2.0";
		j["method"]  = "exit";
		transport_->send("exit", j.dump());
	}
	// Close pipes to unblock reader, then join thread, then ensure child is gone
	terminateProcess();
	stopReader();
	stopTimeoutWatchdog();
	// Clear any pending callbacks
	{
		std::lock_guard<std::mutex> lk(pendingMutex_);
		pending_.clear();
		pendingOrder_.clear();
	}
	running_ = false;
}


void
LspProcessClient::didOpen(const std::string &uri, const std::string &languageId,
                          int version, const std::string &text)
{
	if (!running_)
		return;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] -> didOpen uri=%s lang=%s version=%d bytes=%zu\n",
		             uri.c_str(), languageId.c_str(), version, text.size());
	nlohmann::json j;
	j["jsonrpc"]                              = "2.0";
	j["method"]                               = "textDocument/didOpen";
	j["params"]["textDocument"]["uri"]        = uri;
	j["params"]["textDocument"]["languageId"] = languageId;
	j["params"]["textDocument"]["version"]    = version;
	j["params"]["textDocument"]["text"]       = text;
	transport_->send("textDocument/didOpen", j.dump());
}


void
LspProcessClient::didChange(const std::string &uri, int version,
                            const std::vector<TextDocumentContentChangeEvent> &changes)
{
	if (!running_)
		return;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] -> didChange uri=%s version=%d changes=%zu\n",
		             uri.c_str(), version, changes.size());
	// Phase 1: send full or ranged changes using proper JSON construction
	nlohmann::json j;
	j["jsonrpc"]                           = "2.0";
	j["method"]                            = "textDocument/didChange";
	j["params"]["textDocument"]["uri"]     = uri;
	j["params"]["textDocument"]["version"] = version;
	auto &arr                              = j["params"]["contentChanges"];
	arr                                    = nlohmann::json::array();
	for (const auto &ch: changes) {
		nlohmann::json c;
		if (ch.range.has_value()) {
			c["range"]["start"]["line"]      = ch.range->start.line;
			c["range"]["start"]["character"] = ch.range->start.character;
			c["range"]["end"]["line"]        = ch.range->end.line;
			c["range"]["end"]["character"]   = ch.range->end.character;
		}
		c["text"] = ch.text;
		arr.push_back(std::move(c));
	}
	transport_->send("textDocument/didChange", j.dump());
}


void
LspProcessClient::didClose(const std::string &uri)
{
	if (!running_)
		return;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] -> didClose uri=%s\n", uri.c_str());
	nlohmann::json j;
	j["jsonrpc"]                       = "2.0";
	j["method"]                        = "textDocument/didClose";
	j["params"]["textDocument"]["uri"] = uri;
	transport_->send("textDocument/didClose", j.dump());
}


void
LspProcessClient::didSave(const std::string &uri)
{
	if (!running_)
		return;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] -> didSave uri=%s\n", uri.c_str());
	nlohmann::json j;
	j["jsonrpc"]                       = "2.0";
	j["method"]                        = "textDocument/didSave";
	j["params"]["textDocument"]["uri"] = uri;
	transport_->send("textDocument/didSave", j.dump());
}


void
LspProcessClient::startReader()
{
	stopReader_ = false;
	reader_     = std::thread([this] {
		this->readerLoop();
	});
}


void
LspProcessClient::stopReader()
{
	stopReader_ = true;
	if (reader_.joinable()) {
		// Waking up read() by closing inFd_ is handled in terminateProcess(); ensure it’s closed first
		// Here, best-effort join with small delay
		reader_.join();
	}
}


void
LspProcessClient::readerLoop()
{
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] readerLoop start\n");
	while (!stopReader_) {
		auto msg = transport_->read();
		if (!msg.has_value()) {
			// EOF or error
			break;
		}
		handleIncoming(msg->raw);
	}
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] readerLoop end\n");
}


void
LspProcessClient::handleIncoming(const std::string &json)
{
	try {
		auto j = nlohmann::json::parse(json, nullptr, false);
		if (j.is_discarded())
			return; // malformed JSON
		// Validate jsonrpc if present
		if (auto itRpc = j.find("jsonrpc"); itRpc != j.end()) {
			if (!itRpc->is_string() || *itRpc != "2.0")
				return;
		}

		auto normalizeId = [](const nlohmann::json &idVal) -> std::string {
			if (idVal.is_string())
				return idVal.get<std::string>();
			if (idVal.is_number_integer())
				return std::to_string(idVal.get<long long>());
			return std::string();
		};

		// Handle responses (have id and no method) or server -> client requests (have id and method)
		if (auto itId = j.find("id"); itId != j.end() && !itId->is_null()) {
			const std::string respIdStr = normalizeId(*itId);

			// If it's a request from server, it will also have a method
			if (auto itMeth = j.find("method"); itMeth != j.end() && itMeth->is_string()) {
				const std::string method = *itMeth;
				if (method == "workspace/configuration") {
					// Respond with default empty settings array matching requested items length
					size_t n = 0;
					if (auto itParams = j.find("params");
						itParams != j.end() && itParams->is_object()) {
						if (auto itItems = itParams->find("items");
							itItems != itParams->end() && itItems->is_array()) {
							n = itItems->size();
						}
					}
					nlohmann::json resp;
					resp["jsonrpc"] = "2.0";
					// echo id type: if original was string, send string; else number
					if (itId->is_string())
						resp["id"] = *itId;
					else if (itId->is_number_integer())
						resp["id"] = *itId;
					nlohmann::json arr = nlohmann::json::array();
					for (size_t i = 0; i < n; ++i)
						arr.push_back(nlohmann::json::object());
					resp["result"] = std::move(arr);
					transport_->send("response", resp.dump());
					return;
				}
				if (method == "window/showMessageRequest") {
					// Best-effort respond with null result (dismiss)
					nlohmann::json resp;
					resp["jsonrpc"] = "2.0";
					if (itId->is_string())
						resp["id"] = *itId;
					else if (itId->is_number_integer())
						resp["id"] = *itId;
					resp["result"] = nullptr;
					transport_->send("response", resp.dump());
					return;
				}
				// Unknown server request: respond with MethodNotFound
				nlohmann::json err;
				err["code"]    = -32601;
				err["message"] = "Method not found";
				nlohmann::json resp;
				resp["jsonrpc"] = "2.0";
				if (itId->is_string())
					resp["id"] = *itId;
				else if (itId->is_number_integer())
					resp["id"] = *itId;
				resp["error"] = std::move(err);
				transport_->send("response", resp.dump());
				return;
			}

			// Initialize handshake special-case
			if (!pendingInitializeId_.empty() && respIdStr == pendingInitializeId_) {
				nlohmann::json init;
				init["jsonrpc"] = "2.0";
				init["method"]  = "initialized";
				init["params"]  = nlohmann::json::object();
				transport_->send("initialized", init.dump());
				pendingInitializeId_.clear();
			}
			// Dispatcher lookup
			std::function < void(const nlohmann::json &, const nlohmann::json *) > cb;
			{
				std::lock_guard<std::mutex> lk(pendingMutex_);
				auto it = pending_.find(respIdStr);
				if (it != pending_.end()) {
					cb = it->second.callback;
					if (it->second.orderIt != pendingOrder_.end()) {
						pendingOrder_.erase(it->second.orderIt);
					}
					pending_.erase(it);
				}
			}
			if (cb) {
				const nlohmann::json *errPtr = nullptr;
				const auto itErr             = j.find("error");
				if (itErr != j.end() && itErr->is_object())
					errPtr = &(*itErr);
				nlohmann::json result;
				const auto itRes = j.find("result");
				if (itRes != j.end())
					result = *itRes; // may be null
				cb(result, errPtr);
			}
			return;
		}
		const auto itMethod = j.find("method");
		if (itMethod == j.end() || !itMethod->is_string())
			return;
		const std::string method = *itMethod;
		if (method == "window/logMessage") {
			if (debug_) {
				const auto itParams = j.find("params");
				if (itParams != j.end()) {
					const auto itMsg = itParams->find("message");
					if (itMsg != itParams->end() && itMsg->is_string()) {
						std::fprintf(stderr, "[kte][lsp] logMessage: %s\n",
						             itMsg->get_ref<const std::string &>().c_str());
					}
				}
			}
			return;
		}
		if (method == "window/showMessage") {
			const auto itParams = j.find("params");
			if (debug_ &&itParams
			
			!=
			j.end() && itParams->is_object()
			)
			{
				int typ = 0;
				std::string msg;
				if (auto itm = itParams->find("message"); itm != itParams->end() && itm->is_string())
					msg  = *itm;
				if (auto ity = itParams->find("type");
					ity != itParams->end() && ity->is_number_integer())
					typ = *ity;
				std::fprintf(stderr, "[kte][lsp] showMessage(type=%d): %s\n", typ, msg.c_str());
			}
			return;
		}
		if (method != "textDocument/publishDiagnostics") {
			return;
		}
		const auto itParams = j.find("params");
		if (itParams == j.end() || !itParams->is_object())
			return;
		const auto itUri = itParams->find("uri");
		if (itUri == itParams->end() || !itUri->is_string())
			return;
		const std::string uri = *itUri;
		std::vector<Diagnostic> diags;
		const auto itDiag = itParams->find("diagnostics");
		if (itDiag != itParams->end() && itDiag->is_array()) {
			for (const auto &djson: *itDiag) {
				if (!djson.is_object())
					continue;
				Diagnostic d;
				// severity
				int sev = 3;
				if (auto itS = djson.find("severity"); itS != djson.end() && itS->is_number_integer()) {
					sev = *itS;
				}
				switch (sev) {
				case 1:
					d.severity = DiagnosticSeverity::Error;
					break;
				case 2:
					d.severity = DiagnosticSeverity::Warning;
					break;
				case 3:
					d.severity = DiagnosticSeverity::Information;
					break;
				case 4:
					d.severity = DiagnosticSeverity::Hint;
					break;
				default:
					d.severity = DiagnosticSeverity::Information;
					break;
				}
				if (auto itM = djson.find("message"); itM != djson.end() && itM->is_string()) {
					d.message = *itM;
				}
				if (auto itR = djson.find("range"); itR != djson.end() && itR->is_object()) {
					if (auto itStart = itR->find("start");
						itStart != itR->end() && itStart->is_object()) {
						if (auto itL = itStart->find("line");
							itL != itStart->end() && itL->is_number_integer()) {
							d.range.start.line = *itL;
						}
						if (auto itC = itStart->find("character");
							itC != itStart->end() && itC->is_number_integer()) {
							d.range.start.character = *itC;
						}
					}
					if (auto itEnd = itR->find("end"); itEnd != itR->end() && itEnd->is_object()) {
						if (auto itL = itEnd->find("line");
							itL != itEnd->end() && itL->is_number_integer()) {
							d.range.end.line = *itL;
						}
						if (auto itC = itEnd->find("character");
							itC != itEnd->end() && itC->is_number_integer()) {
							d.range.end.character = *itC;
						}
					}
				}
				// optional code/source
				if (auto itCode = djson.find("code"); itCode != djson.end()) {
					if (itCode->is_string())
						d.code = itCode->get<std::string>();
					else if (itCode->is_number_integer())
						d.code = std::to_string(itCode->get<int>());
				}
				if (auto itSrc = djson.find("source"); itSrc != djson.end() && itSrc->is_string()) {
					d.source = itSrc->get<std::string>();
				}
				diags.push_back(std::move(d));
			}
		}
		if (diagnosticsHandler_) {
			diagnosticsHandler_(uri, diags);
		}
	} catch (...) {
		// swallow parse errors
	}
}


int
LspProcessClient::sendRequest(const std::string &method, const nlohmann::json &params,
                              std::function<void(const nlohmann::json & result, const nlohmann::json * errorJson)> cb)
{
	if (!running_)
		return 0;
	int id = nextRequestIntId_++;
	nlohmann::json j;
	j["jsonrpc"] = "2.0";
	j["id"]      = id;
	j["method"]  = method;
	if (!params.is_null())
		j["params"] = params;
	if (debug_)
		std::fprintf(stderr, "[kte][lsp] -> request method=%s id=%d\n", method.c_str(), id);
	transport_->send(method, j.dump());
	if (cb) {
		std::function < void() > callDropped;
		{
			std::lock_guard<std::mutex> lk(pendingMutex_);
			if (maxPending_ > 0 && pending_.size() >= maxPending_) {
				// Evict oldest
				if (!pendingOrder_.empty()) {
					std::string oldestId = pendingOrder_.front();
					auto it              = pending_.find(oldestId);
					if (it != pending_.end()) {
						auto cbOld          = it->second.callback;
						std::string methOld = it->second.method;
						if (debug_) {
							std::fprintf(
								stderr,
								"[kte][lsp] dropping oldest pending id=%s method=%s (cap=%zu)\n",
								oldestId.c_str(), methOld.c_str(), maxPending_);
						}
						// Prepare drop callback to run outside lock
						callDropped = [cbOld] {
							if (cbOld) {
								nlohmann::json err;
								err["code"]    = -32001;
								err["message"] =
									"Request dropped (max pending exceeded)";
								cbOld(nlohmann::json(), &err);
							}
						};
						pending_.erase(it);
					}
					pendingOrder_.pop_front();
				}
			}
			pendingOrder_.push_back(std::to_string(id));
			auto itOrder = pendingOrder_.end();
			--itOrder;
			PendingRequest pr;
			pr.method   = method;
			pr.callback = std::move(cb);
			if (requestTimeoutMs_ > 0) {
				pr.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(
					              requestTimeoutMs_);
			}
			pr.orderIt                   = itOrder;
			pending_[std::to_string(id)] = std::move(pr);
		}
		if (callDropped)
			callDropped();
	}
	return id;
}


void
LspProcessClient::completion(const std::string &uri, Position pos, CompletionCallback cb)
{
	nlohmann::json params;
	params["textDocument"]["uri"]   = uri;
	params["position"]["line"]      = pos.line;
	params["position"]["character"] = pos.character;
	sendRequest("textDocument/completion", params,
	            [cb = std::move(cb)](const nlohmann::json &/*result*/, const nlohmann::json * /*error*/) {
		            if (cb)
			            cb();
	            });
}


void
LspProcessClient::hover(const std::string &uri, Position pos, HoverCallback cb)
{
	nlohmann::json params;
	params["textDocument"]["uri"]   = uri;
	params["position"]["line"]      = pos.line;
	params["position"]["character"] = pos.character;
	sendRequest("textDocument/hover", params,
	            [cb = std::move(cb)](const nlohmann::json &/*result*/, const nlohmann::json * /*error*/) {
		            if (cb)
			            cb();
	            });
}


void
LspProcessClient::definition(const std::string &uri, Position pos, LocationCallback cb)
{
	nlohmann::json params;
	params["textDocument"]["uri"]   = uri;
	params["position"]["line"]      = pos.line;
	params["position"]["character"] = pos.character;
	sendRequest("textDocument/definition", params,
	            [cb = std::move(cb)](const nlohmann::json &/*result*/, const nlohmann::json * /*error*/) {
		            if (cb)
			            cb();
	            });
}


bool
LspProcessClient::isRunning() const
{
	return running_;
}


std::string
LspProcessClient::getServerName() const
{
	return command_;
}


std::string
LspProcessClient::toFileUri(const std::string &path)
{
	if (path.empty())
		return std::string();
#ifdef _WIN32
	return std::string("file:/") + path;
#else
	return std::string("file://") + path;
#endif
}


void
LspProcessClient::startTimeoutWatchdog()
{
	stopTimeout_ = false;
	if (requestTimeoutMs_ <= 0)
		return;
	timeoutThread_ = std::thread([this] {
		while (!stopTimeout_) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			auto now = std::chrono::steady_clock::now();
			struct Expired {
				std::string id;
				std::string method;
				std::function<void(const nlohmann::json &, const nlohmann::json *)> cb;
			};
			std::vector<Expired> expired;
			{
				std::lock_guard<std::mutex> lk(pendingMutex_);
				for (auto it = pending_.begin(); it != pending_.end();) {
					const auto &pr = it->second;
					if (pr.deadline.time_since_epoch().count() != 0 && now >= pr.deadline) {
						expired.push_back(Expired{it->first, pr.method, pr.callback});
						if (pr.orderIt != pendingOrder_.end())
							pendingOrder_.erase(pr.orderIt);
						it = pending_.erase(it);
					} else {
						++it;
					}
				}
			}
			for (auto &kv: expired) {
				if (debug_) {
					std::fprintf(stderr, "[kte][lsp] request timeout id=%s method=%s\n",
					             kv.id.c_str(), kv.method.c_str());
				}
				if (kv.cb) {
					nlohmann::json err;
					err["code"]    = -32000;
					err["message"] = "Request timed out";
					kv.cb(nlohmann::json(), &err);
				}
			}
		}
	});
}


void
LspProcessClient::stopTimeoutWatchdog()
{
	stopTimeout_ = true;
	if (timeoutThread_.joinable())
		timeoutThread_.join();
}
} // namespace kte::lsp