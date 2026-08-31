#include "ayu/cloud/ayu_settings_sync.h"

#include "ayu/cloud/ayu_cloud_config.h"
#include "ayu/cloud/ayu_cloud_storage.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/localstorage.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace AyuCloud {
namespace {

constexpr auto kStatePref = "ayu.cloud_sync.state";
constexpr auto kPollInterval = 15 * 60 * crl::time(1000);
constexpr auto kDirtyDelay = 2 * crl::time(1000);

QString ErrorText(const Error &error) {
	switch (error.code) {
	case ErrorCode::BotUnavailable: return u"service_unconfigured"_q;
	case ErrorCode::Network: return u"network_error"_q;
	case ErrorCode::Conflict: return u"revision_conflict"_q;
	case ErrorCode::PayloadTooLarge: return u"payload_too_large"_q;
	case ErrorCode::TooManyChunks: return u"too_many_chunks"_q;
	case ErrorCode::MissingChunk: return u"missing_chunk"_q;
	case ErrorCode::HashMismatch: return u"hash_mismatch"_q;
	case ErrorCode::UnsupportedSchema: return u"unsupported_schema"_q;
	default: return u"invalid_backup"_q;
	}
}

std::vector<QString> GenerationKeys(const Generation &generation) {
	auto result = std::vector<QString>();
	result.reserve(generation.parts);
	for (auto i = 0; i != generation.parts; ++i) {
		result.push_back(ChunkKey(generation.id, i));
	}
	return result;
}

int CategoryIndex(Category category) {
	switch (category) {
	case Category::AyuGlobal: return 0;
	case Category::TelegramGlobal: return 1;
	case Category::AccountAndChats: return 2;
	case Category::Appearance: return 3;
	}
	Unexpected("Unknown cloud settings category.");
}

bool IsCloudChunkKey(const QString &key) {
	static const auto expression = QRegularExpression(
		u"^ayw_sync_[0-9a-f]{32}_[0-9]{1,3}$"_q);
	return expression.match(key).hasMatch();
}

} // namespace

struct SettingsSync::UploadJob {
	uint64_t operation = 0;
	EncodedPayload payload;
	Manifest manifest;
	QString settingsHash;
	int next = 0;
	int active = 0;
	int completed = 0;
	bool failed = false;
};

SettingsSync &SettingsSync::Instance() {
	static SettingsSync instance;
	return instance;
}

SettingsSync::SettingsSync()
: _dirtyTimer([=] {
	const auto current = session();
	if (!current || !_enabled || _applying) {
		return;
	}
	const auto snapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies);
	if (!snapshot || snapshot->contentHash == _lastLocalHash) {
		return;
	}
	_lastLocalHash = snapshot->contentHash;
	setStatus(SyncState::Dirty);
	if (_automaticUpload) {
		inspectManifest(false, true);
	}
})
, _pollTimer([=] { inspectManifest(false, false); })
, _retryTimer([=] { inspectManifest(false, false); }) {
}

SettingsSync::~SettingsSync() = default;

void SettingsSync::init() {
	if (_initialized) {
		return;
	}
	_initialized = true;
	loadState();
	Core::App().proxyChanges(
	) | rpl::on_next([=](const auto &) {
		markLocalDirty();
	}, _lifetime);
	Core::App().domain().accountsChanges(
	) | rpl::on_next([=] {
		if (!_pendingRollback.isEmpty() && verifyPendingRestore()) {
			return;
		} else if (_enabled && session()) {
			checkNow(false);
		} else if (_enabled) {
			cancelOperation();
			_storage.reset();
			_storageSession = nullptr;
			setStatus(SyncState::Error, u"sync_account_unavailable"_q);
		}
	}, _lifetime);
	schedulePoll();
	if (!_pendingRollback.isEmpty() && !verifyPendingRestore()) {
		setStatus(SyncState::RestorePending);
	} else if (_enabled) {
		checkNow(false);
	}
}

