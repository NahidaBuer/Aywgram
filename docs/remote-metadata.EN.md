# AywGram remote metadata v1

[简体中文](remote-metadata.md)

AywGram distributes link-preview and automatic Inline Bot rules through the
public Telegram channel configured in
`ayu/features/link_rules/ayu_remote_metadata.h`. A production build must
contain both the username `aywmeta` and its permanent numeric channel ID. The
client refuses metadata when either identity does not match, on Test DC, or
while the numeric ID is zero.

`kChannelId` is Telegram's bare channel ID (`ChannelId::value`), not the Bot
API `-100...` form. Resolve and record it once when creating the channel; the
ID is the permanent trust anchor even if the public username later changes.

The client never joins the channel, starts a bot, sends a message, or opens a
visible conversation while updating metadata. The last fully verified payload
is cached locally and remains active when an update fails.

## Publication transaction

Publish all payload parts first and the manifest last. Do not edit an active
generation in place. Publish a new, monotonically increasing `revision`.

```text
#aywmeta{"schema":1,"revision":42,"published_at":1787850000,"pagepreview":{"parts":1,"message_ids":[120],"sha256":"<64 lowercase hex>"},"inlinebot":{"parts":1,"message_ids":[121],"sha256":"<64 lowercase hex>"}}
```

Each referenced message contains one Base64URL-encoded slice of canonical
UTF-8 JSON. Parts are numbered from one.

```text
#pagepreview{"schema":1,"revision":42,"part":1,"parts":1,"encoding":"base64url","data":"<payload>"}
#inlinebot{"schema":1,"revision":42,"part":1,"parts":1,"encoding":"base64url","data":"<payload>"}
```

`sha256` is calculated over the decoded payload after concatenating decoded
parts in order. The client accepts at most 32 parts and 128 KiB decoded JSON
per payload. A missing or duplicate part, wrong revision, invalid hash,
unknown encoding, invalid envelope, or oversized payload rejects the whole
generation. An individual malformed rule is disabled while the other verified
rules remain usable. Both payload types are committed atomically.

## Page-preview payload

```json
{
  "schema": 1,
  "rules": [
    {
      "id": "x",
      "name": "X preview",
      "enabled": true,
      "host_kind": "exact",
      "host": "x.com",
      "query_policy": "preserve",
      "fragment_policy": "strip",
      "steps": [
        { "regex": "^https?://(?:www\\.)?x\\.com", "replace": "https://fixupx.com" }
      ]
    }
  ]
}
```

`id` is a stable ASCII identifier of at most 96 characters. Changing it loses
the user's saved enable state. `host_kind` is `exact` or `regex`; matching is
case-insensitive. The first enabled matching rule wins and applies up to eight
replacement steps in order.

`query_policy` defaults to `preserve` and may be `strip`.
`fragment_policy` defaults to `strip` and may be `preserve`. The message keeps
the original URL. Only preview lookup uses the transformed URL, and a failed
transformed lookup is retried once with the original URL.

The result must be HTTPS, contain no credentials or control characters, and be
at most 8 KiB. Regex strings are limited to 512 characters and replacements to
2048 characters.

## Inline Bot payload

```json
{
  "schema": 1,
  "rules": [
    {
      "id": "x-inline",
      "name": "X via Inline Bot",
      "enabled": true,
      "username": "ExamplePreviewBot",
      "bot_id": 123456789,
      "rules": ["https?://(?:www\\.)?(?:x|twitter)\\.com/[^\\s]+/status/\\d+"]
    }
  ]
}
```

Both `username` and `bot_id` are mandatory. The client resolves the username
and rejects an identity mismatch. A bot has at most eight case-insensitive
expressions. The first match wins. Only regex match zero—the matched URL—is
sent as the query; other compose-field text is never included. A match is
discarded unless it independently parses as an HTTP or HTTPS URL with a host,
no credentials, no control characters, and a maximum length of 8 KiB.

Automatic queries are disabled by default and require consent on each device.
Cloud-restored settings cannot bypass this local consent.

## Precedence and local rules

Local rules run before remote rules; remote rules run before the bundled
preview fallback. A remote rule with the same ID shadows its bundled version.
Local rules support add, edit, delete, reorder, and enable operations. Remote
and bundled rules can only be enabled or disabled.

The local editor stores the following object. Its two `rules` arrays use the
same rule objects shown above, except the outer payload `schema` is omitted.
Array order is match priority. Deleting an object deletes that local rule;
setting `enabled` to `false` preserves it without matching it.
For a newly added local Inline Bot rule, `bot_id` may be omitted or set to
zero. Saving resolves `username`, verifies that it is an inline-capable bot,
and writes the permanent numeric ID. If a nonzero saved ID later resolves to a
different peer, the editor refuses to save the replacement identity.

```json
{
  "pagepreview": { "rules": [] },
  "inlinebot": { "rules": [] },
  "disabled_remote": ["remote-rule-id"],
  "disabled_bundled": ["twitter"]
}
```

`disabled_remote` applies to stable IDs from both remote payload classes.
IDs therefore must be unique across the page-preview and Inline Bot payloads.
The bundled IDs currently include `twitter`, `x`, `tiktok`, `reddit`,
`instagram`, and `pixiv`.

Local rules and disable state participate in AywGram settings cloud sync.
Channel messages, verified cache, refresh errors, and device consent do not.

## Publishing checklist

1. Canonicalize each payload as UTF-8 JSON with `schema: 1` and no BOM.
2. Split its raw bytes into at most 32 slices. Base64URL-encode each slice
   independently and omit padding if desired.
3. Publish every `#pagepreview` and `#inlinebot` part. Record the resulting
   message IDs in part order.
4. Compute SHA-256 over the concatenated decoded raw slices, not over the
   Base64 text or part envelopes.
5. Publish the `#aywmeta` manifest last. A client can now discover the new
   revision as one atomic generation.
6. Test a clean client, an offline client with a last-known-good cache, and a
   client where one part is missing before deleting superseded messages.

Messages may contain no prose before the marker. JSON escaping happens twice:
once inside a regex JSON string and once when the payload bytes are encoded.
For example, the regex `\d+` is written as `"\\d+"` in payload JSON. Rules
are evaluated in array order; publish more specific hosts and paths first.

The local cache is `tdata/ayu_remote_metadata.json`. It contains public rule
payloads only and is replaced with `QSaveFile` after both classes validate.
It is not a trust source by itself: newly downloaded data is still accepted
only from the pinned channel identity and a valid manifest.

## Rollback and compatibility

To roll back, publish a new revision containing the previous known-good data.
Never reuse or decrement a revision. Clients ignore a revision that is not
newer than their last verified generation.

Unknown manifest or payload schemas are rejected. Additive optional fields may
be introduced within schema 1, but changing rule meaning, encoding, or safety
limits requires a new schema and a client migration that continues to read v1.
