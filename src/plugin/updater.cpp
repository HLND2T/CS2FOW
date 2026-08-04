#include "updater.h"

#include "settings.h"
#include "updater_model.h"

#include "miniz.h"
#include "picosha2.h"

#include <tier0/dbg.h>
#include <tier0/platform.h>
#include <tier1/KeyValues.h>
#include <tier1/utlbuffer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cs2fow
{

namespace
{

namespace fs = std::filesystem;

constexpr auto k_initial_check_delay = std::chrono::seconds(30);
constexpr auto k_retry_delay = std::chrono::minutes(10);
constexpr auto k_regular_check_delay = std::chrono::hours(6);
constexpr uint32_t k_maximum_release_response_size = 2 * 1024 * 1024;
constexpr uint32_t k_maximum_manifest_size = 256 * 1024;
constexpr uint32_t k_maximum_package_size = 128 * 1024 * 1024;
constexpr uint64_t k_maximum_archive_file_size = 256ull * 1024 * 1024;
constexpr uint64_t k_maximum_extracted_size = 384ull * 1024 * 1024;
constexpr uint32_t k_maximum_archive_files = 512;
constexpr const char *k_release_api =
	"https://api.github.com/repos/karola3vax/CS2FOW/releases/latest";
constexpr std::string_view k_download_prefix =
	"https://github.com/karola3vax/CS2FOW/releases/download/";

#ifdef _WIN32
constexpr const char *k_platform_name = "windows";
constexpr const char *k_asset_platform = "windows-x86_64";
constexpr const char *k_binary_extension = ".dll";
constexpr const char *k_baker_name = "cs2fow_baker.exe";
constexpr const char *k_vrf_folder = "win64";
constexpr std::array<std::string_view, 5> k_vrf_files {
	"Source2Viewer-CLI.exe",
	"TinyEXRNative.dll",
	"blake3_dotnet.dll",
	"libSkiaSharp.dll",
	"spirv-cross.dll"
};
#else
constexpr const char *k_platform_name = "linux";
constexpr const char *k_asset_platform = "linux-x86_64";
constexpr const char *k_binary_extension = ".so";
constexpr const char *k_baker_name = "cs2fow_baker";
constexpr const char *k_vrf_folder = "linux64";
constexpr std::array<std::string_view, 4> k_vrf_files {
	"Source2Viewer-CLI",
	"libblake3_dotnet.so",
	"libSkiaSharp.so",
	"libspirv-cross.so"
};
#endif

fs::path game_root()
{
	const char *root = Plat_GetGameDirectory();
	return root != nullptr && *root != '\0' ? fs::path(root) : fs::path();
}

fs::path csgo_root()
{
	const fs::path root = game_root();
	return root.empty() ? fs::path() : root / "csgo";
}

fs::path update_root(const fs::path &root)
{
	return root / "addons" / "cs2fow" / "update";
}

fs::path pending_marker(const fs::path &root)
{
	return update_root(root) / "pending.txt";
}

bool read_text_file(const fs::path &path, std::string &contents)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		return false;
	}
	std::ostringstream stream;
	stream << input.rdbuf();
	contents = stream.str();
	return input.good() || input.eof();
}

bool replace_file(const fs::path &temporary, const fs::path &destination)
{
#ifdef _WIN32
	return MoveFileExW(temporary.c_str(), destination.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
	return std::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

bool write_file_atomically(const fs::path &destination, const void *data,
	size_t size)
{
	std::error_code error;
	fs::create_directories(destination.parent_path(), error);
	if (error)
	{
		return false;
	}
	fs::path temporary = destination;
	temporary += ".tmp";
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output || (size != 0
			&& !output.write(static_cast<const char *>(data),
				static_cast<std::streamsize>(size))))
		{
			return false;
		}
		output.flush();
		if (!output)
		{
			return false;
		}
	}
	if (!replace_file(temporary, destination))
	{
		fs::remove(temporary, error);
		return false;
	}
	return true;
}