bool SettingsSync::configured() const {
	return Config::Configured()
		&& (!session() || !session()->isTestMode());
}

bool SettingsSync::enabled() const {
	return _enabled;
}

bool SettingsSync::automaticUpload() const {
	return _automaticUpload;
}

bool SettingsSync::syncProxies() const {
	return _syncProxies;
}

uint64_t SettingsSync::accountId() const {
	return _accountId;
}

uint32_t SettingsSync::categories() const {
	return _categories;
}

QString SettingsSync::deviceId() const {
	return _deviceId;
}

bool SettingsSync::pendingRestart() const {
	return !_pendingRollback.isEmpty();
}

rpl::producer<bool> SettingsSync::enabledValue() const {
	return _enabledValue.value();
}

rpl::producer<bool> SettingsSync::automaticUploadValue() const {
	return _automaticUploadValue.value();
}

rpl::producer<bool> SettingsSync::syncProxiesValue() const {
	return _syncProxiesValue.value();
}

rpl::producer<uint32_t> SettingsSync::categoriesValue() const {
	return _categoriesValue.value();
}

rpl::producer<SyncStatus> SettingsSync::statusValue() const {
	return _status.value();
}

rpl::producer<QString> SettingsSync::manualErrors() const {
	return _manualErrors.events();
}

SyncStatus SettingsSync::status() const {
	return _status.current();
}

Main::Session *SettingsSync::session() const {
	if (!_accountId || !Core::App().domain().started()) {
		return nullptr;
	}
	for (const auto account : Core::App().domain().orderedAccounts()) {
		if (const auto result = account->maybeSession();
			result && result->userId().bare == _accountId) {
			return result;
		}
	}
	return nullptr;
}

void SettingsSync::loadState() {
	const auto raw = Core::App().settings().readPref<QByteArray>(kStatePref);
	const auto object = QJsonDocument::fromJson(raw).object();
	_enabled = object.value(u"enabled"_q).toBool();
	_automaticUpload = object.value(u"automatic"_q).toBool(true);
	_syncProxies = object.value(u"sync_proxies"_q).toBool();
	_accountId = object.value(u"account_id"_q).toString().toULongLong();
	_categories = uint32_t(object.value(u"categories"_q).toInt(kAllCategories))
		& kAllCategories;
	if (!_categories) {
		_categories = kAllCategories;
	}
	_deviceId = object.value(u"device_id"_q).toString();
	if (_deviceId.size() != 32) {
		_deviceId = RandomId();
	}
	_lastRevision = object.value(u"last_revision"_q).toString().toULongLong();
	_lastHash = object.value(u"last_hash"_q).toString();
	_lastLocalHash = object.value(u"last_local_hash"_q).toString();
	const auto pending = object.value(u"pending_restore"_q).toObject();
	_pendingRollback = QByteArray::fromBase64(
		pending.value(u"rollback"_q).toString().toLatin1());
	_pendingRollbackSyncProxies = pending.value(u"sync_proxies"_q).toBool();
	_pendingTargetHash = pending.value(u"target_hash"_q).toString();
	if (!configured()) {
		_enabled = false;
	}
	_enabledValue = _enabled;
	_automaticUploadValue = _automaticUpload;
	_syncProxiesValue = _syncProxies;
	_categoriesValue = _categories;
	saveState();
}

