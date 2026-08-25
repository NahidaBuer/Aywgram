/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

namespace Api {

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