bool write_text_file_atomically(const fs::path &destination,
	std::string_view text)
{
	return write_file_atomically(destination, text.data(), text.size());
}

bool copy_file_atomically(const fs::path &source, const fs::path &destination)
{
	std::error_code error;
	fs::create_directories(destination.parent_path(), error);
	if (error)
	{
		return false;
	}
	fs::path temporary = destination;
	temporary += ".tmp";
	fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, error);
	if (error || !replace_file(temporary, destination))
	{
		fs::remove(temporary, error);
		return false;
	}
	return true;
}

std::string setting_name(std::string_view line)
{
	return std::string(update_setting_name(line));
}

std::vector<std::string> lines(std::string_view text)
{
	std::vector<std::string> result;
	for (size_t start = 0; start <= text.size();)
	{
		const size_t end = text.find('\n', start);
		std::string line(text.substr(start,
			end == std::string_view::npos ? text.size() - start : end - start));
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		result.push_back(std::move(line));
		if (end == std::string_view::npos)
		{
			break;
		}
		start = end + 1;
	}
	return result;
}

bool merge_config(const fs::path &new_template, const fs::path &current,
	std::string_view version)
{
	std::string template_text;
	if (!read_text_file(new_template, template_text))
	{
		return false;
	}

	std::string current_text;
	const bool has_current = read_text_file(current, current_text);
	std::unordered_map<std::string, std::string> saved;
	if (has_current)
	{
		for (const std::string &line : lines(current_text))
		{
			const std::string name = setting_name(line);
			if (!name.empty())
			{
				saved[name] = line;
			}
		}
		const fs::path backup = current.parent_path()
			/ ("cs2fow.cfg.before-" + std::string(version));
		std::error_code error;
		if (!fs::exists(backup, error)
			&& !copy_file_atomically(current, backup))
		{
			return false;
		}
	}

	std::string merged;
	for (const std::string &line : lines(template_text))
	{
		const std::string name = setting_name(line);
		const auto found = saved.find(name);
		merged += !name.empty() && found != saved.end() ? found->second : line;
		merged += '\n';
	}
	return write_text_file_atomically(current, merged);
}

bool copy_directory(const fs::path &source, const fs::path &destination)
{
	std::error_code error;
	if (!fs::is_directory(source, error))
	{
		return false;
	}
	for (fs::recursive_directory_iterator entry(source, error), end;
		!error && entry != end; entry.increment(error))
	{
		const fs::path relative = fs::relative(entry->path(), source, error);
		if (error)
		{
			break;
		}
		const fs::path target = destination / relative;
		if (entry->is_directory(error))
		{
			fs::create_directories(target, error);
		}
		else if (entry->is_regular_file(error)
			&& !copy_file_atomically(entry->path(), target))
		{
			return false;
		}
	}
	return !error;
}

bool write_vdf(const fs::path &root, std::string_view binary_name)
{
	const std::string contents =
		"\"Metamod Plugin\"\n{\n"
		"  \"alias\"  \"cs2fow\"\n"
		"  \"file\"   \"addons/cs2fow/bin/" + std::string(binary_name)
		+ "\"\n}\n";
	return write_text_file_atomically(
		root / "addons" / "metamod" / "cs2fow.vdf", contents);
}

