# AywGram 远端元数据协议 v1

[English](remote-metadata.EN.md)

AywGram 通过固定的公开 Telegram 频道分发链接预览改写规则和自动 Inline Bot 查询规则。频道身份配置位于 `ayu/features/link_rules/ayu_remote_metadata.h`。正式构建必须同时写入：

- 频道用户名 `aywmeta`，不带 `@`；
- 频道永久数字 ID，填写裸 `ChannelId`，不带 Bot API 的 `-100` 前缀。

例如 Bot API 返回 `-1001234567890`，源码中的 `kChannelId` 应填写 `1234567890`。当数字 ID 为零、用户名解析结果与固定 ID 不一致、频道不存在，或客户端连接 Test DC 时，客户端会拒绝下载远端元数据。

数字 ID 是永久信任锚。即使将来修改公开用户名，也应继续使用原数字 ID，并在源码中同步更新用户名。客户端更新元数据时不会加入频道、启动 Bot、发送消息或创建可见会话。更新失败时继续使用最后一份完整通过校验的缓存。

## 发布事务

必须先发布全部 payload 分片，最后发布 manifest。不要原地编辑当前已生效的 generation。每次发布必须使用单调递增的 `revision`。

Manifest 示例：

```text
#aywmeta{"schema":1,"revision":42,"published_at":1787850000,"pagepreview":{"parts":1,"message_ids":[120],"sha256":"<64 位小写十六进制>"},"inlinebot":{"parts":1,"message_ids":[121],"sha256":"<64 位小写十六进制>"}}
```

每条 payload 消息包含 canonical UTF-8 JSON 的一个 Base64URL 编码分片。`part` 从 1 开始编号：

```text
#pagepreview{"schema":1,"revision":42,"part":1,"parts":1,"encoding":"base64url","data":"<payload>"}
#inlinebot{"schema":1,"revision":42,"part":1,"parts":1,"encoding":"base64url","data":"<payload>"}
```

`sha256` 对按顺序拼接后的已解码 payload 原始字节计算，而不是对 Base64 文本或消息 envelope 计算。每类 payload 最多 32 个分片，解码后的 JSON 最多 128 KiB。

出现以下任一情况时，客户端拒绝整次 generation：

- 分片缺失、重复或顺序信息不一致；
- manifest、分片的 revision 不一致；
- SHA-256 不匹配；
- encoding 未知或 Base64URL 非法；
- envelope 无效或 payload 超过限制。

某一条规则本身无效时，只禁用该规则，其余通过验证的规则仍然可用。`pagepreview` 和 `inlinebot` 两类 payload 始终作为一个事务原子应用。

## Page Preview payload

示例：

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
        {
          "regex": "^https?://(?:www\\.)?x\\.com",
          "replace": "https://fixupx.com"
        }
      ]
    }
  ]
}
```

字段说明：

- `id`：稳定 ASCII 标识，最长 96 字符。修改 ID 会丢失用户为旧 ID 保存的启用状态。
- `name`：设置界面使用的显示名称。
- `enabled`：是否启用，省略时视为启用。
- `host_kind`：`exact` 或 `regex`，匹配不区分大小写。
- `host`：要匹配的 host；`regex` 模式最长 512 字符。
- `steps`：按顺序执行的正则替换步骤，最多 8 项。
- `query_policy`：`preserve` 或 `strip`，默认 `preserve`。
- `fragment_policy`：`preserve` 或 `strip`，默认 `strip`。

第一条已启用且匹配的规则生效。消息正文始终保留原始 URL，只有网页预览查询使用改写后的 URL。改写后的预览请求失败时，同一预览请求最多使用原始 URL 自动重试一次。

改写结果必须满足：

- 使用 HTTPS；
- 包含有效 host；
- 不含用户名、密码或控制字符；
- 总长度不超过 8 KiB。

单个 regex 最长 512 字符，单个 replacement 最长 2048 字符。

## Inline Bot payload

示例：

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
      "rules": [
        "https?://(?:www\\.)?(?:x|twitter)\\.com/[^\\s]+/status/\\d+"
      ]
    }
  ]
}
```

远端规则必须同时提供 `username` 和 `bot_id`。客户端解析 username 后会核对数字 ID；身份不一致时不会信任替代 Bot。每个 Bot 最多包含 8 条不区分大小写的正则表达式，并按数组顺序匹配。

