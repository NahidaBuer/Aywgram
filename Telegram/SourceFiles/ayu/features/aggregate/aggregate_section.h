#pragma once

#include "ayu/features/aggregate/aggregate_model.h"
#include "history/view/history_view_list_widget.h"
#include "ui/layers/layer_widget.h"
#include "ui/abstract_button.h"
#include "window/section_memento.h"

namespace Info::Media { class Memento; }

namespace Ayu::Aggregate {

class Controller;

class Memento final : public Window::SectionMemento {
public:
	explicit Memento(FilterId folder, std::shared_ptr<Controller> data = nullptr);
	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	FilterId folder = 0;
	std::shared_ptr<Controller> data;
	HistoryView::ListMemento list;
	std::optional<Category> mediaCategory;
	NavigationIntent intent = NavigationIntent::Restore;
	std::shared_ptr<Info::Media::Memento> media;
	bool hasReadingState = false;
	bool sidebarOpen = false;
	int sidebarTop = 0;
	int sidebarLimit = 0;

};

[[nodiscard]] object_ptr<Ui::AbstractButton> CreateEntry(
	QWidget *parent, not_null<Window::SessionController*> controller);

void Show(not_null<Window::SessionController*> controller, FilterId folder);

} // namespace Ayu::Aggregate