void SettingsSync::saveState() {
	auto object = QJsonObject{
		{ u"enabled"_q, _enabled },
		{ u"automatic"_q, _automaticUpload },
		{ u"sync_proxies"_q, _syncProxies },
		{ u"account_id"_q, QString::number(_accountId) },
		{ u"categories"_q, int(_categories) },
		{ u"device_id"_q, _deviceId },
		{ u"last_revision"_q, QString::number(_lastRevision) },
		{ u"last_hash"_q, _lastHash },
		{ u"last_local_hash"_q, _lastLocalHash },
	};
	if (!_pendingRollback.isEmpty()) {
		object.insert(u"pending_restore"_q, QJsonObject{
			{ u"rollback"_q, QString::fromLatin1(_pendingRollback.toBase64()) },
			{ u"sync_proxies"_q, _pendingRollbackSyncProxies },
			{ u"target_hash"_q, _pendingTargetHash },
		});
	}
	Core::App().settings().writePref<QByteArray>(
		kStatePref,
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool SettingsSync::verifyPendingRestore() {
	const auto current = session();
	if (!current || _pendingRollback.isEmpty()) {
		return false;
	}
	const auto snapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies);
	if (!snapshot || snapshot->contentHash != _pendingTargetHash) {
		return false;
	}
	_pendingRollback.clear();
	_pendingRollbackSyncProxies = false;
	_pendingTargetHash.clear();
	_lastLocalHash = snapshot->contentHash;
	saveState();
	setStatus(_enabled ? SyncState::Clean : SyncState::Disabled);
	return true;
}

void SettingsSync::setStatus(SyncState state, QString details) {
	auto next = _status.current();
	next.state = state;
	next.localRevision = _lastRevision;
	next.details = std::move(details);
	_status = std::move(next);
}

void SettingsSync::setEnabled(bool value) {
	if (value && !configured()) {
		setStatus(SyncState::Error, u"service_unconfigured"_q);
		return;
	}
	if (_enabled == value) {
		_enabledValue.force_assign(_enabled);
		return;
	}
	_enabled = value;
	_enabledValue = value;
	cancelOperation();
	if (_enabled && !_accountId && Core::App().domain().started()) {
		if (const auto active = Core::App().domain().active().maybeSession()) {
			_accountId = active->userId().bare;
		}
	}
	saveState();
	if (!_pendingRollback.isEmpty()) {
		setStatus(SyncState::RestorePending);
	} else if (_enabled) {
		checkNow(false);
	} else {
		setStatus(SyncState::Disabled);
	}
}

void SettingsSync::setAutomaticUpload(bool value) {
	if (_automaticUpload == value) {
		_automaticUploadValue.force_assign(_automaticUpload);
		return;
	}
	_automaticUpload = value;
	_automaticUploadValue = value;
	saveState();
	if (value && _enabled && _status.current().state == SyncState::Dirty) {
		uploadNow();
	}
}

void SettingsSync::setSyncProxies(bool value) {
	if (_syncProxies == value || !_pendingRollback.isEmpty()) {
		_syncProxiesValue.force_assign(_syncProxies);
		return;
	}
	_syncProxies = value;
	_syncProxiesValue = value;
	saveState();
	markLocalDirty();
}

void SettingsSync::setAccountId(uint64_t value) {
	if (_accountId == value || !_pendingRollback.isEmpty()) {
		return;
	}
	cancelOperation();
	_accountId = value;
	_lastRevision = 0;
	_lastHash.clear();
	_lastLocalHash.clear();
	_storage.reset();
	_storageSession = nullptr;
	saveState();
	if (_enabled) {
		checkNow(false);
	}
}

void SettingsSync::setCategories(uint32_t value) {
	value &= kAllCategories;
	if (!value || _categories == value || !_pendingRollback.isEmpty()) {
		_categoriesValue.force_assign(_categories);
		return;
	}
	_categories = value;
	_categoriesValue = value;
	saveState();
	markLocalDirty();
}

void SettingsSync::checkNow(bool manual) {
	inspectManifest(manual, false);
}

void SettingsSync::uploadNow(bool overwriteRemote) {
	if (!_enabled || !session()) {
		fail(u"sync_account_unavailable"_q, true);
		return;
	}
	const auto operation = ++_operation;
	setStatus(SyncState::Checking);
	const auto current = session();
	if (_storageSession != current) {
		_storage = std::make_unique<Storage>(current);
		_storageSession = current;
	}
	_storage->getItems({ QString::fromLatin1(kManifestKey) }, [=](auto result) {
		if (operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), true);
			return;
		}
		const auto raw = result->value(QString::fromLatin1(kManifestKey));
		if (raw.isEmpty()) {
			beginUpload(std::nullopt, overwriteRemote);
			return;
		}
		const auto manifest = ParseManifest(raw.toUtf8());
		if (!manifest) {
			fail(manifest.error(), true);
			return;
		}
		beginUpload(*manifest, overwriteRemote);
	});
}