只有正则的完整匹配结果，即 match 0，会作为 query 发送给 Inline Bot；输入框中其它文字不会被发送。即使正则写得过宽，匹配结果也必须能独立解析为符合以下要求的 URL，否则客户端丢弃该命中：

- HTTP 或 HTTPS；
- 包含有效 host；
- 不含用户名、密码或控制字符；
- 长度不超过 8 KiB。

用户手动输入的 `@bot query` 始终优先于自动规则。自动 Inline Bot 查询默认关闭，并且每台设备都必须单独完成隐私确认。云同步恢复的开关不能绕过本机确认。

## 规则优先级与本地规则

规则匹配顺序为：

1. 用户本地规则；
2. `@aywmeta` 远端规则；
3. AywGram 内置的 Preview fallback。

远端规则与内置规则使用相同 ID 时，远端规则覆盖内置版本。本地规则支持新增、编辑、删除、排序以及启用或禁用。远端和内置规则内容只读，但用户可以通过稳定 ID 禁用它们。

本地编辑器保存以下 JSON 对象。两个 `rules` 数组使用前文相同的规则对象，但不含外层 payload 的 `schema`。数组顺序就是匹配优先级。删除对象即删除本地规则；将 `enabled` 设为 `false` 会保留规则但停止匹配。

```json
{
  "pagepreview": {
    "rules": []
  },
  "inlinebot": {
    "rules": []
  },
  "disabled_remote": [
    "remote-rule-id"
  ],
  "disabled_bundled": [
    "twitter"
  ]
}
```

新增本地 Inline Bot 规则时，`bot_id` 可以省略或设为 `0`。保存时客户端会解析 `username`，确认目标是支持 Inline Mode 的 Bot，并写入永久数字 ID。对于已经保存了非零 ID 的规则，如果 username 后来解析到另一个 ID，客户端会拒绝保存替代身份。

`disabled_remote` 同时作用于两类远端 payload 的稳定 ID，因此 page-preview 和 Inline Bot 的远端规则 ID 必须全局唯一。当前内置 Preview 规则 ID 包括：`twitter`、`x`、`tiktok`、`reddit`、`instagram` 和 `pixiv`。

本地规则、规则顺序以及远端/内置禁用表参与 AywGram 设置云同步。频道消息、远端 payload 缓存、刷新错误和设备隐私确认不参与同步。

## 发布检查清单

1. 将每类 payload 规范化为无 BOM、包含 `schema: 1` 的 UTF-8 JSON。
2. 将原始 JSON 字节切分成不超过 32 个分片。
3. 分别对每个原始分片进行 Base64URL 编码；可以省略 padding。
4. 发布全部 `#pagepreview` 和 `#inlinebot` 消息，并按 part 顺序记录 message ID。
5. 对按顺序拼接的已解码原始分片计算 SHA-256。
6. 最后发布 `#aywmeta` manifest。至此新 revision 才成为可发现的完整事务。
7. 删除旧消息前，至少测试一次全新客户端、持有旧缓存的离线客户端，以及缺少一个分片时的失败行为。

消息中的 marker 前不能包含说明文字。正则字符串需要先进行 JSON 转义，再把整个 payload 编码为 Base64URL。例如正则 `\d+` 在 JSON 中应写成 `"\\d+"`。规则按数组顺序求值，应把更具体的 host 和 path 规则放在前面。

本地公开元数据缓存位于：

```text
tdata/ayu_remote_metadata.json
```

缓存只包含公开规则 payload，并在两类 payload 均通过验证后使用 `QSaveFile` 原子替换。缓存本身不是远端身份信任来源；新下载数据仍然必须来自固定频道身份，并通过 manifest、限制和 hash 校验。

## 回滚与兼容策略

需要回滚时，使用新的、更高 revision 重新发布上一份已知正常的数据。不要复用或降低 revision；客户端会忽略不高于本地最后验证 revision 的 manifest。

客户端拒绝未知的 manifest 或 payload schema。schema 1 可以新增不会改变现有语义的可选字段；如果需要修改规则语义、编码方式或安全限制，应定义新 schema，并在客户端迁移代码中继续提供对 v1 的读取兼容。
