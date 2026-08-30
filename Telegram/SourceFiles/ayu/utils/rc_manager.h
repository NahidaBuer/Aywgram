// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

struct CustomBadge
{
	EmojiStatusId emojiStatusId;
	QString text;
};

class RCManager final
{
public:
	static RCManager &getInstance() {
		static RCManager instance;
		return instance;
	}

	RCManager(const RCManager &) = delete;
	RCManager &operator=(const RCManager &) = delete;
	RCManager(RCManager &&) = delete;
	RCManager &operator=(RCManager &&) = delete;

	[[nodiscard]] const std::unordered_set<ID> &developers() const {
		return _developers;
	}

	[[nodiscard]] const std::unordered_set<ID> &channels() const {
		return _officialChannels;
	}

	[[nodiscard]] const std::unordered_set<ID> &supporters() const {
		return _supporters;
	}

	[[nodiscard]] const std::unordered_set<ID> &supporterChannels() const {
		return _supporterChannels;
	}

	[[nodiscard]] const std::unordered_map<ID, CustomBadge> &supporterCustomBadges() const {
		return _customBadges;
	}

	[[nodiscard]] QString donateUsername() const {
		return _donateUsername;
	}

	[[nodiscard]] QString donateAmountUsd() const {
		return _donateAmountUsd;
	}

	[[nodiscard]] QString donateAmountTon() const {
		return _donateAmountTon;
	}

	[[nodiscard]] QString donateAmountRub() const {
		return _donateAmountRub;
	}

private:
	RCManager();

	const std::unordered_set<ID> _developers;
	const std::unordered_set<ID> _officialChannels;
	const std::unordered_set<ID> _supporters;
	const std::unordered_set<ID> _supporterChannels;
	const std::unordered_map<ID, CustomBadge> _customBadges;

	const QString _donateUsername = u"@ayugramOwner"_q;
	const QString _donateAmountUsd = u"5.00"_q;
	const QString _donateAmountTon = u"3.50"_q;
	const QString _donateAmountRub = u"386"_q;

};