void SettingsSync::restoreNow() {
	if (!_enabled || !session()) {
		fail(u"sync_account_unavailable"_q, true);
		return;
	}
	const auto operation = ++_operation;
	setStatus(SyncState::Checking);
	const auto current = session();
	if (_storageSession != current) {
		_storage = std::make_unique<Storage>(current);
		_storageSession = current;
	}
	_storage->getItems({ QString::fromLatin1(kManifestKey) }, [=](auto result) {
		if (operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), true);
			return;
		}
		const auto raw = result->value(QString::fromLatin1(kManifestKey));
		if (raw.isEmpty()) {
			fail(u"backup_not_found"_q, true);
			return;
		}
		const auto manifest = ParseManifest(raw.toUtf8());
		if (!manifest) {
			fail(manifest.error(), true);
			return;
		}
		if (manifest->accountId != _accountId) {
			fail(u"backup_account_mismatch"_q, true);
			return;
		}
		downloadGeneration(*manifest, manifest->current, true, operation);
	});
}

void SettingsSync::rollbackPendingRestore() {
	const auto current = session();
	if (!current || _pendingRollback.isEmpty()) {
		return;
	}
	const auto snapshot = ValidateSettingsSnapshot(_pendingRollback, _accountId);
	if (!snapshot) {
		fail(snapshot.error(), true);
		return;
	}
	_applying = true;
	const auto result = ApplySettingsSnapshot(current, *snapshot);
	_applying = false;
	if (!result.success) {
		fail(u"rollback_failed"_q, true);
		return;
	}
	_categories = snapshot->categories;
	_categoriesValue = _categories;
	_syncProxies = _pendingRollbackSyncProxies;
	_syncProxiesValue = _syncProxies;
	_pendingRollback.clear();
	_pendingRollbackSyncProxies = false;
	_pendingTargetHash.clear();
	if (const auto currentSnapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies)) {
		_lastLocalHash = currentSnapshot->contentHash;
	}
	saveState();
	setStatus(_enabled ? SyncState::Dirty : SyncState::Disabled);
}

void SettingsSync::deleteRemote() {
	if (!session()) {
		fail(u"sync_account_unavailable"_q, true);
		return;
	}
	_enabled = false;
	_automaticUpload = false;
	_enabledValue = false;
	_automaticUploadValue = false;
	saveState();
	const auto operation = ++_operation;
	const auto current = session();
	if (_storageSession != current) {
		_storage = std::make_unique<Storage>(current);
		_storageSession = current;
	}
	_storage->getKeys([=](auto result) {
		if (operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), true);
			return;
		}
		auto keys = std::vector<QString>{ QString::fromLatin1(kManifestKey) };
		for (const auto &key : *result) {
			if (IsCloudChunkKey(key)) {
				keys.push_back(key);
			}
		}
		_storage->deleteItems(std::move(keys), [=](auto deleted) {
			if (operation != _operation) {
				return;
			}
			if (!deleted) {
				fail(deleted.error(), true);
				return;
			}
			_lastRevision = 0;
			_lastHash.clear();
			_lastLocalHash.clear();
			saveState();
			setStatus(SyncState::Disabled);
		});
	});
}

