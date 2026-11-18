#pragma once
#include <stddef.h>
#include <stdint.h>

// Apple Notification Center Service: pure protocol logic (no BLE), host-tested.
// Spec: https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification
namespace ancs {

enum Event : uint8_t { kAdded = 0, kModified = 1, kRemoved = 2 };
enum Flag : uint8_t { kSilent = 1, kImportant = 2, kPreExisting = 4, kPositiveAction = 8, kNegativeAction = 16 };
enum Category : uint8_t {
    kOther = 0, kIncomingCall = 1, kMissedCall = 2, kVoicemail = 3, kSocial = 4, kSchedule = 5, kEmail = 6,
    kNews = 7, kHealthFitness = 8, kBusinessFinance = 9, kLocation = 10, kEntertainment = 11
};
enum Action : uint8_t { kActionPositive = 0, kActionNegative = 1 };

// Notification Source packet (8 bytes)
struct SourceEvent {
    uint8_t event, flags, category, count;
    uint32_t uid;
};
bool parseSource(const uint8_t *d, size_t n, SourceEvent &out);

constexpr size_t kAppMax = 63, kTitleMax = 95, kMessageMax = 399, kNameMax = 31;

// Control Point commands; return the number of bytes written (0 if it doesn't fit)
size_t buildGetNotification(uint32_t uid, uint8_t *out, size_t cap); // app id, title, message, date
size_t buildGetAppName(const char *appId, uint8_t *out, size_t cap);
size_t buildAction(uint32_t uid, Action a, uint8_t *out, size_t cap);

struct Attributes {
    uint32_t uid;
    char app[kAppMax + 1];
    char title[kTitleMax + 1];
    char message[kMessageMax + 1];
    char date[16]; // "yyyyMMddTHHmmSS", local time of the phone
};

// "20261116T194233" -> "19:42"; false (out untouched) if malformed
bool formatTime(const char *date, char out[6]);

// Reassembles one Data Source response that may arrive split over several GATT notifications.
// Set what you expect right before writing the Control Point command, then feed every Data Source packet.
class Assembler {
public:
    enum Result : uint8_t { kPending, kNotification, kAppName, kError };
    void expectNotification(uint32_t uid);
    void expectAppName(const char *appId);
    void reset() { m_len = 0; m_kind = kNone; }
    bool busy() const { return m_kind != kNone; }
    Result feed(const uint8_t *d, size_t n);
    const Attributes &attributes() const { return m_attr; }
    const char *appId() const { return m_appId; }   // for kAppName
    const char *appName() const { return m_name; }  // for kAppName

private:
    enum Kind : uint8_t { kNone, kWantNotification, kWantApp };
    Result tryParse();
    Kind m_kind = kNone;
    uint32_t m_uid = 0;
    char m_appId[kAppMax + 1] = {};
    char m_name[kNameMax + 1] = {};
    Attributes m_attr = {};
    uint8_t m_buf[1024];
    size_t m_len = 0;
};

// Copies UTF-8 text keeping only what the watch fonts can draw (Latin, Latin-1/Extended-A, Cyrillic,
// common punctuation, single monochrome emoji). Other scripts, flags, skin tones, ZWJ/variation
// selectors are dropped (a ZWJ sequence shows as its parts), whitespace runs collapse to one space
// (newlines kept when keepNewlines), output is trimmed and never cut inside a character.
void sanitize(const char *in, size_t n, char *out, size_t cap, bool keepNewlines);

// Human name for a known bundle id, nullptr if unknown
const char *knownAppName(const char *appId);

enum class Haptic : uint8_t { kDefault, kTelegram, kWhatsApp, kSlack, kMail, kCalendar, kCall };
Haptic hapticFor(const char *appId, uint8_t category);

// Last notifications, newest (highest uid) first (ADR 0005: last 20, RAM only)
struct Item {
    uint32_t uid;
    char when[6];      // "HH:MM" from the phone
    uint8_t category, flags;
    Haptic haptic;
    bool unread;       // arrived live and not looked at yet
    char app[kNameMax + 1];   // display name
    char title[kTitleMax + 1];
    char message[kMessageMax + 1];
};

class Store {
public:
    static constexpr int kCapacity = 20;
    void put(const Item &it);         // insert by uid (iOS uids grow); same uid replaces; full -> oldest dropped
    bool remove(uint32_t uid);
    int count() const { return m_count; }
    const Item &at(int i) const { return m_items[i]; } // 0 = newest
    const Item *find(uint32_t uid) const;
    int unreadCount() const;
    void markRead(uint32_t uid);
    void markAllRead();
    void clear() { m_count = 0; }

private:
    Item m_items[kCapacity];
    int m_count = 0;
};

} // namespace ancs
