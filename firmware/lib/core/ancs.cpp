#include "ancs.h"
#include <string.h>

namespace ancs {

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

bool parseSource(const uint8_t *d, size_t n, SourceEvent &out)
{
    if (n < 8 || d[0] > kRemoved) return false;
    out = {d[0], d[1], d[2], d[3], le32(d + 4)};
    return true;
}

// Attribute ids
static constexpr uint8_t kAttrApp = 0, kAttrTitle = 1, kAttrMessage = 3, kAttrDate = 5, kAttrDisplayName = 0;
static constexpr int kAttrCount = 4;

size_t buildGetNotification(uint32_t uid, uint8_t *out, size_t cap)
{
    if (cap < 13) return 0;
    out[0] = 0; // GetNotificationAttributes
    put32(out + 1, uid);
    out[5] = kAttrApp;
    out[6] = kAttrTitle;
    out[7] = kTitleMax & 0xFF;
    out[8] = kTitleMax >> 8;
    out[9] = kAttrMessage;
    out[10] = kMessageMax & 0xFF;
    out[11] = kMessageMax >> 8;
    out[12] = kAttrDate;
    return 13;
}

size_t buildGetAppName(const char *appId, uint8_t *out, size_t cap)
{
    size_t l = strlen(appId);
    if (cap < l + 3) return 0;
    out[0] = 1; // GetAppAttributes
    memcpy(out + 1, appId, l + 1);
    out[l + 2] = kAttrDisplayName;
    return l + 3;
}

size_t buildAction(uint32_t uid, Action a, uint8_t *out, size_t cap)
{
    if (cap < 6) return 0;
    out[0] = 2; // PerformNotificationAction
    put32(out + 1, uid);
    out[5] = a;
    return 6;
}

void Assembler::expectNotification(uint32_t uid)
{
    m_kind = kWantNotification;
    m_uid = uid;
    m_len = 0;
}

void Assembler::expectAppName(const char *appId)
{
    m_kind = kWantApp;
    strncpy(m_appId, appId, kAppMax);
    m_appId[kAppMax] = 0;
    m_len = 0;
}

Assembler::Result Assembler::feed(const uint8_t *d, size_t n)
{
    if (m_kind == kNone) return kPending; // nothing asked: stray packet
    if (m_len + n > sizeof(m_buf)) { reset(); return kError; }
    memcpy(m_buf + m_len, d, n);
    m_len += n;
    return tryParse();
}

// Raw attribute text -> fixed buffer (sanitized)
static void copyAttr(const uint8_t *p, size_t len, char *out, size_t cap, bool keepNewlines)
{
    sanitize((const char *)p, len, out, cap, keepNewlines);
}

Assembler::Result Assembler::tryParse()
{
    if (m_kind == kWantNotification) {
        if (m_len < 5) return kPending;
        if (m_buf[0] != 0 || le32(m_buf + 1) != m_uid) { reset(); return kError; }
        size_t pos = 5;
        Attributes a = {};
        a.uid = m_uid;
        for (int i = 0; i < kAttrCount; i++) {
            if (pos + 3 > m_len) return kPending;
            uint8_t id = m_buf[pos];
            size_t len = m_buf[pos + 1] | (m_buf[pos + 2] << 8);
            if (pos + 3 + len > m_len) return kPending;
            const uint8_t *v = m_buf + pos + 3;
            if (id == kAttrApp) {
                size_t k = len < kAppMax ? len : kAppMax;
                memcpy(a.app, v, k);
                a.app[k] = 0;
            } else if (id == kAttrTitle) {
                copyAttr(v, len, a.title, sizeof(a.title), false);
            } else if (id == kAttrMessage) {
                copyAttr(v, len, a.message, sizeof(a.message), true);
            } else if (id == kAttrDate) {
                size_t k = len < sizeof(a.date) - 1 ? len : sizeof(a.date) - 1;
                memcpy(a.date, v, k);
                a.date[k] = 0;
            }
            pos += 3 + len;
        }
        m_attr = a;
        reset();
        return kNotification;
    }
    if (m_kind == kWantApp) {
        if (m_len < 1) return kPending;
        if (m_buf[0] != 1) { reset(); return kError; }
        const uint8_t *z = (const uint8_t *)memchr(m_buf + 1, 0, m_len - 1);
        if (!z) return kPending;
        size_t pos = z - m_buf + 1;
        if (pos + 3 > m_len) return kPending;
        size_t len = m_buf[pos + 1] | (m_buf[pos + 2] << 8);
        if (pos + 3 + len > m_len) return kPending;
        copyAttr(m_buf + pos + 3, len, m_name, sizeof(m_name), false);
        reset();
        return kAppName;
    }
    return kPending;
}

bool formatTime(const char *date, char out[6])
{
    if (strlen(date) < 13 || date[8] != 'T') return false;
    for (int i = 9; i <= 12; i++)
        if (date[i] < '0' || date[i] > '9') return false;
    out[0] = date[9]; out[1] = date[10]; out[2] = ':'; out[3] = date[11]; out[4] = date[12]; out[5] = 0;
    return true;
}

// ---- text ----
// Monochrome emoji font (f_emoji_*, fallback of the text fonts): same ranges as EMOJI in tools/fonts/gen.sh
static bool emoji(uint32_t c)
{
    if ((c >= 0x1F1E6 && c <= 0x1F1FF) || (c >= 0x1F3FB && c <= 0x1F3FF)) return false; // flags, skin tones
    return c == 0x203C || c == 0x2049 || c == 0x2122 || c == 0x2139 || (c >= 0x2194 && c <= 0x21AA) ||
           (c >= 0x231A && c <= 0x23FF) || c == 0x24C2 || (c >= 0x25AA && c <= 0x25FE) || (c >= 0x2600 && c <= 0x27BF) ||
           c == 0x2934 || c == 0x2935 || (c >= 0x2B05 && c <= 0x2B55) || c == 0x3030 || c == 0x303D || c == 0x3297 ||
           c == 0x3299 || (c >= 0x1F004 && c <= 0x1F0CF) || (c >= 0x1F170 && c <= 0x1F251) || (c >= 0x1F300 && c <= 0x1FAFF);
}

static bool drawable(uint32_t c)
{
    return emoji(c) || (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0x17F) || (c >= 0x400 && c <= 0x45F) ||
           (c >= 0x2010 && c <= 0x2014) || (c >= 0x2018 && c <= 0x201E) || c == 0x2022 || c == 0x2026 ||
           c == 0x20AC || c == 0x2116 || c == 0x2212;
}

static bool isSpace(uint32_t c) { return c == ' ' || c == '\t' || c == 0xA0 || c == 0x2009 || c == 0x200A || c == 0x202F; }

// Decodes one UTF-8 sequence; returns its length (>=1), sets c = 0xFFFD for invalid input
static size_t decode(const uint8_t *p, size_t n, uint32_t &c)
{
    uint8_t b = p[0];
    size_t len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (!len || len > n) { c = 0xFFFD; return 1; }
    if (len == 1) { c = b; return 1; }
    c = b & (0x3F >> (len - 1));
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { c = 0xFFFD; return i; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    return len;
}

void sanitize(const char *in, size_t n, char *out, size_t cap, bool keepNewlines)
{
    if (!cap) return;
    const uint8_t *p = (const uint8_t *)in;
    size_t o = 0;
    bool pendingSpace = false, pendingNl = false;
    for (size_t i = 0; i < n && p[i];) {
        uint32_t c;
        size_t len = decode(p + i, n - i, c);
        i += len;
        if (c == '\n' || c == 0x2028 || c == 0x2029) {
            if (keepNewlines) pendingNl = true; else pendingSpace = true;
            continue;
        }
        if (c == '\r') continue;
        if (isSpace(c)) { pendingSpace = true; continue; }
        if (!drawable(c)) continue;
        if (o > 0 && (pendingNl || pendingSpace)) {
            if (o + 1 >= cap) break;
            out[o++] = pendingNl ? '\n' : ' ';
        }
        pendingSpace = pendingNl = false;
        if (o + len >= cap) break;
        memcpy(out + o, p + i - len, len);
        o += len;
    }
    out[o] = 0;
}

// ---- apps ----
struct KnownApp { const char *id, *name; };
static const KnownApp kApps[] = {
    {"ph.telegra.Telegraph", "Telegram"},
    {"net.whatsapp.WhatsApp", "WhatsApp"},
    {"com.tinyspeck.chatlyio", "Slack"},
    {"com.apple.mobilemail", "Почта"},
    {"com.google.Gmail", "Gmail"},
    {"com.apple.mobilecal", "Календарь"},
    {"com.google.calendar", "Google Календарь"},
    {"com.apple.MobileSMS", "Сообщения"},
    {"com.apple.mobilephone", "Телефон"},
    {"com.apple.facetime", "FaceTime"},
    {"com.apple.reminders", "Напоминания"},
    {"com.culturedcode.ThingsiPhone", "Things"},
    {"com.apple.mobilenotes", "Заметки"},
    {"com.apple.Health", "Здоровье"},
    {"com.apple.Passbook", "Wallet"},
    {"com.apple.Home", "Дом"},
    {"io.robbie.HomeAssistant", "Home Assistant"},
    {"com.viber", "Viber"},
    {"com.okta.mobile", "Okta Verify"},
    {"com.github.stormbreaker.prod", "GitHub"},
};

const char *knownAppName(const char *appId)
{
    for (auto &a : kApps)
        if (!strcmp(a.id, appId)) return a.name;
    return nullptr;
}

Haptic hapticFor(const char *appId, uint8_t category)
{
    if (category == kIncomingCall) return Haptic::kCall;
    if (!strcmp(appId, "ph.telegra.Telegraph")) return Haptic::kTelegram;
    if (!strcmp(appId, "net.whatsapp.WhatsApp")) return Haptic::kWhatsApp;
    if (!strcmp(appId, "com.tinyspeck.chatlyio")) return Haptic::kSlack;
    if (!strcmp(appId, "com.apple.mobilemail") || !strcmp(appId, "com.google.Gmail")) return Haptic::kMail;
    if (!strcmp(appId, "com.apple.mobilecal") || !strcmp(appId, "com.google.calendar")) return Haptic::kCalendar;
    return Haptic::kDefault;
}

// ---- store ----
void Store::put(const Item &it)
{
    for (int i = 0; i < m_count; i++)
        if (m_items[i].uid == it.uid) { m_items[i] = it; return; }
    int pos = 0;
    while (pos < m_count && m_items[pos].uid > it.uid) pos++;
    if (pos >= kCapacity) return; // older than everything kept
    int n = m_count < kCapacity ? m_count : kCapacity - 1; // items that survive
    memmove(&m_items[pos + 1], &m_items[pos], (n - pos) * sizeof(Item));
    m_items[pos] = it;
    m_count = n + 1;
}

bool Store::remove(uint32_t uid)
{
    for (int i = 0; i < m_count; i++) {
        if (m_items[i].uid != uid) continue;
        memmove(&m_items[i], &m_items[i + 1], (m_count - i - 1) * sizeof(Item));
        m_count--;
        return true;
    }
    return false;
}

int Store::unreadCount() const
{
    int n = 0;
    for (int i = 0; i < m_count; i++) n += m_items[i].unread;
    return n;
}

void Store::markRead(uint32_t uid)
{
    for (int i = 0; i < m_count; i++)
        if (m_items[i].uid == uid) m_items[i].unread = false;
}

void Store::markAllRead()
{
    for (int i = 0; i < m_count; i++) m_items[i].unread = false;
}

const Item *Store::find(uint32_t uid) const
{
    for (int i = 0; i < m_count; i++)
        if (m_items[i].uid == uid) return &m_items[i];
    return nullptr;
}

} // namespace ancs