void SettingsSync::markLocalDirty() {
	if (_initialized && _enabled && !_applying && _pendingRollback.isEmpty()) {
		_dirtyTimer.callOnce(kDirtyDelay);
	}
}

void SettingsSync::schedulePoll() {
	_pollTimer.callEach(kPollInterval);
}

void SettingsSync::scheduleRetry() {
	const auto seconds = std::min(15 * 60, 5 * (1 << std::min(_retryAttempt++, 8)));
	_retryTimer.callOnce(seconds * crl::time(1000));
}

void SettingsSync::inspectManifest(bool manual, bool allowUpload) {
	if (!_enabled || !_pendingRollback.isEmpty()) {
		return;
	}
	const auto current = session();
	if (!current) {
		fail(u"sync_account_unavailable"_q, manual);
		return;
	}
	const auto operation = ++_operation;
	setStatus(SyncState::Checking);
	if (_storageSession != current) {
		_storage = std::make_unique<Storage>(current);
		_storageSession = current;
	}
	_storage->getItems({ QString::fromLatin1(kManifestKey) }, [=](auto result) {
		if (operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), manual);
			return;
		}
		const auto raw = result->value(QString::fromLatin1(kManifestKey));
		if (raw.isEmpty()) {
			handleManifest(std::nullopt, manual, allowUpload, operation);
			return;
		}
		const auto manifest = ParseManifest(raw.toUtf8());
		if (!manifest) {
			fail(manifest.error(), manual);
			return;
		}
		handleManifest(*manifest, manual, allowUpload, operation);
	});
}

void SettingsSync::handleManifest(
		std::optional<Manifest> manifest,
		bool manual,
		bool allowUpload,
		uint64_t operation) {
	if (operation != _operation) {
		return;
	}
	_retryAttempt = 0;
	const auto current = session();
	if (!current) {
		fail(u"sync_account_unavailable"_q, manual);
		return;
	}
	const auto snapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies);
	if (!snapshot) {
		fail(snapshot.error(), manual);
		return;
	}
	auto info = _status.current();
	info.remoteRevision = manifest ? manifest->revision : 0;
	info.remoteUpdatedAt = manifest ? manifest->updatedAt : 0;
	info.remoteDeviceId = manifest ? manifest->deviceId : QString();
	info.hasDifferences = false;
	_status = info;
	if (!manifest) {
		if (!_lastRevision) {
			beginUpload(std::nullopt, false);
		} else {
			setStatus(SyncState::Dirty, u"backup_not_found"_q);
		}
		return;
	}
	if (manifest->accountId != _accountId) {
		fail(u"backup_account_mismatch"_q, manual);
		return;
	}
	if (manifest->settingsHash == snapshot->contentHash) {
		_lastRevision = manifest->revision;
		_lastHash = manifest->settingsHash;
		_lastLocalHash = snapshot->contentHash;
		_retryAttempt = 0;
		saveState();
		setStatus(SyncState::Clean);
		return;
	}
	const auto changedLocally = !_lastLocalHash.isEmpty()
		&& snapshot->contentHash != _lastLocalHash;
	const auto changedRemotely = !_lastRevision
		|| manifest->revision != _lastRevision;
	if (changedLocally && changedRemotely) {
		setStatus(SyncState::Conflict);
		loadRemoteDiff(*manifest, operation);
	} else if (changedRemotely) {
		setStatus(SyncState::RemoteNewer);
		loadRemoteDiff(*manifest, operation);
	} else {
		setStatus(SyncState::Dirty);
		if (allowUpload && _automaticUpload) {
			beginUpload(*manifest, false);
		}
	}
}