bool file_digest_matches(const std::vector<uint8_t> &body,
	std::string_view expected_digest)
{
	if (!valid_sha256_digest(expected_digest))
	{
		return false;
	}
	std::string actual = picosha2::hash256_hex_string(body.begin(), body.end());
	std::transform(actual.begin(), actual.end(), actual.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	std::string wanted(expected_digest.substr(std::string_view("sha256:").size()));
	std::transform(wanted.begin(), wanted.end(), wanted.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	return actual == wanted;
}

bool extract_package(const std::vector<uint8_t> &body,
	const fs::path &destination)
{
	mz_zip_archive archive {};
	if (!mz_zip_reader_init_mem(&archive, body.data(), body.size(), 0))
	{
		return false;
	}
	const bool extracted = [&]()
	{
		const mz_uint files = mz_zip_reader_get_num_files(&archive);
		if (files == 0 || files > k_maximum_archive_files)
		{
			return false;
		}
		uint64_t total_size = 0;
		std::unordered_set<std::string> paths;
		for (mz_uint index = 0; index < files; ++index)
		{
			mz_zip_archive_file_stat file {};
			if (!mz_zip_reader_file_stat(&archive, index, &file))
			{
				return false;
			}
			if (file.m_is_directory)
			{
				continue;
			}
			const std::string path(file.m_filename);
			if (!paths.insert(path).second || !safe_update_archive_path(path)
				|| file.m_uncomp_size > k_maximum_archive_file_size
				|| total_size > k_maximum_extracted_size - file.m_uncomp_size)
			{
				return false;
			}
			total_size += file.m_uncomp_size;
			std::vector<uint8_t> contents(
				static_cast<size_t>(file.m_uncomp_size));
			uint8_t empty_file {};
			if (!mz_zip_reader_extract_to_mem(&archive, index,
				contents.empty() ? &empty_file : contents.data(),
				contents.size(), 0)
				|| !write_file_atomically(destination / path, contents.data(),
					contents.size()))
			{
				return false;
			}
		}
		return true;
	}();
	mz_zip_reader_end(&archive);
	return extracted;
}

fs::path package_binary(const fs::path &package_root)
{
	return package_root / "addons" / "cs2fow" / "bin"
		/ (std::string("cs2fow") + k_binary_extension);
}

bool complete_staged_package(const fs::path &stage)
{
	std::error_code error;
	if (!fs::is_regular_file(package_binary(stage), error)
		|| !fs::is_regular_file(stage / "addons" / "cs2fow" / "gamedata"
			/ "cs2fow.games.txt", error)
		|| !fs::is_regular_file(stage / "cfg" / "cs2fow.cfg", error)
		|| !fs::is_regular_file(stage / "addons" / "metamod"
			/ "cs2fow.vdf", error)
		|| !fs::is_regular_file(stage / "tools" / k_baker_name, error)
		|| !fs::is_regular_file(stage / "THIRD_PARTY_NOTICES", error)
		|| !fs::is_directory(stage / "licenses", error))
	{
		return false;
	}
	for (std::string_view name : k_vrf_files)
	{
		if (!fs::is_regular_file(stage / "tools" / "vrf" / k_vrf_folder
			/ name, error))
		{
			return false;
		}
	}
	return true;
}

bool set_executable(const fs::path &path)
{
#ifdef _WIN32
	(void)path;
	return true;
#else
	std::error_code error;
	fs::permissions(path,
		fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
		fs::perm_options::add, error);
	return !error;
#endif
}

void cleanup_old_update_files(const fs::path &root)
{
	if (root.empty())
	{
		return;
	}
	std::error_code error;
	const fs::path binary_directory = root / "addons" / "cs2fow" / "bin";
	for (fs::directory_iterator entry(binary_directory, error), end;
		!error && entry != end; entry.increment(error))
	{
		const std::string name = entry->path().filename().string();
		if (name.rfind("cs2fow-", 0) == 0
			&& entry->path().extension() == k_binary_extension)
		{
			fs::remove(entry->path(), error);
			if (error)
			{
				return;
			}
		}
	}
	fs::remove_all(update_root(root), error);
}

bool select_asset(KeyValues *assets, const std::string &name,
	std::string &url, std::string &digest, uint32_t &size,
	uint32_t maximum_size)
{
	url.clear();
	digest.clear();
	size = 0;
	for (KeyValues *asset = assets == nullptr ? nullptr
		: assets->GetFirstSubKey(); asset != nullptr;
		asset = asset->GetNextKey())
	{
		if (name != asset->GetString("name", ""))
		{
			continue;
		}
		url = asset->GetString("browser_download_url", "");
		digest = asset->GetString("digest", "");
		const uint64_t asset_size = asset->GetUint64("size", 0);
		size = asset_size <= maximum_size ? static_cast<uint32_t>(asset_size) : 0;
		break;
	}
	return url.rfind(k_download_prefix.data(), 0) == 0
		&& valid_sha256_digest(digest) && size != 0 && size <= maximum_size;
}

} // namespace

void updater_service::apply_pending_update()
{
	const fs::path root = csgo_root();
	if (root.empty())
	{
		return;
	}
	std::string version;
	if (!read_text_file(pending_marker(root), version))
	{
		cleanup_old_update_files(root);
		return;
	}
	while (!version.empty()
		&& std::isspace(static_cast<unsigned char>(version.back())))
	{
		version.pop_back();
	}
	if (!safe_update_version(version) || version != CS2FOW_VERSION)
	{
		return;
	}

	const fs::path stage = update_root(root) / version;
	const fs::path live_plugin = root / "addons" / "cs2fow";
	const fs::path stable_binary = live_plugin / "bin"
		/ (std::string("cs2fow") + k_binary_extension);
	const fs::path previous_binary = live_plugin / "bin"
		/ (std::string("cs2fow.previous") + k_binary_extension);
	const fs::path backup_marker = update_root(root) / version
		/ "stable-backed-up";
	std::error_code error;
	if (!complete_staged_package(stage))
	{
		Warning("[CS2FOW] The downloaded update is incomplete. "
			"The current installation was left unchanged.\n");
		return;
	}

	if (!fs::exists(backup_marker, error) && fs::exists(stable_binary, error))
	{
		if (!copy_file_atomically(stable_binary, previous_binary)
			|| !write_text_file_atomically(backup_marker, version))
		{
			Warning("[CS2FOW] The update could not back up the previous plugin "
				"binary. The current installation was left unchanged.\n");
			return;
		}
	}

	const fs::path live_baker = root / "tools" / k_baker_name;
	const fs::path live_vrf = root / "tools" / "vrf" / k_vrf_folder;
	if (!copy_file_atomically(stage / "addons" / "cs2fow" / "gamedata"
			/ "cs2fow.games.txt",
			live_plugin / "gamedata" / "cs2fow.games.txt")
		|| !copy_file_atomically(stage / "tools" / k_baker_name, live_baker)
		|| !copy_directory(stage / "tools" / "vrf" / k_vrf_folder, live_vrf)
		|| !merge_config(stage / "cfg" / "cs2fow.cfg",
			root / "cfg" / "cs2fow.cfg", version)
		|| !copy_file_atomically(stage / "THIRD_PARTY_NOTICES",
			live_plugin / "THIRD_PARTY_NOTICES")
		|| !copy_directory(stage / "licenses", live_plugin / "licenses")
		|| !copy_file_atomically(stage / "README.md", live_plugin / "README.md")
		|| !copy_file_atomically(stage / "CHANGELOG.md",
			live_plugin / "CHANGELOG.md")
		|| !copy_file_atomically(stage / "LICENSE", live_plugin / "LICENSE")
		|| !copy_file_atomically(package_binary(stage), stable_binary)
		|| !set_executable(live_baker)
		|| !set_executable(live_vrf / k_vrf_files.front())
		|| !write_vdf(root, "cs2fow"))
	{
		Warning("[CS2FOW] The downloaded update could not be installed "
			"completely. CS2FOW will retry on the next server start.\n");
		return;
	}

	fs::remove(pending_marker(root), error);
	Msg("[CS2FOW] CS2FOW %s was installed successfully. Future starts will "
		"use the normal plugin path again.\n", version.c_str());
}

void updater_service::start(server_binary_fingerprint detected_fingerprint)
{
	detected_fingerprint_ = detected_fingerprint;
	next_check_ = std::chrono::steady_clock::now() + k_initial_check_delay;
}

void updater_service::unload()
{
	cancel_request();
	if (staging_.valid())
	{
		staging_.wait();
		(void)staging_.get();
	}
	http_ = nullptr;
	steam_context_.Clear();
	next_check_ = {};
	detected_fingerprint_ = {};
	update_version_.clear();
	manifest_url_.clear();
	manifest_digest_.clear();
	manifest_size_ = 0;
	package_url_.clear();
	package_digest_.clear();
	package_size_ = 0;
	http_unavailable_warned_ = false;
	release_selection_failed_ = false;
	manual_check_ = false;
}

void updater_service::check_now()
{
	if (staging_.valid() || request_ != INVALID_HTTPREQUEST_HANDLE)
	{
		Msg("[CS2FOW] An update check is already running.\n");
		return;
	}
	const fs::path root = csgo_root();
	std::error_code error;
	if (!root.empty() && fs::exists(pending_marker(root), error))
	{
		Msg("[CS2FOW] An update is already prepared; restart the server to "
			"install it.\n");
		return;
	}
	Msg("[CS2FOW] Checking for an update now.\n");
	next_check_ = std::chrono::steady_clock::now();
	check_release();
	// Carries the manual run through the automatic path's own guards, and
	// reports the outcome once the check finishes.
	manual_check_ = request_kind_ == request_kind::release;
}

void updater_service::on_game_frame()
{
	poll_staging();
	if (staging_.valid())
	{
		return;
	}
	if (!settings::current().automatic_updates && !manual_check_)
	{
		if (request_ != INVALID_HTTPREQUEST_HANDLE)
		{
			cancel_request();
		}
		return;
	}
	if (request_ == INVALID_HTTPREQUEST_HANDLE
		&& std::chrono::steady_clock::now() >= next_check_)
	{
		check_release();
	}
}

void updater_service::check_release()
{
	if (http_ == nullptr
		&& (!steam_context_.Init()
			|| (http_ = steam_context_.SteamHTTP()) == nullptr))
	{
		if (!http_unavailable_warned_)
		{
			Msg("[CS2FOW] Automatic updates are waiting because Steam's HTTP "
				"service is not ready yet.\n");
			http_unavailable_warned_ = true;
		}
		next_check_ = std::chrono::steady_clock::now() + k_retry_delay;
		return;
	}
	http_unavailable_warned_ = false;
	request_ = http_->CreateHTTPRequest(k_EHTTPMethodGET, k_release_api);
	if (request_ == INVALID_HTTPREQUEST_HANDLE
		|| !http_->SetHTTPRequestHeaderValue(request_, "Accept",
			"application/vnd.github+json")
		|| !http_->SetHTTPRequestHeaderValue(request_, "X-GitHub-Api-Version",
			"2022-11-28")
		|| !http_->SetHTTPRequestUserAgentInfo(request_, "CS2FOW-Updater")
		|| !http_->SetHTTPRequestNetworkActivityTimeout(request_, 10)
		|| !http_->SetHTTPRequestAbsoluteTimeoutMS(request_, 20000)
		|| !http_->SetHTTPRequestRequiresVerifiedCertificate(request_, true))
	{
		retry_later("The release check could not be prepared.");
		return;
	}
	SteamAPICall_t call {};
	if (!http_->SendHTTPRequest(request_, &call))
	{
		retry_later("The release check could not be sent.");
		return;
	}
	request_kind_ = request_kind::release;
	call_result_.SetGameserverFlag();
	call_result_.Set(call, this, &updater_service::on_completed);
}

void updater_service::download_manifest()
{
	request_ = http_->CreateHTTPRequest(k_EHTTPMethodGET, manifest_url_.c_str());
	if (request_ == INVALID_HTTPREQUEST_HANDLE
		|| !http_->SetHTTPRequestUserAgentInfo(request_, "CS2FOW-Updater")
		|| !http_->SetHTTPRequestNetworkActivityTimeout(request_, 10)
		|| !http_->SetHTTPRequestAbsoluteTimeoutMS(request_, 20000)
		|| !http_->SetHTTPRequestRequiresVerifiedCertificate(request_, true))
	{
		retry_later("The compatibility manifest download could not be prepared.");
		return;
	}
	SteamAPICall_t call {};
	if (!http_->SendHTTPRequest(request_, &call))
	{
		retry_later("The compatibility manifest download could not be sent.");
		return;
	}
	request_kind_ = request_kind::manifest;
	call_result_.SetGameserverFlag();
	call_result_.Set(call, this, &updater_service::on_completed);
}

void updater_service::download_package()
{
	request_ = http_->CreateHTTPRequest(k_EHTTPMethodGET, package_url_.c_str());
	if (request_ == INVALID_HTTPREQUEST_HANDLE
		|| !http_->SetHTTPRequestUserAgentInfo(request_, "CS2FOW-Updater")
		|| !http_->SetHTTPRequestNetworkActivityTimeout(request_, 20)
		|| !http_->SetHTTPRequestAbsoluteTimeoutMS(request_, 300000)
		|| !http_->SetHTTPRequestRequiresVerifiedCertificate(request_, true))
	{
		retry_later("The update download could not be prepared.");
		return;
	}
	SteamAPICall_t call {};
	if (!http_->SendHTTPRequest(request_, &call))
	{
		retry_later("The update download could not be sent.");
		return;
	}
	request_kind_ = request_kind::package;
	call_result_.SetGameserverFlag();
	call_result_.Set(call, this, &updater_service::on_completed);
}

void updater_service::on_completed(HTTPRequestCompleted_t *result, bool failed)
{
	if (result == nullptr || result->m_hRequest != request_)
	{
		return;
	}
	if (!settings::current().automatic_updates && !manual_check_)
	{
		cancel_request();
		return;
	}
	const request_kind completed_kind = request_kind_;
	const int status = static_cast<int>(result->m_eStatusCode);
	uint32_t maximum_size = k_maximum_package_size;
	if (completed_kind == request_kind::release)
	{
		maximum_size = k_maximum_release_response_size;
	}
	else if (completed_kind == request_kind::manifest)
	{
		maximum_size = k_maximum_manifest_size;
	}
	std::vector<uint8_t> body;
	const bool response_ready = !failed && result->m_bRequestSuccessful
		&& status >= 200 && status <= 299
		&& read_response(body, maximum_size);
	cancel_request();
	if (!response_ready)
	{
		std::ostringstream message;
		message << "The automatic update request failed with HTTP " << status
			<< ".";
		retry_later(message.str().c_str());
		return;
	}
	if (completed_kind == request_kind::release)
	{
		if (!select_release(body))
		{
			if (manual_check_ && !release_selection_failed_)
			{
				Msg("[CS2FOW] CS2FOW is already up to date.\n");
			}
			manual_check_ = false;
			next_check_ = std::chrono::steady_clock::now()
				+ (release_selection_failed_ ? k_retry_delay
					: k_regular_check_delay);
			return;
		}
		download_manifest();
		return;
	}
	if (completed_kind == request_kind::manifest)
	{
		if (!select_manifest(body))
		{
			manual_check_ = false;
			next_check_ = std::chrono::steady_clock::now()
				+ (release_selection_failed_ ? k_retry_delay
					: k_regular_check_delay);
			return;
		}
		download_package();
		return;
	}
	if (completed_kind == request_kind::package)
	{
		begin_staging(std::move(body));
	}
}

void updater_service::cancel_request()
{
	call_result_.Cancel();
	if (request_ != INVALID_HTTPREQUEST_HANDLE && http_ != nullptr)
	{
		http_->ReleaseHTTPRequest(request_);
	}
	request_ = INVALID_HTTPREQUEST_HANDLE;
	request_kind_ = request_kind::none;
}

void updater_service::retry_later(const char *reason)
{
	cancel_request();
	manual_check_ = false;
	next_check_ = std::chrono::steady_clock::now() + k_retry_delay;
	Warning("[CS2FOW] %s The current version will keep running, and CS2FOW "
		"will try again later.\n",
		reason != nullptr ? reason : "The automatic update failed.");
}

bool updater_service::read_response(std::vector<uint8_t> &body,
	uint32_t maximum_size) const
{
	uint32_t size {};
	if (http_ == nullptr || !http_->GetHTTPResponseBodySize(request_, &size)
		|| size == 0 || size > maximum_size)
	{
		return false;
	}
	body.resize(size);
	return http_->GetHTTPResponseBodyData(request_, body.data(), size);
}

bool updater_service::select_release(const std::vector<uint8_t> &body)
{
	release_selection_failed_ = false;
	CUtlBuffer buffer(body.data(), static_cast<int>(body.size()),
		CUtlBuffer::READ_ONLY);
	bool parsed = false;
	KeyValues *release = KeyValuesFromJSON(&buffer, false, &parsed);
	KeyValues::AutoDelete release_owner(release);
	if (release == nullptr || !parsed)
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] GitHub returned release information CS2FOW could "
			"not read.\n");
		return false;
	}

	const std::string tag = release->GetString("tag_name", "");
	semantic_version available;
	semantic_version current;
	const bool newer = !release->GetBool("draft")
		&& !release->GetBool("prerelease")
		&& parse_semantic_version(tag, available)
		&& parse_semantic_version(CS2FOW_VERSION, current)
		&& compare_semantic_versions(available, current) > 0;
	if (!newer)
	{
		return false;
	}

	update_version_ = tag.front() == 'v' ? tag.substr(1) : tag;
	const std::string package_name = "cs2fow-" + update_version_ + "-"
		+ k_asset_platform + ".zip";
	const std::string manifest_name = "v" + update_version_
		+ "-manifest.json";
	KeyValues *assets = release->FindKey("assets", false);
	if (!select_asset(assets, package_name, package_url_, package_digest_,
			package_size_, k_maximum_package_size)
		|| !select_asset(assets, manifest_name, manifest_url_,
			manifest_digest_, manifest_size_, k_maximum_manifest_size))
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] The newest GitHub release does not contain valid "
			"%s package and compatibility-manifest assets.\n",
			k_asset_platform);
		return false;
	}
	return true;
}

