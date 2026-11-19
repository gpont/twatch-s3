#include <unity.h>
#include <string.h>
#include "ancs.h"

using namespace ancs;

void setUp() {}
void tearDown() {}

static Item item(uint32_t uid)
{
    Item it = {};
    it.uid = uid;
    return it;
}

void test_parse_source_added()
{
    const uint8_t p[] = {0, kImportant | kNegativeAction, kSocial, 3, 0x78, 0x56, 0x34, 0x12};
    SourceEvent e;
    TEST_ASSERT_TRUE(parseSource(p, sizeof(p), e));
    TEST_ASSERT_EQUAL_UINT8(kAdded, e.event);
    TEST_ASSERT_EQUAL_UINT8(kSocial, e.category);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, e.uid);
    TEST_ASSERT_TRUE(e.flags & kNegativeAction);
}

void test_parse_source_rejects_short_and_bad()
{
    const uint8_t shortP[] = {0, 0, 0, 0, 1, 2, 3};
    const uint8_t bad[] = {7, 0, 0, 0, 1, 2, 3, 4};
    SourceEvent e;
    TEST_ASSERT_FALSE(parseSource(shortP, sizeof(shortP), e));
    TEST_ASSERT_FALSE(parseSource(bad, sizeof(bad), e));
}

void test_build_get_notification()
{
    uint8_t b[32];
    size_t n = buildGetNotification(0x01020304, b, sizeof(b));
    const uint8_t want[] = {0, 4, 3, 2, 1, 0, 1, kTitleMax & 0xFF, kTitleMax >> 8, 3, kMessageMax & 0xFF, kMessageMax >> 8, 5, 6, 7};
    TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b, n);
}

void test_build_app_name_and_action()
{
    uint8_t b[64];
    size_t n = buildGetAppName("a.b", b, sizeof(b));
    const uint8_t want[] = {1, 'a', '.', 'b', 0, 0};
    TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b, n);
    n = buildAction(5, kActionNegative, b, sizeof(b));
    const uint8_t act[] = {2, 5, 0, 0, 0, 1};
    TEST_ASSERT_EQUAL_UINT32(6, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(act, b, n);
}

// response: cmd 0, uid, (0,"ph.telegra.Telegraph"), (1,"Мама"), (3,"Привет\nкак дела?")
static size_t makeResponse(uint32_t uid, uint8_t *b)
{
    size_t n = 0;
    b[n++] = 0;
    b[n++] = uid; b[n++] = uid >> 8; b[n++] = uid >> 16; b[n++] = uid >> 24;
    auto attr = [&](uint8_t id, const char *s) {
        size_t l = strlen(s);
        b[n++] = id; b[n++] = l; b[n++] = l >> 8;
        memcpy(b + n, s, l); n += l;
    };
    attr(0, "ph.telegra.Telegraph");
    attr(1, "Мама");
    attr(3, "Привет\nкак дела?");
    attr(5, "20261116T194233");
    attr(6, "Approve");
    attr(7, "Deny \xF0\x9F\x98\x80");
    return n;
}

void test_assembler_whole_packet()
{
    Assembler a;
    uint8_t b[200];
    size_t n = makeResponse(42, b);
    a.expectNotification(42);
    TEST_ASSERT_EQUAL(Assembler::kNotification, a.feed(b, n));
    TEST_ASSERT_EQUAL_STRING("ph.telegra.Telegraph", a.attributes().app);
    TEST_ASSERT_EQUAL_STRING("Мама", a.attributes().title);
    TEST_ASSERT_EQUAL_STRING("Привет\nкак дела?", a.attributes().message);
    TEST_ASSERT_EQUAL_STRING("20261116T194233", a.attributes().date);
    TEST_ASSERT_EQUAL_STRING("Approve", a.attributes().positive);
    TEST_ASSERT_EQUAL_STRING("Deny \xF0\x9F\x98\x80", a.attributes().negative);
    TEST_ASSERT_FALSE(a.busy());
}

void test_format_time()
{
    char t[6] = "x";
    TEST_ASSERT_TRUE(formatTime("20261116T194233", t));
    TEST_ASSERT_EQUAL_STRING("19:42", t);
    TEST_ASSERT_FALSE(formatTime("garbage", t));
}