void SettingsSync::loadRemoteDiff(
		const Manifest &manifest,
		uint64_t operation) {
	const auto keys = GenerationKeys(manifest.current);
	_storage->getItems(keys, [=](auto result) {
		if (operation != _operation || !result) {
			return;
		}
		auto chunks = std::vector<QString>();
		for (const auto &key : keys) {
			const auto value = result->value(key);
			if (value.isEmpty()) {
				return;
			}
			chunks.push_back(value);
		}
		const auto decoded = DecodePayload(chunks, manifest.current.sha256);
		const auto remote = decoded
			? ValidateSettingsSnapshot(*decoded, _accountId)
			: base::expected<Snapshot, QString>(base::unexpected(u"invalid_backup"_q));
		const auto current = session();
		const auto local = current
			? ExportSettingsSnapshot(
				current,
				_categories,
				_deviceId,
				_syncProxies)
			: base::expected<Snapshot, QString>(base::unexpected(u"sync_account_unavailable"_q));
		if (!remote || !local) {
			return;
		}
		const auto differences = DiffSettingsSnapshots(*local, *remote);
		if (!differences || operation != _operation) {
			return;
		}
		auto status = _status.current();
		for (const auto &entry : *differences) {
			status.differences[CategoryIndex(entry.category)] = entry.difference;
		}
		status.hasDifferences = true;
		_status = std::move(status);
	});
}

void SettingsSync::beginUpload(
		std::optional<Manifest> remote,
		bool overwriteRemote) {
	const auto current = session();
	if (!current) {
		fail(u"sync_account_unavailable"_q, true);
		return;
	}
	if (remote && remote->accountId != _accountId) {
		fail(u"backup_account_mismatch"_q, true);
		return;
	}
	if (remote && !overwriteRemote && _lastRevision != remote->revision) {
		setStatus(_lastRevision ? SyncState::Conflict : SyncState::RemoteNewer);
		return;
	}
	const auto snapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies);
	if (!snapshot) {
		fail(snapshot.error(), true);
		return;
	}
	const auto payload = EncodePayload(snapshot->canonical);
	if (!payload) {
		fail(payload.error(), true);
		return;
	}
	const auto baseRevision = remote ? remote->revision : 0;
	const auto generation = RandomId();
	_upload = std::make_unique<UploadJob>(UploadJob{
		.operation = _operation,
		.payload = *payload,
		.manifest = Manifest{
			.schema = 1,
			.revision = baseRevision + 1,
			.baseRevision = baseRevision,
			.current = { generation, int(payload->chunks.size()), payload->sha256 },
			.previous = remote ? remote->current : Generation(),
			.settingsHash = snapshot->contentHash,
			.updatedAt = uint64_t(base::unixtime::now()),
			.deviceId = _deviceId,
			.clientVersion = QString::fromLatin1(AppVersionStr),
			.accountId = _accountId,
			.categories = _categories,
		},
		.settingsHash = snapshot->contentHash,
	});
	setStatus(SyncState::Uploading);
	pumpUpload();
}

void SettingsSync::pumpUpload() {
	if (!_upload || _upload->failed || _upload->operation != _operation) {
		return;
	}
	while (_upload->active < 4
		&& _upload->next < int(_upload->payload.chunks.size())) {
		const auto index = _upload->next++;
		++_upload->active;
		const auto operation = _upload->operation;
		_storage->setItem(
			ChunkKey(_upload->manifest.current.id, index),
			_upload->payload.chunks[index],
			[=](auto result) {
				if (!_upload || operation != _operation) {
					return;
				}
				--_upload->active;
				if (!result) {
					_upload->failed = true;
					fail(result.error(), false);
					return;
				}
				++_upload->completed;
				if (_upload->completed == int(_upload->payload.chunks.size())) {
					commitUpload();
				} else {
					pumpUpload();
				}
			});
	}
}

