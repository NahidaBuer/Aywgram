# Agent Coding Reference

Read only the sections relevant to the current change: [API Usage](#api-usage), [UI Styling](#ui-styling), [Localization](#localization), or [RPL](#rpl). Repository paths in code spans are relative to the checkout root. Follow [REVIEW.md](../../REVIEW.md) for coding style; illustrative comments below explain examples rather than relaxing its source-comment rules.

## API Usage

### API Schema Files

API definitions use [TL Language](https://core.telegram.org/mtproto/TL):

1. **`Telegram/SourceFiles/mtproto/scheme/mtproto.tl`** - MTProto protocol (encryption, auth, etc.)
2. **`Telegram/SourceFiles/mtproto/scheme/api.tl`** - Telegram API (messages, users, chats, etc.)

### Making API Requests

Standard pattern using `api()`, generated `MTP...` types, and callbacks:

```cpp
api().request(MTPnamespace_MethodName(
    MTP_flags(flags_value),
    MTP_inputPeer(peer),
    MTP_string(messageText),
    MTP_long(randomId),
    MTP_vector<MTPMessageEntity>()
)).done([=](const MTPResponseType &result) {
    // Handle successful response

    // Multiple constructors - use .match() or check type:
    result.match([&](const MTPDuser &data) {
        // use data.vfirst_name().v
    }, [&](const MTPDuserEmpty &data) {
        // handle empty user
    });

    // Single constructor - use .data() shortcut:
    const auto &data = result.data();
    // use data.vmessages().v

}).fail([=](const MTP::Error &error) {
    // Handle API error
    if (error.type() == u"FLOOD_WAIT_X"_q) {
        // Handle flood wait
    }
}).handleFloodErrors().send();
```

**Key points:**
- Always refer to `api.tl` for method signatures and return types
- Use generated `MTP...` types for parameters (`MTP_int`, `MTP_string`, etc.)
- For multiple constructors, use `.match()` or check `.type()` against `mtpc_` constants then call `.c_constructorName()`:
  ```cpp
  // Using match:
  result.match([&](const MTPDuser &data) { ... }, [&](const MTPDuserEmpty &data) { ... });
  // Or explicit type check:
  if (result.type() == mtpc_user) {
      const auto &data = result.c_user(); // asserts on type mismatch
  }
  ```
- For single constructors, use `.data()` shortcut
- Include `.handleFloodErrors()` before `.send()` in rare cases where you want special case flood error handling
- Silently ignore HTTP 406 errors in UI: the server uses 406 to mean "show nothing to the user". Guard toasts with `MTP::IgnoreError(error)` or use `MTP::ShowErrorFallback(show, error)` (both in `mtproto/mtproto_response.h`) which shows `error.type()` as a toast unless the error should be ignored.

## UI Styling

### Style Files

UI styles are defined in `.style` files using custom syntax:

```style
using "ui/basic.style";
using "ui/widgets/widgets.style";

MyButtonStyle {
    textPadding: margins;
    icon: icon;
    height: pixels;
}

defaultButton: MyButtonStyle {
    textPadding: margins(10px, 15px, 10px, 15px);
    icon: icon{{ "gui/icons/search", iconColor }};
    height: 30px;
}

primaryButton: MyButtonStyle(defaultButton) {
    icon: icon{{ "gui/icons/check", iconColor }};
}
```

**Built-in types:**
- `int` - Integer numbers (e.g., `maxLines: 3;`)
- `bool` - Boolean values (e.g., `useShadow: true;`)
- `pixels` - Pixel values with `px` suffix (e.g., `10px`)
- `color` - Named colors from `ui/colors.palette`
- `icon` - Inline icon definition: `icon{{ "path/stem", color }}`
- `margins` - Four values: `margins(left, top, right, bottom)`
- `size` - Two values: `size(width, height)`
- `point` - Two values: `point(x, y)`
- `align` - Alignment: `align(center)`, `align(left)`
- `font` - Font: `font(14px semibold)`
- `double` - Floating point

**Multi-part icons** (layers drawn bottom-up):
```style
myComplexIcon: icon{
  { "gui/icons/background", iconBgColor },
  { "gui/icons/foreground", iconFgColor }
};
```

**Borders** are typically separate fields, not a single property:
```style
chatInput {
  border: 1px;                       // width
  borderFg: defaultInputFieldBorder; // color
}
```

**Never hardcode sizes in code:**

The app supports different interface scale options. Style `px` values are automatically scaled at runtime, but raw integer constants in code are not. Never use hardcoded numbers for margins, paddings, spacing, sizes, coordinates, or any other dimensional values. Always define them in `.style` files and reference via `st::`.

```cpp
// BAD - breaks at non-100% interface scale:
p.drawText(10, 20, text);
widget->setFixedHeight(48);
auto margin = 8;
auto iconSize = QSize(24, 24);

// GOOD - define in .style file and reference:
p.drawText(st::myWidgetTextLeft, st::myWidgetTextTop, text);
widget->setFixedHeight(st::myWidgetHeight);
auto margin = st::myWidgetMargin;
auto iconSize = st::myWidgetIconSize;
```

**Duration constants**: Animation durations should NOT go in `.style` files, this is a legacy approach. Prefer `constexpr auto kName = crl::time(N)` in an anonymous namespace in the relevant `.cpp` file.

### Usage in Code

```cpp
#include "styles/style_widgets.h"

// Access style members
int height = st::primaryButton.height;
const style::icon &icon = st::primaryButton.icon;
style::margins padding = st::primaryButton.textPadding;

// Use in painting
void MyWidget::paintEvent(QPaintEvent *e) {
    Painter p(this);
    p.fillRect(rect(), st::chatInput.backgroundColor);
}
```

## Localization

Read this section when adding or changing translated string calls. The English/Simplified Chinese maintenance contract and audit routing are in [AGENTS.md](../../AGENTS.md#localization).

### String Definitions

Strings are defined in `Telegram/Resources/langs/lang.strings`:

```
"lng_settings_title" = "Settings";
"lng_confirm_delete_item" = "Are you sure you want to delete {item_name}?";
"lng_files_selected#one" = "{count} file selected";
"lng_files_selected#other" = "{count} files selected";
```

### Usage in Code

**Immediate (current value):**

```cpp
auto currentTitle = tr::lng_settings_title(tr::now);

auto currentConfirmation = tr::lng_confirm_delete_item(
    tr::now,
    lt_item_name, currentItemName);

auto filesText = tr::lng_files_selected(tr::now, lt_count, count);
```

**Reactive (rpl::producer):**

```cpp
auto titleProducer = tr::lng_settings_title();

auto confirmationProducer = tr::lng_confirm_delete_item(
    lt_item_name,
    std::move(itemNameProducer));

auto filesTextProducer = tr::lng_files_selected(
    lt_count,
    countProducer | tr::to_count());
```

**Key points:**
- Pass `tr::now` as first argument for immediate `QString`
- Omit `tr::now` for reactive `rpl::producer<QString>`
- Placeholders use `lt_tag_name, value` pattern
- For `{count}`: immediate uses `int`, reactive uses `rpl::producer<float64>` with `| tr::to_count()`
- Move producers with `std::move` when passing to placeholders
- Rich text projectors — these `tr::` helpers serve double duty: as the **last argument** (projector) they set the return type to `TextWithEntities`, and as **placeholder values** they wrap individual substitutions in formatting. Always prefer them over `Ui::Text::Bold()`, `Ui::Text::RichLangValue`, etc. — see [REVIEW.md](../../REVIEW.md) for the full mapping.
  - `tr::marked` — basic projection, converts `QString` to `TextWithEntities`
  - `tr::rich` — interprets `**bold**`/`__italic__` markup in the string
  - `tr::bold`, `tr::italic`, `tr::underline` — wrap text in that formatting
  - `tr::link` — wrap as a clickable link
  - `tr::url(u"https://..."_q)` — returns a projection that converts text to a link pointing to the given URL; can be passed to `rpl::map` or directly to a `tr::lng_...` call
  ```cpp
  // As last argument (projector):
  auto title = tr::lng_export_progress_title(tr::now, tr::bold);
  auto text = tr::lng_proxy_incorrect_secret(tr::now, tr::rich);
  // As placeholder value wrapper + projector:
  auto desc = tr::lng_some_key(
      tr::now,
      lt_name,
      tr::bold(userName),
      lt_group,
      tr::bold(groupName),
      tr::rich);
  // Nested tr::lng as placeholder:
  auto linked = tr::lng_settings_birthday_contacts(
      lt_link,
      tr::lng_settings_birthday_contacts_link(tr::url(link)),
      tr::marked);
  ```

## RPL

### Core Concepts

**Producers** represent streams of values over time:

```cpp
auto intProducer = rpl::single(123);  // Emits single value
auto lifetime = rpl::lifetime();       // Manages subscription lifetime
```

### Starting Pipelines

```cpp
std::move(counter) | rpl::on_next([=](int value) {
    qDebug() << "Received: " << value;
}, lifetime);

// Without lifetime parameter - MUST store returned lifetime:
auto subscriptionLifetime = std::move(counter) | rpl::on_next([=](int value) {
    // process value
});
```

### Transforming Producers

```cpp
auto strings = std::move(ints) | rpl::map([](int value) {
    return QString::number(value * 2);
});

auto evenInts = std::move(ints) | rpl::filter([](int value) {
    return (value % 2 == 0);
});
```

### Combining Producers

**`rpl::combine`** - combines latest values (lambdas receive unpacked arguments):

```cpp
auto combined = rpl::combine(countProducer, textProducer);

std::move(combined) | rpl::on_next([=](int count, const QString &text) {
    qDebug() << "Count=" << count << ", Text=" << text;
}, lifetime);
```

**`rpl::merge`** - merges producers of same type:

```cpp
auto merged = rpl::merge(sourceA, sourceB);

std::move(merged) | rpl::on_next([=](QString &&value) {
    qDebug() << "Merged value: " << value;
}, lifetime);
```

**Other pipeline starters** — besides `rpl::on_next`, there are:
- `rpl::on_error([=](Error &&e) { ... }, lifetime)` — handle errors
- `rpl::on_done([=] { ... }, lifetime)` — handle stream completion
- `rpl::on_next_error_done(nextCb, errorCb, doneCb, lifetime)` — handle all three

The `Error` template parameter defaults to `rpl::no_error`: `rpl::producer<Type, Error = no_error>`.

**Key points:**
- Explicitly `std::move` producers when starting pipelines
- Pass `rpl::lifetime` to `on_...` methods or store returned lifetime
- Use `rpl::duplicate(producer)` to reuse a producer multiple times
- Combined producers automatically unpack tuples in lambdas (works with `rpl::map`, `rpl::filter`, and `rpl::on_next`)