void test_store_orders_by_uid()
{
    static Store s;
    s.put(item(5));
    s.put(item(9));
    s.put(item(7)); // a pre-existing one fetched late still lands in order
    TEST_ASSERT_EQUAL_UINT32(9, s.at(0).uid);
    TEST_ASSERT_EQUAL_UINT32(7, s.at(1).uid);
    TEST_ASSERT_EQUAL_UINT32(5, s.at(2).uid);
}

void test_store_full_drops_oldest_and_ignores_older()
{
    static Store s;
    for (uint32_t i = 11; i <= 30; i++) s.put(item(i));
    s.put(item(3)); // older than all 20 kept
    TEST_ASSERT_EQUAL_UINT32(11, s.at(Store::kCapacity - 1).uid);
    s.put(item(20)); // replaces, no change in count
    s.put(item(31));
    TEST_ASSERT_EQUAL(Store::kCapacity, s.count());
    TEST_ASSERT_EQUAL_UINT32(31, s.at(0).uid);
    TEST_ASSERT_EQUAL_UINT32(12, s.at(Store::kCapacity - 1).uid);
}

void test_assembler_fragmented()
{
    Assembler a;
    uint8_t b[200];
    size_t n = makeResponse(7, b);
    a.expectNotification(7);
    size_t i = 0;
    Assembler::Result r = Assembler::kPending;
    while (i < n) { // 20-byte ATT payloads (default MTU)
        size_t k = n - i < 20 ? n - i : 20;
        r = a.feed(b + i, k);
        i += k;
        if (i < n) TEST_ASSERT_EQUAL(Assembler::kPending, r);
    }
    TEST_ASSERT_EQUAL(Assembler::kNotification, r);
    TEST_ASSERT_EQUAL_STRING("Мама", a.attributes().title);
}

void test_assembler_wrong_uid_is_error()
{
    Assembler a;
    uint8_t b[200];
    size_t n = makeResponse(8, b);
    a.expectNotification(9);
    TEST_ASSERT_EQUAL(Assembler::kError, a.feed(b, n));
}

void test_assembler_ignores_unrequested()
{
    Assembler a;
    uint8_t b[200];
    size_t n = makeResponse(8, b);
    TEST_ASSERT_EQUAL(Assembler::kPending, a.feed(b, n));
}