void SettingsSync::commitUpload() {
	if (!_upload || _upload->operation != _operation) {
		return;
	}
	const auto operation = _upload->operation;
	_storage->getItems({ QString::fromLatin1(kManifestKey) }, [=](auto result) {
		if (!_upload || operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), false);
			return;
		}
		const auto raw = result->value(QString::fromLatin1(kManifestKey));
		const auto expected = _upload->manifest.baseRevision;
		if (raw.isEmpty()) {
			if (expected) {
				fail(Error{ ErrorCode::Conflict }, false);
				return;
			}
		} else {
			const auto parsed = ParseManifest(raw.toUtf8());
			if (!parsed || parsed->revision != expected) {
				fail(parsed ? Error{ ErrorCode::Conflict } : parsed.error(), false);
				return;
			}
		}
		writeUploadManifest();
	});
}

void SettingsSync::writeUploadManifest() {
	if (!_upload || _upload->operation != _operation) {
		return;
	}
	const auto operation = _upload->operation;
	const auto serialized = SerializeManifest(_upload->manifest);
	_storage->setItem(
		QString::fromLatin1(kManifestKey),
		QString::fromUtf8(serialized),
		[=](auto result) {
			if (!_upload || operation != _operation) {
				return;
			}
			if (!result) {
				fail(result.error(), false);
				return;
			}
			_storage->getItems({ QString::fromLatin1(kManifestKey) }, [=](auto read) {
				if (!_upload || operation != _operation) {
					return;
				}
				if (!read) {
					fail(read.error(), false);
					return;
				}
				const auto raw = read->value(QString::fromLatin1(kManifestKey));
				const auto parsed = ParseManifest(raw.toUtf8());
				if (!parsed
					|| parsed->current.id != _upload->manifest.current.id
					|| parsed->current.sha256 != _upload->manifest.current.sha256
					|| parsed->settingsHash != _upload->manifest.settingsHash) {
					fail(parsed ? Error{ ErrorCode::Conflict } : parsed.error(), false);
					return;
				}
				finishUpload();
			});
		});
}

void SettingsSync::finishUpload() {
	if (!_upload) {
		return;
	}
	const auto manifest = _upload->manifest;
	_lastRevision = _upload->manifest.revision;
	_lastHash = _upload->manifest.settingsHash;
	_lastLocalHash = _upload->settingsHash;
	_retryAttempt = 0;
	auto info = _status.current();
	info.remoteRevision = _lastRevision;
	info.remoteUpdatedAt = _upload->manifest.updatedAt;
	info.remoteDeviceId = _deviceId;
	_status = info;
	_upload.reset();
	saveState();
	setStatus(SyncState::Clean);
	cleanupOldGenerations(manifest);
}

void SettingsSync::cleanupOldGenerations(const Manifest &manifest) {
	const auto storage = base::make_weak(_storage.get());
	_storage->getKeys([=](auto result) {
		const auto strong = storage.get();
		if (!strong || !result) {
			return;
		}
		auto remove = std::vector<QString>();
		const auto current = QString::fromLatin1(kChunkPrefix)
			+ manifest.current.id
			+ u"_"_q;
		const auto previous = manifest.previous.id.isEmpty()
			? QString()
			: QString::fromLatin1(kChunkPrefix)
				+ manifest.previous.id
				+ u"_"_q;
		for (const auto &key : *result) {
			if (IsCloudChunkKey(key)
				&& !key.startsWith(current)
				&& (previous.isEmpty() || !key.startsWith(previous))) {
				remove.push_back(key);
			}
		}
		if (!remove.empty()) {
			strong->deleteItems(std::move(remove), [](auto) {});
		}
	});
}