bool updater_service::select_manifest(const std::vector<uint8_t> &body)
{
	release_selection_failed_ = false;
	if (body.size() != manifest_size_
		|| !file_digest_matches(body, manifest_digest_))
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] The update compatibility manifest failed its "
			"size or SHA-256 check.\n");
		return false;
	}
	CUtlBuffer buffer(body.data(), static_cast<int>(body.size()),
		CUtlBuffer::READ_ONLY);
	bool parsed = false;
	KeyValues *manifest = KeyValuesFromJSON(&buffer, false, &parsed);
	KeyValues::AutoDelete manifest_owner(manifest);
	if (manifest == nullptr || !parsed
		|| update_version_ != manifest->GetString("version", ""))
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] The update compatibility manifest could not be "
			"validated.\n");
		return false;
	}

	const std::string package_name = "cs2fow-" + update_version_ + "-"
		+ k_asset_platform + ".zip";
	KeyValues *artifacts = manifest->FindKey("artifacts", false);
	const std::string manifest_package_digest = artifacts == nullptr
		? "" : artifacts->GetString(package_name.c_str(), "");
	const std::string expected_manifest_digest = "sha256:"
		+ manifest_package_digest;
	if (!valid_sha256_digest(expected_manifest_digest)
		|| !std::equal(expected_manifest_digest.begin(),
			expected_manifest_digest.end(), package_digest_.begin(),
			package_digest_.end(),
			[](unsigned char left, unsigned char right)
			{
				return std::tolower(left) == std::tolower(right);
			}))
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] The release manifest and package SHA-256 values "
			"do not agree.\n");
		return false;
	}

	std::vector<server_binary_fingerprint> accepted;
	KeyValues *depots = manifest->FindKey("server_depots", false);
	KeyValues *platform = depots == nullptr
		? nullptr : depots->FindKey(k_platform_name, false);
	KeyValues *fingerprints = platform == nullptr
		? nullptr : platform->FindKey("accepted_binary_fingerprints", false);
	for (KeyValues *entry = fingerprints == nullptr ? nullptr
		: fingerprints->GetFirstSubKey(); entry != nullptr;
		entry = entry->GetNextKey())
	{
		const uint64_t size = entry->GetUint64("size", 0);
		uint32_t crc {};
		if (size == 0 || size > UINT32_MAX
			|| !parse_crc32(entry->GetString("crc32", ""), crc))
		{
			release_selection_failed_ = true;
			Warning("[CS2FOW] The update manifest contains an invalid %s "
				"server fingerprint.\n", k_platform_name);
			return false;
		}
		accepted.push_back({static_cast<uint32_t>(size), crc});
	}
	if (accepted.empty())
	{
		release_selection_failed_ = true;
		Warning("[CS2FOW] The update manifest contains no %s server "
			"fingerprints.\n", k_platform_name);
		return false;
	}
	if (!matches_server_binary_fingerprint(accepted,
		detected_fingerprint_.size, detected_fingerprint_.crc32))
	{
		Msg("[CS2FOW] CS2FOW %s is available, but it does not support this "
			"server binary fingerprint. The current installation was left "
			"unchanged.\n", update_version_.c_str());
		return false;
	}
	return true;
}

