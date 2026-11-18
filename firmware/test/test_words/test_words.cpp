#include <unity.h>
#include <string.h>
#include "word_clock.h"
#include "ru_text.h"

void setUp() {}
void tearDown() {}

static void phraseIs(int h, int m, const char *expected)
{
    char buf[128];
    word_clock::phrase(h, m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

void test_phrase_examples()
{
    phraseIs(14, 37, "ДВА ЧАСА ТРИДЦАТЬ СЕМЬ МИНУТ ДНЯ");
    phraseIs(9, 0, "ДЕВЯТЬ ЧАСОВ РОВНО УТРА");
    phraseIs(13, 1, "ЧАС ОДНА МИНУТА ДНЯ");
    phraseIs(0, 15, "ДВЕНАДЦАТЬ ЧАСОВ ПЯТНАДЦАТЬ МИНУТ НОЧИ");
    phraseIs(12, 0, "ДВЕНАДЦАТЬ ЧАСОВ РОВНО ДНЯ");
    phraseIs(19, 22, "СЕМЬ ЧАСОВ ДВАДЦАТЬ ДВЕ МИНУТЫ ВЕЧЕРА");
    phraseIs(3, 44, "ТРИ ЧАСА СОРОК ЧЕТЫРЕ МИНУТЫ НОЧИ");
    phraseIs(5, 11, "ПЯТЬ ЧАСОВ ОДИННАДЦАТЬ МИНУТ УТРА");
    phraseIs(23, 59, "ОДИННАДЦАТЬ ЧАСОВ ПЯТЬДЕСЯТ ДЕВЯТЬ МИНУТ ВЕЧЕРА");
}

void test_every_word_fits_grid()
{
    for (int h = 0; h < 24; h++)
        for (int m = 0; m < 60; m++) {
            word_clock::Grid g;
            word_clock::layout(h, m, g);
            for (int r = 0; r < word_clock::kRows; r++) TEST_ASSERT_EQUAL(word_clock::kCols, (int)strlen(g.rows[r]) / 2); // UTF-8 Cyrillic: 2 bytes
        }
}

void test_layout_lights_the_phrase_in_order()
{
    word_clock::Grid g;
    word_clock::layout(14, 37, g);
    // concatenating lit cells row by row must give the phrase without spaces
    char lit[128] = "";
    for (int r = 0; r < word_clock::kRows; r++)
        for (int c = 0; c < word_clock::kCols; c++)
            if (g.lit[r] & (1u << c)) strncat(lit, g.rows[r] + c * 2, 2);
    TEST_ASSERT_EQUAL_STRING("ДВАЧАСАТРИДЦАТЬСЕМЬМИНУТДНЯ", lit);
}

void test_plural()
{
    TEST_ASSERT_EQUAL(ru_text::One, ru_text::plural(1));
    TEST_ASSERT_EQUAL(ru_text::One, ru_text::plural(21));
    TEST_ASSERT_EQUAL(ru_text::Few, ru_text::plural(3));
    TEST_ASSERT_EQUAL(ru_text::Few, ru_text::plural(44));
    TEST_ASSERT_EQUAL(ru_text::Many, ru_text::plural(11));
    TEST_ASSERT_EQUAL(ru_text::Many, ru_text::plural(15));
    TEST_ASSERT_EQUAL(ru_text::Many, ru_text::plural(0));
    TEST_ASSERT_EQUAL(ru_text::Many, ru_text::plural(112));
}

void test_number_formatting()
{
    char b[24];
    ru_text::thousands(5412, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("5 412", b);
    ru_text::thousands(800, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("800", b);
    ru_text::thousands(1234567, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("1 234 567", b);
    ru_text::euro(20.1f, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("+20,10 €", b);
    ru_text::euro(-3.456f, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("−3,46 €", b);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_phrase_examples);
    RUN_TEST(test_every_word_fits_grid);
    RUN_TEST(test_layout_lights_the_phrase_in_order);
    RUN_TEST(test_plural);
    RUN_TEST(test_number_formatting);
    return UNITY_END();
}
