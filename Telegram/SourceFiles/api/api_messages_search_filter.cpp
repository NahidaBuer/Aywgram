/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

namespace Api {

bool MatchesSearchFilterLocally(
		SearchFilter filter,
		LocalSearchMessageFlags flags) {
	const auto has = [&](LocalSearchMessageFlag flag) {
		return (flags & static_cast<LocalSearchMessageFlags>(flag)) != 0;
	};
	switch (filter) {
	case SearchFilter::NoFilter:
		return true;
	case SearchFilter::Photos:
		return has(LocalSearchMessageFlag::Photo);
	case SearchFilter::Videos:
		return has(LocalSearchMessageFlag::Video);
	case SearchFilter::Files:
		return has(LocalSearchMessageFlag::File);
	case SearchFilter::Links:
		return has(LocalSearchMessageFlag::Link);
	case SearchFilter::Music:
		return has(LocalSearchMessageFlag::Music);
	case SearchFilter::VoiceMessages:
		return has(LocalSearchMessageFlag::VoiceMessage);
	case SearchFilter::VideoMessages:
		return has(LocalSearchMessageFlag::VideoMessage);
	case SearchFilter::Gifs:
		return has(LocalSearchMessageFlag::Gif);
	case SearchFilter::Polls:
		return has(LocalSearchMessageFlag::Poll);
	case SearchFilter::MyMentions:
		return has(LocalSearchMessageFlag::Mention);
	case SearchFilter::Locations:
		return has(LocalSearchMessageFlag::Location);
	case SearchFilter::Pinned:
		return has(LocalSearchMessageFlag::Pinned);
	}
	Unexpected("SearchFilter in MatchesSearchFilterLocally.");
}

bool MatchesSearchSenderLocally(PeerId actual, PeerId expected) {
	return actual == expected;
}

bool MatchesSearchTraitsLocally(
		const SearchMessageTraits &traits,
		SearchFilter filter,
		PeerId sender) {
	return MatchesSearchFilterLocally(filter, traits.filterFlags)
		&& MatchesSearchSenderLocally(traits.sender, sender);
}

std::optional<MessageIdsList> SearchMessagesMatchingTraits(
		const FoundMessages &found,
		SearchFilter filter,
		PeerId sender) {
	auto result = MessageIdsList();
	result.reserve(found.messages.size());
	for (const auto id : found.messages) {
		const auto i = found.traits.find(id);
		if (i == end(found.traits)) {
			return std::nullopt;
		}
		if (MatchesSearchTraitsLocally(i->second, filter, sender)) {
			result.push_back(id);
		}
	}
	return result;
}

MTPMessagesFilter PrepareSearchFilter(SearchFilter filter) {
	switch (filter) {
	case SearchFilter::Photos:
		return MTP_inputMessagesFilterPhotos();
	case SearchFilter::Videos:
		return MTP_inputMessagesFilterVideo();
	case SearchFilter::Files:
		return MTP_inputMessagesFilterDocument();
	case SearchFilter::Links:
		return MTP_inputMessagesFilterUrl();
	case SearchFilter::Music:
		return MTP_inputMessagesFilterMusic();
	case SearchFilter::VoiceMessages:
		return MTP_inputMessagesFilterVoice();
	case SearchFilter::VideoMessages:
		return MTP_inputMessagesFilterRoundVideo();
	case SearchFilter::Gifs:
		return MTP_inputMessagesFilterGif();
	case SearchFilter::Polls:
		return MTP_inputMessagesFilterPoll();
	case SearchFilter::MyMentions:
		return MTP_inputMessagesFilterMyMentions();
	case SearchFilter::Locations:
		return MTP_inputMessagesFilterGeo();
	case SearchFilter::Pinned:
		return MTP_inputMessagesFilterPinned();
	case SearchFilter::NoFilter:
		return MTP_inputMessagesFilterEmpty();
	}
	Unexpected("SearchFilter in PrepareSearchFilter.");
}

const std::vector<SearchFilter> &SearchFilters() {
	static const auto result = std::vector{
		SearchFilter::NoFilter,
		SearchFilter::Photos,
		SearchFilter::Videos,
		SearchFilter::Files,
		SearchFilter::Links,
		SearchFilter::Music,
		SearchFilter::VoiceMessages,
		SearchFilter::VideoMessages,
		SearchFilter::Gifs,
		SearchFilter::Polls,
		SearchFilter::MyMentions,
		SearchFilter::Locations,
		SearchFilter::Pinned,
	};
	return result;
}

SearchSelectionNormalization NormalizeSearchSelection(
		SearchSelectionChange change,
		bool senderSelected,
		SearchFilter filter,
		bool exactIntersection) {
	if (exactIntersection
		|| !senderSelected
		|| filter == SearchFilter::NoFilter) {
		return {};
	}
	return {
		.clearSender = (change == SearchSelectionChange::Filter),
		.clearFilter = (change == SearchSelectionChange::Sender),
	};
}

bool ShouldUseSearchIntersection(
		bool enabled,
		bool fixedFilter,
		bool senderSelected,
		SearchFilter filter) {
	return enabled
		&& !fixedFilter
		&& senderSelected
		&& filter != SearchFilter::NoFilter;
}

SearchIntersectionRequests PrepareSearchIntersectionRequests(
		const MessagesSearch::Request &request) {
	Expects(request.from != nullptr);
	Expects(request.filter != SearchFilter::NoFilter);
	auto result = SearchIntersectionRequests{
		.sender = request,
		.filter = request,
	};
	result.sender.filter = SearchFilter::NoFilter;
	result.filter.from = nullptr;
	return result;
}

} // namespace Api
