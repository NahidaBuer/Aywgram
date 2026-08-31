# AywGram Desktop 会话迁移

AywGram Desktop 可与 Pyrogram 会话串和 Mithka 兼容的备份交换登录会话。功能位于登录流程，以及 **设置 → AywGram → 其他 → 会话迁移**。

会话串和 JSON 导出均为明文认证凭据。任何取得它们的人都可能使用其中的 MTProto 授权密钥，而无需接收短信验证码。复制、导出和导入前都会要求明确确认；只有 Telegram 验证会话和账户身份成功后，应用才会把导入数据写入本地存储。

## Pyrogram 会话串

二进制载荷与 Pyrogram 的 `SESSION_STRING_FORMAT = ">BI?256sQ?"` 完全一致：

| 偏移 | 大小 | 内容 |
| ---: | ---: | --- |
| 0 | 1 | 数据中心 ID，无符号整数 |
| 1 | 4 | API ID，无符号大端整数 |
| 5 | 1 | 测试模式布尔值 |
| 6 | 256 | MTProto 授权密钥 |
| 262 | 8 | 用户 ID，无符号大端整数 |
| 270 | 1 | 机器人布尔值 |

载荷固定为 271 字节。规范输出为未填充的 base64url，因此长度为 362 个字符。解码器同时接受标准 Base64、可选填充符和前后或嵌入的空白字符；会拒绝无效布尔字节、零 ID、全零或长度不对的密钥，以及格式错误的 Base64。为兼容格式会解析机器人会话，但桌面端拒绝导入它们。

导入串中的 API ID 会被校验但不会采用。MTProto 授权密钥属于数据中心而非 API ID，因此运行中的 AywGram 仍使用自身构建时配置的应用凭据。

## Mithka JSON 信封

桌面端写入格式标识符 `mithka.tdlib.session_string.v2.explicit_consent`，并接受旧版 `mithka.tdlib.session_string.v1`。导出对象包含 `id`、`accountId`、`slot`、`userId`、`name`、可空的 `phone`、`storage`、`createdAt` 与 `sessionString`。

导出使用 `slot: 0`、`storage: "local"` 和带毫秒的 UTC ISO-8601 时间戳。声明的用户 ID 必须与会话串中的用户 ID 完全一致；未知格式和字段不一致的信封会被拒绝。

## 数据中心迁移

会话串中的 DC 指向导入密钥所属的数据中心，可能与账户主 DC 不同。AywGram 会在临时 MTProto 实例中挂载该密钥并调用 `updates.getState`。`303 USER_MIGRATE_N` 会交给导入器处理，而不会被通常的自动重试掩盖。

需要迁移时，AywGram 在密钥原始 DC 调用 `auth.exportAuthorization`，并在主 DC 调用 `auth.importAuthorization`。导出请求再次返回迁移即视为失败。最后，`users.getUsers(inputUserSelf)` 必须返回 ID 与会话串相符的自身用户；只有此时才会把已验证的主 DC、配置、持久化密钥和自身用户提交到本地存储。取消、任一错误或 120 秒超时都会销毁临时实例，不会添加账户。
