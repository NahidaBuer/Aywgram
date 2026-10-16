#include "ayu/features/aggregate/aggregate_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"

#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Ayu::Aggregate {
namespace {

constexpr auto kGlobalKey = "ayu.aggregate.settings";

[[nodiscard]] rpl::event_stream<> &Changes() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] std::string FolderKey(FilterId folder) {
	return "ayu.aggregate.folder." + std::to_string(folder);
}

[[nodiscard]] QJsonObject Parse(const QByteArray &bytes) {
	return QJsonDocument::fromJson(bytes).object();
}

[[nodiscard]] QByteArray Serialize(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

GlobalSettings ReadGlobal() {
	const auto object = Parse(Core::App().settings().readPref<QByteArray>(kGlobalKey));
	return {
		.enabled = object.value(u"enabled"_q).toBool(false),
		.parameters = Normalize({
			.groupGap = object.value(u"groupGap"_q).toInt(120),
			.privateGap = object.value(u"privateGap"_q).toInt(900),
			.botGap = object.value(u"botGap"_q).toInt(900),
			.maximumUnits = object.value(u"maximumUnits"_q).toInt(30),
		}),
		.mediaScale = std::clamp(object.value(u"mediaScale"_q).toDouble(1.), 1., 5.),
	};
}

bool FolderExists(not_null<Main::Session*> session, FilterId folder) {
	return !folder || ranges::contains(session->data().chatsFilters().list(), folder, &Data::ChatFilter::id);
}

void WriteGlobal(GlobalSettings settings) {
	settings.parameters = Normalize(settings.parameters);
	Core::App().settings().writePref<QByteArray>(kGlobalKey, Serialize({
		{ u"enabled"_q, settings.enabled },
		{ u"mediaScale"_q, std::clamp(settings.mediaScale, 1., 5.) },
		{ u"groupGap"_q, settings.parameters.groupGap },
		{ u"privateGap"_q, settings.parameters.privateGap },
		{ u"botGap"_q, settings.parameters.botGap },
		{ u"maximumUnits"_q, settings.parameters.maximumUnits },
	}));
	Core::App().saveSettingsDelayed();
	Changes().fire({});
}

FolderSettings ReadFolder(not_null<Main::Session*> session, FilterId folder) {
	const auto object = Parse(session->local().readPref<QByteArray>(FolderKey(folder)));
	auto result = FolderSettings{
		.visible = object.value(u"visible"_q).toBool(true),
		.newestFirst = object.value(u"newestFirst"_q).toBool(true),
		.categories = Mask(object.value(u"categories"_q).toInt())
			& ((Mask(1) << int(Category::Count)) - 1),
		.sources = Mask(object.value(u"sources"_q).toInt()) & 15,
		.unmutedOnly = object.value(u"unmutedOnly"_q).toBool(),
		.anchor = {
			.peer = object.value(u"anchorPeer"_q).toString().toULongLong(),
			.message = object.value(u"anchorMessage"_q).toString().toLongLong(),
		},
		.anchorShift = object.value(u"anchorShift"_q).toInt(),
		.snapshot = object.value(u"snapshot"_q).toString().toLongLong(),
		.draft = QByteArray::fromBase64(object.value(u"draft"_q).toString().toLatin1()),
	};
	for (const auto &value : object.value(u"excluded"_q).toArray()) {
		if (const auto id = value.toString().toULongLong()) {
			result.excluded.push_back(id);
		}
	}
	return result;
}

void WriteFolder(
		not_null<Main::Session*> session,
		FilterId folder,
		const FolderSettings &settings,
		bool notify) {
	if (!FolderExists(session, folder)) {
		return;
	}
	auto excluded = QJsonArray();
	for (const auto id : settings.excluded) {
		excluded.push_back(QString::number(id));
	}
	session->local().writePref<QByteArray>(FolderKey(folder), Serialize({
		{ u"visible"_q, settings.visible },
		{ u"newestFirst"_q, settings.newestFirst },
		{ u"categories"_q, int(settings.categories) },
		{ u"sources"_q, int(settings.sources) },
		{ u"unmutedOnly"_q, settings.unmutedOnly },
		{ u"excluded"_q, excluded },
		{ u"anchorPeer"_q, QString::number(settings.anchor.peer) },
		{ u"anchorMessage"_q, QString::number(settings.anchor.message) },
		{ u"anchorShift"_q, settings.anchorShift },
		{ u"snapshot"_q, QString::number(settings.snapshot) },
		{ u"draft"_q, QString::fromLatin1(settings.draft.toBase64()) },
	}));
	if (notify) {
		Changes().fire({});
	}
}

void RemoveFolder(not_null<Main::Session*> session, FilterId folder) {
	session->local().clearPref(FolderKey(folder));
	Changes().fire({});
}

rpl::producer<> SettingsChanged() {
	return Changes().events();
}

QString CategoryLabel(Category category) {
	switch (category) {
	case Category::Text: return tr::lng_notification_show_text(tr::now);
	case Category::Photo: return tr::lng_media_type_photos(tr::now);
	case Category::Video: return tr::lng_media_type_videos(tr::now);
	case Category::Music: return tr::lng_all_music(tr::now);
	case Category::Voice: return tr::lng_media_type_audios(tr::now);
	case Category::File: return tr::lng_media_type_files(tr::now);
	case Category::Link: return tr::lng_all_links(tr::now);
	case Category::Gif: return tr::lng_media_type_gifs(tr::now);
	case Category::RoundVideo: return tr::lng_media_type_rounds(tr::now);
	case Category::Sticker: return tr::lng_switch_stickers(tr::now);
	case Category::Poll: return tr::lng_media_type_polls(tr::now);
	case Category::Location: return tr::ayu_AggregateLocations(tr::now);
	case Category::Count: break;
	}
	Unexpected("Category in Aggregate::CategoryLabel.");
}

QString SourceLabel(SourceType type) {
	switch (type) {
	case SourceType::Channel: return tr::lng_filters_include_channels(tr::now);
	case SourceType::Group: return tr::lng_filters_include_groups(tr::now);
	case SourceType::Private: return tr::lng_search_filter_private(tr::now);
	case SourceType::Bot: return tr::lng_filters_include_bots(tr::now);
	}
	Unexpected("SourceType in Aggregate::SourceLabel.");
}

void SettingsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::ayu_AggregateTitle());
	const auto settings = ReadGlobal();
	const auto enabled = box->addRow(object_ptr<Ui::Checkbox>(
		box,
		tr::ayu_AggregateEnabled(tr::now),
		settings.enabled,
		st::defaultBoxCheckbox));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::ayu_AggregateSettingsAbout(),
		st::boxLabel), style::margins(st::boxRowPadding.left(), st::boxPadding.top(), st::boxRowPadding.right(), st::boxPadding.bottom()));
	const auto addField = [&](rpl::producer<QString> label, int value) {
		box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(label), st::boxLabel),
			style::margins(st::boxRowPadding.left(), st::boxPadding.top(), st::boxRowPadding.right(), st::boxPadding.top() / 2));
		const auto wrap = box->addRow(object_ptr<Ui::FixedHeightWidget>(
			box, st::aggregateNumberField.heightMin));
		auto owned = object_ptr<Ui::NumberInput>(wrap,
			st::aggregateNumberField, rpl::single(QString()), QString::number(value), 86400);
		const auto field = owned.data();
		wrap->widthValue() | rpl::on_next([=](int width) {
			field->resize(width, st::aggregateNumberField.heightMin);
		}, wrap->lifetime());
		return field;
	};
	const auto group = addField(tr::ayu_AggregateGroupGap(), settings.parameters.groupGap);
	const auto personal = addField(tr::ayu_AggregatePrivateGap(), settings.parameters.privateGap);
	const auto bot = addField(tr::ayu_AggregateBotGap(), settings.parameters.botGap);
	const auto maximum = addField(tr::ayu_AggregateMaximum(), settings.parameters.maximumUnits);
	box->addRow(object_ptr<Ui::FlatLabel>(box, tr::ayu_AggregateMediaScale(), st::boxLabel),
		style::margins(st::boxRowPadding.left(), st::boxPadding.top(), st::boxRowPadding.right(), st::boxPadding.top()));
	auto scale = Settings::MakeSliderWithLabel(box, st::settingsScale, st::boxLabel,
		st::boxPadding.top(), st::normalFont->width(u"5.0×"_q), true);
	const auto slider = scale.slider;
	const auto label = scale.label;
	const auto updateScale = [=](float64 value) {
		label->setText(QString::number(1. + value * 4., 'f', 1) + u"×"_q);
	};
	slider->setAdjustCallback([](float64 value) { return std::round(value * 40.) / 40.; });
	slider->setValue((settings.mediaScale - 1.) / 4.);
	slider->setChangeProgressCallback(updateScale);
	updateScale(slider->value());
	box->addRow(std::move(scale.widget));
	box->addButton(tr::lng_settings_save(), [=] {
		auto values = std::vector<int>();
		for (const auto field : { group, personal, bot, maximum }) {
			auto ok = false;
			const auto value = field->getLastText().toInt(&ok);
			const auto isMaximum = (field == maximum);
			if (!ok || value < (isMaximum ? 1 : 0)
				|| value > (isMaximum ? 500 : 86400)) {
				field->showError();
				return;
			}
			values.push_back(value);
		}
		WriteGlobal({ enabled->checked(), { values[0], values[1], values[2], values[3] }, 1. + slider->value() * 4. });
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->addLeftButton(tr::ayu_AggregateReset(), [=] {
		group->setText(u"120"_q);
		personal->setText(u"900"_q);
		bot->setText(u"900"_q);
		maximum->setText(u"30"_q);
		slider->setValue(0.);
		updateScale(0.);
	});
}

} // namespace Ayu::Aggregate
