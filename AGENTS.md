# Agent Guide for Telegram Desktop

This guide defines repository-wide instructions for coding agents working with the Telegram Desktop codebase.

## Fork Lineage and Upstream Integration


```text
AywGram Desktop
  -> AyuGram Desktop
    -> Telegram Desktop
```

Preserve the distribution-specific patches and the existing AyuGram feature set as the
product baseline. Upstream integration should move the fork closer to AyuGram
and Telegram Desktop only where the incoming changes are useful and compatible
with that baseline; upstream parity by itself is not a reason to remove or
weaken fork-specific behavior.

For recurring upstream maintenance:

- Use the repository-owned `.agents/skills/ayugram-upstream-sync` workflow for
  recurring audits and integrations, and preserve it when resolving upstream
  agent-workflow conflicts. Continue to ignore unrelated skills or agent
  pipelines introduced by upstream.
- Start from a clean worktree and fetch the relevant remotes before comparing
  commits. In this checkout, `ayugram` is the public AyuGram source, `telegram`
  is the official Telegram Desktop source, and `origin` is the distribution
  fork's publishing remote. Verify their URLs and record the fetched branch
  tips in each maintenance report
- Review AyuGram and Telegram Desktop separately. Select changes based on the
  requested product needs, security and correctness fixes, compatibility, and
  the value of reducing future divergence. Do not bulk-merge an upstream branch
  without first understanding its unique commits and affected files.
- Integrate on a dedicated branch in stages: distribution patches first, then the
  selected AyuGram changes, then the selected Telegram Desktop changes and
  follow-up compatibility fixes. Adjust the order when commit ancestry requires
  it, and explain the reason in the plan.
- Before resolving conflicts, identify the intent on all sides. Preserve the
  current distribution and AyuGram behavior while adapting it to upstream APIs and
  structural changes. Never resolve a conflict by blindly choosing one side for
  a file containing fork-specific features.
- Treat submodule pointers, generated sources, API schemas, settings
  serialization, styles, and localization as explicit integration risks. Keep
  related main-repository and submodule changes aligned, and report any
  referenced submodule commit that is unavailable from its configured remote.
- After each integration stage, inspect the resulting diff for lost AyuGram or
  distribution features and run focused checks appropriate to the touched areas.
  Follow the repository build restrictions in this guide; do not build unless
  the user explicitly overrides the repository-wide no-build instruction.
- Deliver a maintenance summary containing the compared commit ranges, grouped
  incoming changes, selected and deferred patches with reasons, conflicts and
  resolutions, feature-preservation checks, unresolved risks, and the proposed
  next integration baseline.

If you're asked to create a Pull Request, then clearly state in PR description that it was AI generated.

## Build and Platform Instructions

Avoid building the project unless the user explicitly authorizes a build.
Build Release when no configuration is named; build Debug only when explicitly
requested or after approval for a stated diagnostic need. Before configuring,
building, packaging, or troubleshooting, read only the guide for the target
platform: [Windows](docs/agent-building-windows.md),
[macOS](docs/agent-building-macos.md), or
[Linux](docs/agent-building-linux.md).
Keep all generated output under the repository-root `out/` directory.
Treat a successful build as compilation evidence, not UI or release approval.

## Text File Format

- On Windows, keep project text files with CRLF line endings.
- Do not save source, header, build/config, style, or localization files as UTF-8 with BOM. Use UTF-8 without BOM.
- When rewriting project text files for normalization, preserve file content otherwise and do not introduce a BOM.
- Never hard-wrap Markdown prose or list items to an 80-column limit. Keep each paragraph or list item on one line unless Markdown syntax requires a deliberate line break.

## Commits

- Follow the Conventional Commits format:

```text
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

- Subject: one concise, plain-language line summarizing the change, ~50-60 characters, matching the style of recent `git log` subjects. This is usually the entire message.
- Add a short plain-language body only when the subject can't carry it (what was done, not the technical how) — a line or two at most.
- Never add a `Co-Authored-By:` line or any tool/assistant attribution trailer.
- Never add `Autotask:`/attempt or other workflow markers — commits read like normal history.

## Local Storage Serialization

Both app-level (`Core::Settings`) and session-level (`Main::SessionSettings`) use sequential binary serialization via `QDataStream`. Key rules:

- New fields must ALWAYS be appended at the **end** of the stream, never inserted in the middle
- Reading new fields must be guarded with `!stream.atEnd()` and provide a meaningful default/fallback
- Inserting in the middle breaks reading of data saved by older versions (the new read code consumes bytes that belong to subsequent fields)
- For simple flags and values, prefer using the generic KV prefs facility (`writePref<Type>` / `readPref<Type>`) instead of adding to the binary stream -- this avoids serialization ordering issues entirely

---

# Development Guidelines

## Coding Style

**Do NOT write comments in code:**

This is important! Do not write single-line comments that describe what the next line does - they are bloat. Comments are allowed ONLY to describe complex algorithms in detail, when the explanation requires at least 4-5 lines. Self-documenting code with clear variable and function names is preferred.

```cpp
// BAD - don't do this:
// Get the user's name
auto name = user->name();
// Check if premium
if (user->isPremium()) {

// GOOD - no comments needed, code is self-explanatory:
auto name = user->name();
if (user->isPremium()) {

// ACCEPTABLE - complex algorithm explanation (4+ lines):
// The algorithm works by first collecting all visible messages
// in the viewport, then calculating their intersection with
// the clip rectangle. Messages are grouped by date headers,
// and we need to account for sticky headers that may overlap
// with the first message in each group.
```

**Style and formatting rules** are in `REVIEW.md` — see that file for empty-line-before-closing-brace, operator placement in multi-line expressions, if-with-initializer, and other mechanical style rules.

**Use `auto` for type deduction:**

Prefer `auto` (or `const auto`, `const auto &`) instead of explicit types:

```cpp
// Prefer this:
auto currentTitle = tr::lng_settings_title(tr::now);
auto nameProducer = GetNameProducer();

// Instead of this:
QString currentTitle = tr::lng_settings_title(tr::now);
rpl::producer<QString> nameProducer = GetNameProducer();
```

**Use trailing return types only when the normal form is too long:**

Prefer the normal return type form when the opening line fits comfortably, roughly around 77 characters or less:

```cpp
// GOOD:
[[nodiscard]] TextWithEntities FlattenSummaryBlocks(
	const std::vector<Block> &blocks);
```

Do not use one-line trailing return types, or put the trailing return type after `)` on the same line. If it fits on one line with trailing syntax, the normal form would be shorter and easier to read:

```cpp
// BAD:
auto ComputeTitle() -> QString;

// BAD:
[[nodiscard]] auto FlattenSummaryBlocks(
	const std::vector<Block> &blocks) -> TextWithEntities;
```

Use `auto` with a trailing return type only when the normal opening line
`{attributes} {return-type} {class-name::}{function-name(}` would be too long, or would force the return type onto its own line. Put the arrow and return type on the next line so the return type remains easy to find:

```cpp
// BAD:
not_null<HistoryView::Controls::ComposeAiButton*>
HistoryView::Controls::SetupCaptionAiButton(SetupCaptionAiButtonArgs &&args);
```

```cpp
// GOOD:
auto HistoryView::Controls::SetupCaptionAiButton(
		SetupCaptionAiButtonArgs &&args)
-> not_null<HistoryView::Controls::ComposeAiButton*>;
```

This applies to both declarations and definitions.

**Use `_q` for QString literals:**

Prefer the project literal `u"..."_q` instead of the verbose `QStringLiteral("...")` macro when creating `QString` values:

```cpp
// Prefer this:
auto text = u"Settings"_q;

// Instead of this:
auto text = QStringLiteral("Settings");
```

**Never use `Q_OS_LINUX` for platform checks in new code:**

Telegram Desktop distinguishes at most three platforms: Windows / macOS / all-other. The "all-other" branch covers Linux, the BSD variants and more — and this is almost always the branch you want. `Q_OS_LINUX` narrows it to Linux alone, silently excluding the non-Linux Unix platforms, which is almost never intended. For the all-other branch use `!defined Q_OS_WIN && !defined Q_OS_MAC` at compile time, or its runtime equivalent `Platform::IsLinux()` — which, despite the name, means exactly `!defined Q_OS_WIN && !defined Q_OS_MAC` ("everything except Windows and macOS"), not Linux specifically:

```cpp
// BAD - excludes FreeBSD and other non-Linux Unix:
#ifdef Q_OS_LINUX
UnixSpecificCode();
#endif // Q_OS_LINUX

// GOOD - the all-other branch, compile time:
#if !defined Q_OS_WIN && !defined Q_OS_MAC
UnixSpecificCode();
#endif // !Q_OS_WIN && !Q_OS_MAC

// GOOD - the all-other branch, runtime (same meaning, NOT Linux-only):
if (Platform::IsLinux()) {
	UnixSpecificCode();
}
```

`Q_OS_LINUX` is only for the rare case where you genuinely want exactly Linux and not the other Unix-like systems — usually you don't. The few existing uses (`Telegram/SourceFiles/core/sandbox.cpp`, `Telegram/SourceFiles/platform/linux/specific_linux.cpp`) are such genuinely Linux-only code paths and stay as-is.

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

Keep `Telegram/Resources/langs/zh-hans.lproj/zh-hans.json` in sync when an
`ayu_` key in `Telegram/Resources/langs/lang.strings` is mirrored there.

Use the repository-owned `.agents/skills/aywgram-i18n` workflow for periodic
localization audits, English source synchronization, release preparation, and
localization-heavy upstream integrations. Keep `lang.strings`, bundled
Simplified Chinese, and the translatable English source current with each
relevant change; batch other language translations during dedicated i18n
maintenance instead of requiring every feature change to update every locale.

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
- Rich text projectors — these `tr::` helpers serve double duty: as the **last argument** (projector) they set the return type to `TextWithEntities`, and as **placeholder values** they wrap individual substitutions in formatting. Always prefer them over `Ui::Text::Bold()`, `Ui::Text::RichLangValue`, etc. — see REVIEW.md for the full mapping.
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

## RPL (Reactive Programming Library)

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