void test_assembler_app_name()
{
    Assembler a;
    a.expectAppName("com.x.y");
    const uint8_t p1[] = {1, 'c', 'o', 'm', '.', 'x'};
    const uint8_t p2[] = {'.', 'y', 0, 0, 5, 0, 'H', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_EQUAL(Assembler::kPending, a.feed(p1, sizeof(p1)));
    TEST_ASSERT_EQUAL(Assembler::kAppName, a.feed(p2, sizeof(p2)));
    TEST_ASSERT_EQUAL_STRING("com.x.y", a.appId());
    TEST_ASSERT_EQUAL_STRING("Hello", a.appName());
}

void test_sanitize_keeps_emoji_and_collapses_space()
{
    char out[64];
    const char *in = "  Hi \xF0\x9F\x98\x80  there\xE2\x80\xA6 ";
    sanitize(in, strlen(in), out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("Hi \xF0\x9F\x98\x80 there\xE2\x80\xA6", out);
}

void test_sanitize_drops_emoji_modifiers()
{
    char out[64];
    // thumbs up + skin tone, heart + VS16, man ZWJ laptop, flag (regional indicators), CJK
    const char *in = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD \xE2\x9D\xA4\xEF\xB8\x8F \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB \xF0\x9F\x87\xB7\xF0\x9F\x87\xB8 \xE4\xBD\xA0";
    sanitize(in, strlen(in), out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x91\x8D \xE2\x9D\xA4 \xF0\x9F\x91\xA8\xF0\x9F\x92\xBB", out);
}

void test_sanitize_keeps_serbian_and_cyrillic()
{
    char out[64];
    const char *in = "Ćao, šta radiš? Ђорђе љ";
    sanitize(in, strlen(in), out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING(in, out);
}

void test_sanitize_newlines()
{
    char out[64];
    const char *in = "a\r\n\nb\nc";
    sanitize(in, strlen(in), out, sizeof(out), true);
    TEST_ASSERT_EQUAL_STRING("a\nb\nc", out);
    sanitize(in, strlen(in), out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("a b c", out);
}

void test_sanitize_truncates_on_char_boundary()
{
    char out[6]; // room for 5 bytes: "Пр" (4 bytes) + one more 2-byte char would overflow
    const char *in = "Привет";
    sanitize(in, strlen(in), out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("Пр", out);
}

void test_sanitize_respects_length_not_nul()
{
    char out[16];
    sanitize("abcdef", 3, out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

void test_haptic_and_names()
{
    TEST_ASSERT_EQUAL(Haptic::kCall, hapticFor("com.apple.mobilephone", kIncomingCall));
    TEST_ASSERT_EQUAL(Haptic::kTelegram, hapticFor("ph.telegra.Telegraph", kSocial));
    TEST_ASSERT_EQUAL(Haptic::kMail, hapticFor("com.google.Gmail", kEmail));
    TEST_ASSERT_EQUAL(Haptic::kDefault, hapticFor("com.unknown", kOther));
    TEST_ASSERT_EQUAL_STRING("Telegram", knownAppName("ph.telegra.Telegraph"));
    TEST_ASSERT_NULL(knownAppName("com.unknown"));
}

void test_store_newest_first_and_capacity()
{
    static Store s;
    for (uint32_t i = 1; i <= 25; i++) s.put(item(i));
    TEST_ASSERT_EQUAL(Store::kCapacity, s.count());
    TEST_ASSERT_EQUAL_UINT32(25, s.at(0).uid);
    TEST_ASSERT_EQUAL_UINT32(6, s.at(Store::kCapacity - 1).uid);
}

void test_store_replace_and_remove()
{
    static Store s;
    s.put(item(1));
    s.put(item(2));
    Item upd = item(1);
    upd.category = 9;
    s.put(upd);
    TEST_ASSERT_EQUAL(2, s.count());
    TEST_ASSERT_EQUAL_UINT8(9, s.find(1)->category);
    TEST_ASSERT_TRUE(s.remove(2));
    TEST_ASSERT_FALSE(s.remove(2));
    TEST_ASSERT_EQUAL(1, s.count());
    TEST_ASSERT_EQUAL_UINT32(1, s.at(0).uid);
}

void test_store_unread()
{
    static Store s;
    Item a = item(1), b = item(2);
    a.unread = b.unread = true;
    s.put(a);
    s.put(b);
    s.put(item(3));
    TEST_ASSERT_EQUAL(2, s.unreadCount());
    s.markRead(2);
    TEST_ASSERT_EQUAL(1, s.unreadCount());
    s.remove(1);
    TEST_ASSERT_EQUAL(0, s.unreadCount());
    Item c = item(4);
    c.unread = true;
    s.put(c);
    s.markAllRead();
    TEST_ASSERT_EQUAL(0, s.unreadCount());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_source_added);
    RUN_TEST(test_parse_source_rejects_short_and_bad);
    RUN_TEST(test_build_get_notification);
    RUN_TEST(test_build_app_name_and_action);
    RUN_TEST(test_assembler_whole_packet);
    RUN_TEST(test_format_time);
    RUN_TEST(test_store_orders_by_uid);
    RUN_TEST(test_store_full_drops_oldest_and_ignores_older);
    RUN_TEST(test_assembler_fragmented);
    RUN_TEST(test_assembler_wrong_uid_is_error);
    RUN_TEST(test_assembler_ignores_unrequested);
    RUN_TEST(test_assembler_app_name);
    RUN_TEST(test_sanitize_keeps_emoji_and_collapses_space);
    RUN_TEST(test_sanitize_drops_emoji_modifiers);
    RUN_TEST(test_sanitize_keeps_serbian_and_cyrillic);
    RUN_TEST(test_sanitize_newlines);
    RUN_TEST(test_sanitize_truncates_on_char_boundary);
    RUN_TEST(test_sanitize_respects_length_not_nul);
    RUN_TEST(test_haptic_and_names);
    RUN_TEST(test_store_newest_first_and_capacity);
    RUN_TEST(test_store_replace_and_remove);
    RUN_TEST(test_store_unread);
    return UNITY_END();
}