void updater_service::begin_staging(std::vector<uint8_t> body)
{
	const fs::path root = csgo_root();
	if (root.empty())
	{
		retry_later("The CS2 game directory is unavailable.");
		return;
	}
	try
	{
		staging_ = std::async(std::launch::async,
			[root, package = std::move(body), version = update_version_,
				digest = package_digest_, size = package_size_]() mutable
			{
				return stage_package(root, std::move(package), version, digest,
					size);
			});
	}
	catch (const std::exception &exception)
	{
		retry_later(exception.what());
	}
}

void updater_service::poll_staging()
{
	if (!staging_.valid()
		|| staging_.wait_for(std::chrono::seconds(0))
			!= std::future_status::ready)
	{
		return;
	}
	stage_result result;
	try
	{
		result = staging_.get();
	}
	catch (const std::exception &exception)
	{
		result.error = exception.what();
	}
	if (result.success)
	{
		Msg("[CS2FOW] CS2FOW %s is ready. It will be installed the next "
			"time the server starts.\n", result.version.c_str());
		manual_check_ = false;
		next_check_ = std::chrono::steady_clock::now() + k_regular_check_delay;
		return;
	}
	retry_later(result.error.empty()
		? "The downloaded update did not pass validation."
		: result.error.c_str());
}

updater_service::stage_result updater_service::stage_package(
	fs::path root, std::vector<uint8_t> body, std::string version,
	std::string expected_digest, uint32_t expected_size)
{
	stage_result result {false, std::move(version), {}};
	if (root.empty())
	{
		result.error = "The CS2 game directory became unavailable.";
		return result;
	}
	if (body.size() != expected_size
		|| !file_digest_matches(body, expected_digest)
		|| !safe_update_version(result.version))
	{
		result.error = "The downloaded update failed its size, version, or "
			"SHA-256 check.";
		return result;
	}

	const fs::path stage = update_root(root) / result.version;
	std::error_code error;
	fs::remove_all(stage, error);
	if (error || !extract_package(body, stage)
		|| !complete_staged_package(stage))
	{
		result.error = "The downloaded update archive is unsafe, incomplete, "
			"or could not be staged.";
		return result;
	}

	const fs::path update_binary = root / "addons" / "cs2fow" / "bin"
		/ (std::string("cs2fow-update") + k_binary_extension);
	if (!copy_file_atomically(package_binary(stage), update_binary)
		|| !write_text_file_atomically(pending_marker(root),
			result.version + "\n")
		|| !write_vdf(root, "cs2fow-update"))
	{
		result.error = "The verified update could not prepare its restart "
			"marker and bootstrap binary.";
		return result;
	}
	result.success = true;
	return result;
}

} // namespace cs2fow
