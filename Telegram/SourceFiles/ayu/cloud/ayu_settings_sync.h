#pragma once

#include "ayu/cloud/ayu_cloud_codec.h"
#include "ayu/cloud/ayu_settings_snapshot.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "rpl/variable.h"

#include <array>
#include <memory>
#include <optional>

namespace Main {
class Session;
} // namespace Main

namespace AyuCloud {

enum class SyncState {
	Disabled,
	Checking,
	Clean,
	Dirty,
	Uploading,
	RemoteNewer,
	Conflict,
	RestorePending,
	Error,
};

struct SyncStatus {
	SyncState state = SyncState::Disabled;
	uint64_t localRevision = 0;
	uint64_t remoteRevision = 0;
	uint64_t remoteUpdatedAt = 0;
	QString remoteDeviceId;
	QString details;
	std::array<Difference, 4> differences = {};
	bool hasDifferences = false;

	friend bool operator==(const SyncStatus&, const SyncStatus&) = default;
};

class SettingsSync final : public base::has_weak_ptr {
public:
	static SettingsSync &Instance();
	~SettingsSync();

	void init();
	[[nodiscard]] bool configured() const;
	[[nodiscard]] bool enabled() const;
	[[nodiscard]] bool automaticUpload() const;
	[[nodiscard]] bool syncProxies() const;
	[[nodiscard]] uint64_t accountId() const;
	[[nodiscard]] uint32_t categories() const;
	[[nodiscard]] QString deviceId() const;
	[[nodiscard]] bool pendingRestart() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] rpl::producer<bool> automaticUploadValue() const;
	[[nodiscard]] rpl::producer<bool> syncProxiesValue() const;
	[[nodiscard]] rpl::producer<uint32_t> categoriesValue() const;
	[[nodiscard]] rpl::producer<SyncStatus> statusValue() const;
	[[nodiscard]] rpl::producer<QString> manualErrors() const;
	[[nodiscard]] SyncStatus status() const;

	void setEnabled(bool value);
	void setAutomaticUpload(bool value);
	void setSyncProxies(bool value);
	void setAccountId(uint64_t value);
	void setCategories(uint32_t value);
	void checkNow(bool manual = true);
	void uploadNow(bool overwriteRemote = false);
	void restoreNow();
	void rollbackPendingRestore();
	void deleteRemote();
	void markLocalDirty();

private:
	struct UploadJob;

	SettingsSync();
	[[nodiscard]] Main::Session *session() const;
	void loadState();
	void saveState();
	bool verifyPendingRestore();
	void setStatus(SyncState state, QString details = {});
	void schedulePoll();
	void scheduleRetry();
	void inspectManifest(bool manual, bool allowUpload);
	void handleManifest(
		std::optional<Manifest> manifest,
		bool manual,
		bool allowUpload,
		uint64_t operation);
	void beginUpload(std::optional<Manifest> remote, bool overwriteRemote);
	void loadRemoteDiff(const Manifest &manifest, uint64_t operation);
	void pumpUpload();
	void commitUpload();
	void writeUploadManifest();
	void finishUpload();
	void cleanupOldGenerations(const Manifest &manifest);
	void downloadGeneration(
		const Manifest &manifest,
		Generation generation,
		bool allowFallback,
		uint64_t operation);
	void applyDownloaded(
		const Manifest &manifest,
		QByteArray canonical,
		bool previousGeneration,
		uint64_t operation);
	void fail(Error error, bool manual);
	void fail(QString details, bool manual);
	void cancelOperation();

	bool _initialized = false;
	bool _enabled = false;
	bool _automaticUpload = true;
	bool _syncProxies = false;
	bool _applying = false;
	uint64_t _accountId = 0;
	uint32_t _categories = kAllCategories;
	QString _deviceId;
	uint64_t _lastRevision = 0;
	QString _lastHash;
	QString _lastLocalHash;
	QByteArray _pendingRollback;
	bool _pendingRollbackSyncProxies = false;
	QString _pendingTargetHash;
	int _retryAttempt = 0;
	uint64_t _operation = 0;
	std::unique_ptr<class Storage> _storage;
	Main::Session *_storageSession = nullptr;
	std::unique_ptr<UploadJob> _upload;
	base::Timer _dirtyTimer;
	base::Timer _pollTimer;
	base::Timer _retryTimer;
	rpl::variable<SyncStatus> _status;
	rpl::event_stream<QString> _manualErrors;
	rpl::variable<bool> _enabledValue;
	rpl::variable<bool> _automaticUploadValue = true;
	rpl::variable<bool> _syncProxiesValue = false;
	rpl::variable<uint32_t> _categoriesValue = kAllCategories;
	rpl::lifetime _lifetime;
};

void InitSettingsSync();
void MarkSettingsDirty();

} // namespace AyuCloud
