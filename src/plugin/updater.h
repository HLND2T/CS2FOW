#pragma once

#include "runtime_compatibility_model.h"

#undef snprintf
#include <steam/steam_gameserver.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace cs2fow
{

class updater_service
{
public:
	static void apply_pending_update();

	void start(server_binary_fingerprint detected_fingerprint);
	void unload();
	void on_game_frame();
	void check_now();

private:
	enum class request_kind
	{
		none,
		release,
		manifest,
		package,
	};

	struct stage_result
	{
		bool success {};
		std::string version;
		std::string error;
	};

	void check_release();
	void download_manifest();
	void download_package();
	void on_completed(HTTPRequestCompleted_t *result, bool failed);
	void cancel_request();
	void retry_later(const char *reason);
	bool read_response(std::vector<std::uint8_t> &body,
		std::uint32_t maximum_size) const;
	bool select_release(const std::vector<std::uint8_t> &body);
	bool select_manifest(const std::vector<std::uint8_t> &body);
	void begin_staging(std::vector<std::uint8_t> body);
	void poll_staging();
	static stage_result stage_package(std::filesystem::path root,
		std::vector<std::uint8_t> body,
		std::string version, std::string expected_digest,
		std::uint32_t expected_size);

	CSteamGameServerAPIContext steam_context_;
	ISteamHTTP *http_ {};
	CCallResult<updater_service, HTTPRequestCompleted_t> call_result_;
	HTTPRequestHandle request_ {INVALID_HTTPREQUEST_HANDLE};
	std::chrono::steady_clock::time_point next_check_;
	request_kind request_kind_ {request_kind::none};
	server_binary_fingerprint detected_fingerprint_;
	std::string update_version_;
	std::string manifest_url_;
	std::string manifest_digest_;
	std::uint32_t manifest_size_ {};
	std::string package_url_;
	std::string package_digest_;
	std::uint32_t package_size_ {};
	std::future<stage_result> staging_;
	bool http_unavailable_warned_ {};
	bool release_selection_failed_ {};
	bool manual_check_ {};
};

} // namespace cs2fow
