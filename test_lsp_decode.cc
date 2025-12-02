// test_lsp_decode.cc - tests for LspProcessClient JSON decoding/dispatch
#include <cassert>
#include <atomic>
#include <string>
#include <vector>

#include "lsp/LspProcessClient.h"
#include "lsp/Diagnostic.h"

using namespace kte::lsp;


int
main()
{
	// Create client (won't start a process for these tests)
	LspProcessClient client("/bin/echo", {});

	// 1) Numeric id response should match string key "42"
	{
		std::atomic<bool> called{false};
		client.debugAddPendingForTest("42", "dummy",
		                              [&](const nlohmann::json &result, const nlohmann::json *err) {
			                              (void) result;
			                              (void) err;
			                              called = true;
		                              });
		std::string resp = "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":null}";
		client.debugInjectMessageForTest(resp);
		assert(called.load());
	}

	// 2) String id response should resolve
	{
		std::atomic<bool> called{false};
		client.debugAddPendingForTest("abc123", "dummy",
		                              [&](const nlohmann::json &result, const nlohmann::json *err) {
			                              (void) result;
			                              (void) err;
			                              called = true;
		                              });
		std::string resp = "{\"jsonrpc\":\"2.0\",\"id\":\"abc123\",\"result\":{}}";
		client.debugInjectMessageForTest(resp);
		assert(called.load());
	}

	// 3) Diagnostics notification decoding
	{
		std::atomic<bool> diagCalled{false};
		client.setDiagnosticsHandler([&](const std::string &uri, const std::vector<Diagnostic> &d) {
			assert(uri == "file:///tmp/x.rs");
			assert(!d.empty());
			diagCalled = true;
		});
		std::string notif = R"({
          "jsonrpc":"2.0",
          "method":"textDocument/publishDiagnostics",
          "params":{
            "uri":"file:///tmp/x.rs",
            "diagnostics":[{
              "range": {"start": {"line":0, "character":1}, "end": {"line":0, "character":2}},
              "severity": 1,
              "message": "oops"
            }]
          }
        })";
		client.debugInjectMessageForTest(notif);
		assert(diagCalled.load());
	}

	// 4) ShowMessage notification should be safely handled (no crash)
	{
		std::string msg =
			"{\"jsonrpc\":\"2.0\",\"method\":\"window/showMessage\",\"params\":{\"type\":2,\"message\":\"hi\"}}";
		client.debugInjectMessageForTest(msg);
	}

	// 5) workspace/configuration request should be responded to (no crash)
	{
		std::string req = R"({
          "jsonrpc":"2.0",
          "id": 7,
          "method":"workspace/configuration",
          "params": {"items": [{"section":"x"},{"section":"y"}]}
        })";
		client.debugInjectMessageForTest(req);
	}

	// 6) Pending cap eviction: oldest request is dropped with -32001
	{
		LspProcessClient c2("/bin/echo", {});
		c2.setRunningForTest(true);
		c2.setMaxPendingForTest(2);

		std::atomic<int> drops{0};
		auto make_cb = [&](const char *tag) {
			return [&, tag](const nlohmann::json &res, const nlohmann::json *err) {
				(void) res;
				if (err && err->is_object()) {
					auto it = err->find("code");
					if (it != err->end() && it->is_number_integer() && *it == -32001) {
						// Only the oldest (first) should be dropped
						if (std::string(tag) == "first")
							drops.fetch_add(1);
					}
				}
			};
		};
		// Enqueue 3 requests; cap=2 -> first should be dropped immediately when third is added
		c2.debugSendRequestForTest("a", nlohmann::json::object(), make_cb("first"));
		c2.debugSendRequestForTest("b", nlohmann::json::object(), make_cb("second"));
		c2.debugSendRequestForTest("c", nlohmann::json::object(), make_cb("third"));
		// Allow callbacks (none are async here, drop is invoked inline after send)
		assert(drops.load() == 1);
	}

	std::puts("test_lsp_decode: OK");
	return 0;
}