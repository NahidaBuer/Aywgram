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

	[[nodiscard]] const std::unordered_set<ID> &upstreamDevelopers() const {
		return _upstreamDevelopers;
	}

	[[nodiscard]] auto upstreamOfficialChannels() const
	-> const std::unordered_set<ID> & {
		return _upstreamOfficialChannels;
	}

	[[nodiscard]] const std::unordered_set<ID> &upstreamSupporters() const {
		return _upstreamSupporters;
	}

	[[nodiscard]] auto upstreamSupporterChannels() const
	-> const std::unordered_set<ID> & {
		return _upstreamSupporterChannels;
	}

	[[nodiscard]] auto upstreamCustomBadges() const
	-> const std::unordered_map<ID, CustomBadge> & {
		return _upstreamCustomBadges;
	}

	[[nodiscard]] const std::unordered_set<ID> &aywGramDevelopers() const {
		return _aywGramDevelopers;
	}

	[[nodiscard]] auto aywGramOfficialChannels() const
	-> const std::unordered_set<ID> & {
		return _aywGramOfficialChannels;
	}

	[[nodiscard]] const std::unordered_set<ID> &aywGramSupporters() const {
		return _aywGramSupporters;
	}

	[[nodiscard]] auto aywGramSupporterChannels() const
	-> const std::unordered_set<ID> & {
		return _aywGramSupporterChannels;
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

	const std::unordered_set<ID> _upstreamDevelopers;
	const std::unordered_set<ID> _upstreamOfficialChannels;
	const std::unordered_set<ID> _upstreamSupporters;
	const std::unordered_set<ID> _upstreamSupporterChannels;
	const std::unordered_map<ID, CustomBadge> _upstreamCustomBadges;

	const std::unordered_set<ID> _aywGramDevelopers;
	const std::unordered_set<ID> _aywGramOfficialChannels;
	const std::unordered_set<ID> _aywGramSupporters;
	const std::unordered_set<ID> _aywGramSupporterChannels = {};

	const QString _donateUsername = u"@ayugramOwner"_q;
	const QString _donateAmountUsd = u"5.00"_q;
	const QString _donateAmountTon = u"3.50"_q;
	const QString _donateAmountRub = u"386"_q;

};