void SettingsSync::downloadGeneration(
		const Manifest &manifest,
		Generation generation,
		bool allowFallback,
		uint64_t operation) {
	const auto keys = GenerationKeys(generation);
	_storage->getItems(keys, [=](auto result) {
		if (operation != _operation) {
			return;
		}
		if (!result) {
			fail(result.error(), true);
			return;
		}
		auto chunks = std::vector<QString>();
		for (const auto &key : keys) {
			const auto value = result->value(key);
			if (value.isEmpty()) {
				if (allowFallback && !manifest.previous.id.isEmpty()) {
					downloadGeneration(manifest, manifest.previous, false, operation);
				} else {
					fail(Error{ ErrorCode::MissingChunk }, true);
				}
				return;
			}
			chunks.push_back(value);
		}
		const auto decoded = DecodePayload(chunks, generation.sha256);
		if (!decoded) {
			if (allowFallback && !manifest.previous.id.isEmpty()) {
				downloadGeneration(manifest, manifest.previous, false, operation);
			} else {
				fail(decoded.error(), true);
			}
			return;
		}
		applyDownloaded(
			manifest,
			*decoded,
			generation.id != manifest.current.id,
			operation);
	});
}

void SettingsSync::applyDownloaded(
		const Manifest &manifest,
		QByteArray canonical,
		bool previousGeneration,
		uint64_t operation) {
	if (operation != _operation) {
		return;
	}
	const auto current = session();
	const auto incoming = ValidateSettingsSnapshot(canonical, _accountId);
	if (!current || !incoming) {
		fail(current ? incoming.error() : u"sync_account_unavailable"_q, true);
		return;
	}
	if (!previousGeneration
		&& (manifest.accountId != incoming->accountId
			|| manifest.categories != incoming->categories
			|| manifest.settingsHash != incoming->contentHash)) {
		fail(Error{ ErrorCode::HashMismatch }, true);
		return;
	}
	const auto rollback = ExportSettingsSnapshot(
		current,
		incoming->categories,
		_deviceId,
		_syncProxies || incoming->proxiesIncluded);
	if (!rollback) {
		fail(rollback.error(), true);
		return;
	}
	_pendingRollback = rollback->canonical;
	_pendingRollbackSyncProxies = _syncProxies;
	_pendingTargetHash = incoming->contentHash;
	saveState();
	_applying = true;
	const auto applied = ApplySettingsSnapshot(current, *incoming);
	_applying = false;
	if (!applied.success) {
		setStatus(SyncState::RestorePending, u"restore_apply_failed"_q);
		return;
	}
	_categories = incoming->categories;
	_categoriesValue = _categories;
	_syncProxies = incoming->proxiesIncluded;
	_syncProxiesValue = _syncProxies;
	_lastRevision = previousGeneration
		? manifest.baseRevision
		: manifest.revision;
	_lastHash = previousGeneration
		? incoming->contentHash
		: manifest.settingsHash;
	if (const auto appliedSnapshot = ExportSettingsSnapshot(
		current,
		_categories,
		_deviceId,
		_syncProxies)) {
		_pendingTargetHash = appliedSnapshot->contentHash;
		_lastLocalHash = appliedSnapshot->contentHash;
	} else {
		_lastLocalHash = incoming->contentHash;
	}
	saveState();
	auto warnings = QStringList();
	for (const auto &warning : applied.warnings) {
		warnings.push_back(warning);
	}
	setStatus(
		SyncState::RestorePending,
		warnings.empty() ? QString() : u"appearance_preserved"_q);
}

void SettingsSync::fail(Error error, bool manual) {
	const auto retry = (error.code == ErrorCode::Network);
	if (error.code == ErrorCode::BotUnavailable) {
		_enabled = false;
		_enabledValue = false;
		saveState();
	}
	fail(ErrorText(error), manual || !retry);
}

void SettingsSync::fail(QString details, bool manual) {
	_upload.reset();
	setStatus(SyncState::Error, details);
	if (manual) {
		_manualErrors.fire(std::move(details));
	}
	if (!manual && _enabled) {
		scheduleRetry();
	}
}

void SettingsSync::cancelOperation() {
	++_operation;
	_upload.reset();
	_dirtyTimer.cancel();
	_retryTimer.cancel();
}

void InitSettingsSync() {
	SettingsSync::Instance().init();
}

void MarkSettingsDirty() {
	SettingsSync::Instance().markLocalDirty();
}

} // namespace AyuCloud
