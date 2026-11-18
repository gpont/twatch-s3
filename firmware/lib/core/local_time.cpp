#include "local_time.h"
#include <stdlib.h>

namespace local_time {

void setTimezone(const char *posixTz)
{
    setenv("TZ", posixTz, 1);
    tzset();
}

static int32_t daysFromCivil(int y, unsigned m, unsigned d)
{
    // Howard Hinnant's algorithm, proleptic Gregorian
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

int32_t localDayNumber(time_t utc)
{
    struct tm lt;
    localtime_r(&utc, &lt);
    return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

time_t utcFromCivil(int year, int month, int day, int hour, int minute, int second)
{
    return (time_t)daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400 + hour * 3600 + minute * 60 + second;
}

int32_t minutesOfDay(time_t utc)
{
    struct tm lt;
    localtime_r(&utc, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

} // namespace local_time
