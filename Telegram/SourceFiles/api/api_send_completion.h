/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {

class SendCompletion final {
public:
	explicit SendCompletion(FnMut<void(bool)> callback)
	: _callback(std::move(callback)) {
	}

	void addRequest() {
		if (!_completed) {
			++_pending;
		}
	}

	void finishRequest(bool success) {
		if (_completed) {
			return;
		}
		if (!success) {
			complete(false);
			return;
		}
		Assert(_pending > 0);
		--_pending;
		finishIfReady();
	}

	void seal() {
		if (_completed) {
			return;
		}
		_sealed = true;
		finishIfReady();
	}

	void fail() {
		complete(false);
	}

private:
	void finishIfReady() {
		if (_sealed && !_pending) {
			complete(true);
		}
	}

	void complete(bool success) {
		if (_completed) {
			return;
		}
		_completed = true;
		auto callback = std::move(_callback);
		if (callback) {
			callback(success);
		}
	}

	FnMut<void(bool)> _callback;
	int _pending = 0;
	bool _sealed = false;
	bool _completed = false;

};

using SendCompletionPtr = std::shared_ptr<SendCompletion>;

[[nodiscard]] inline SendCompletionPtr MakeSendCompletion(
		FnMut<void(bool)> callback) {
	return callback
		? std::make_shared<SendCompletion>(std::move(callback))
		: SendCompletionPtr();
}

} // namespace Api
