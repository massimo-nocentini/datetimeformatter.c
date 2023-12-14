

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <time.h>
#include <math.h>
#include <locale.h>

#include "datetimeformatter.h"

long floorDiv(long x, long y)
{
    long r = x / y;
    // if the signs are different and modulo not zero, round down
    if ((x ^ y) < 0 && (r * y != x))
    {
        r--;
    }
    return r;
}

long floorMod(long x, long y)
{
    long mod = x % y;
    // if the signs are different and modulo not zero, adjust result
    if ((x ^ y) < 0 && mod != 0)
    {
        mod += y;
    }
    return mod;
}

static calendar_t PATTERN_INDEX_TO_CALENDAR_FIELD[] = {
    ERA,
    YEAR,
    MONTH,
    df_DATE,
    HOUR_OF_DAY,
    HOUR_OF_DAY,
    MINUTE,
    SECOND,
    MILLISECOND,
    DAY_OF_WEEK,
    DAY_OF_YEAR,
    DAY_OF_WEEK_IN_MONTH,
    WEEK_OF_YEAR,
    WEEK_OF_MONTH,
    AM_PM,
    HOUR,
    HOUR,
    ZONE_OFFSET,
    ZONE_OFFSET,
    WEEK_YEAR,       // Pseudo Calendar field
    ISO_DAY_OF_WEEK, // Pseudo Calendar field
    ZONE_OFFSET,
    MONTH};

buffer_t *new_buffer(size_t size)
{
    buffer_t *b = (buffer_t *)malloc(sizeof(buffer_t));

    b->size = size;
    b->length = 0;
    b->buffer = (char_t *)malloc(sizeof(char_t) * size);

    return b;
}

void free_buffer(buffer_t *B)
{
    if (B != NULL)
    {
        free(B->buffer);
        free(B);
    }
}

void add_char(buffer_t *B, char_t c)
{
    assert(B->length < B->size);

    B->buffer[B->length++] = c;
}

void add_buffer(buffer_t *B, buffer_t *C)
{
    int l = C->length;
    char_t *another = C->buffer;

    for (int i = 0; i < l; i++)
        add_char(B, another[i]);
}

int triple_shift(int n, int s)
{
    return n >= 0 ? n >> s : (n >> s) + (2 << ~s);
}

int encode(int tag, int length, buffer_t *buffer, char *error)
{
    if (tag == PATTERN_ISO_ZONE && length >= 4)
    {
        sprintf(error, "invalid ISO 8601 format: length=%d", length);
        return 1;
    }
    if (length < 255)
    {
        add_char(buffer, (char_t)(tag << 8 | length));
    }
    else
    {
        add_char(buffer, (char_t)((tag << 8) | 0xff));
        add_char(buffer, (char_t)triple_shift(length, 16));
        add_char(buffer, (char_t)(length & 0xffff));
    }

    return 0;
}

int dtf_compile(const char *pattern, buffer_t **compiledCodeRef, char *error)
{
    int length = strlen(pattern);

    bool inQuote = false;

    buffer_t *compiledCode = new_buffer(length * 2); // new StringBuilder(length * 2);
    buffer_t *tmpBuffer = NULL;

    int count = 0, tagcount = 0;
    int lastTag = -1; //, prevTag = -1;

    for (int i = 0; i < length; i++)
    {
        char c = pattern[i];

        if (c == '\'')
        {
            // '' is treated as a single quote regardless of being in a quoted section.
            // if ((i + 1) < length)
            if (i < length - 1)
            {
                c = pattern[i + 1];
                if (c == '\'')
                {
                    i++;
                    if (count != 0)
                    {
                        if (encode(lastTag, count, compiledCode, error))
                            return 1;

                        tagcount++;
                        // prevTag = lastTag;
                        lastTag = -1;
                        count = 0;
                    }
                    if (inQuote)
                    {
                        add_char(tmpBuffer, (char_t)c);
                    }
                    else
                    {
                        add_char(compiledCode, (char_t)(TAG_QUOTE_ASCII_CHAR << 8 | c));
                    }
                    continue;
                }
            }
            if (!inQuote)
            {
                if (count != 0)
                {
                    if (encode(lastTag, count, compiledCode, error))
                        return 1;

                    tagcount++;
                    // prevTag = lastTag;
                    lastTag = -1;
                    count = 0;
                }
                if (tmpBuffer == NULL)
                {
                    tmpBuffer = new_buffer(length); // new StringBuilder(length);
                }
                else
                {
                    tmpBuffer->length = 0; // tmpBuffer.setLength(0);
                }
                inQuote = true;
            }
            else
            {
                int len = tmpBuffer->length;
                if (len == 1)
                {
                    char_t ch = tmpBuffer->buffer[0];
                    if (ch < 128)
                    {
                        add_char(compiledCode, (char_t)(TAG_QUOTE_ASCII_CHAR << 8 | ch));
                    }
                    else
                    {
                        add_char(compiledCode, (char_t)(TAG_QUOTE_CHARS << 8 | 1));
                        add_char(compiledCode, ch);
                    }
                }
                else
                {
                    if (encode(TAG_QUOTE_CHARS, len, compiledCode, error))
                        return 1;

                    add_buffer(compiledCode, tmpBuffer);
                }
                inQuote = false;
            }
            continue;
        }
        if (inQuote)
        {
            add_char(tmpBuffer, (char_t)c);
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        {
            if (count != 0)
            {
                if (encode(lastTag, count, compiledCode, error))
                    return 1;

                tagcount++;
                // prevTag = lastTag;
                lastTag = -1;
                count = 0;
            }
            if (c < 128)
            {
                // In most cases, c would be a delimiter, such as ':'.
                add_char(compiledCode, (char_t)(TAG_QUOTE_ASCII_CHAR << 8 | c));
            }
            else
            {
                // Take any contiguous non-ASCII alphabet characters and
                // put them in a single TAG_QUOTE_CHARS.
                int j;
                for (j = i + 1; j < length; j++)
                {
                    char d = pattern[j];
                    if (d == '\'' || ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z')))
                    {
                        break;
                    }
                }

                if (encode(TAG_QUOTE_CHARS, j - i, compiledCode, error))
                    return 1;

                for (; i < j; i++)
                {
                    add_char(compiledCode, (char_t)pattern[i]);
                }
                i--;
            }
            continue;
        }

        int tag = strchr(patternChars, c) - patternChars;
        if (tag < 0)
        {
            sprintf(error, "Illegal pattern character '%c'", c);
            return 1;
        }
        if (lastTag == -1 || lastTag == tag)
        {
            lastTag = tag;
            count++;
            continue;
        }

        if (encode(lastTag, count, compiledCode, error))
            return 1;

        tagcount++;
        // prevTag = lastTag;
        lastTag = tag;
        count = 1;
    }

    if (inQuote)
    {
        sprintf(error, "Unterminated quote");
        return 1;
    }

    if (count != 0)
    {
        if (encode(lastTag, count, compiledCode, error))
            return 1;

        tagcount++;
        // prevTag = lastTag;
    }

    // bool forceStandaloneForm = (tagcount == 1 && prevTag == PATTERN_MONTH);

    free_buffer(tmpBuffer);

    *compiledCodeRef = compiledCode;

    return 0;
}

int calendar_get(lua_State *L, tm_t tm, int field, int *v, char *output)
{
    *v = -1;
    struct tm *info = tm.tm;

    switch (field)
    {
    case ERA:
        *v = info->tm_year + 1900 >= 0 ? 1 : 0;
        break;
    case YEAR:
        *v = info->tm_year + 1900;
        break;
    case MONTH:
        *v = info->tm_mon;
        break;
    case df_DATE:
        *v = info->tm_mday;
        break;
    case HOUR_OF_DAY:
        *v = info->tm_hour;
        break;
    case MINUTE:
        *v = info->tm_min;
        break;
    case SECOND:
        *v = info->tm_sec;
        break;
    case MILLISECOND:
        sprintf(output, "MILLISECOND calendar field isn't supported.");
        return 1;
    case DAY_OF_WEEK:
        *v = info->tm_wday;
        break;
    case DAY_OF_YEAR:
        *v = info->tm_yday + 1;
        break;
    case DAY_OF_WEEK_IN_MONTH:
        sprintf(output, "DAY_OF_WEEK_IN_MONTH calendar field isn't supported.");
        return 1;
    case WEEK_OF_YEAR:
        sprintf(output, "WEEK_OF_YEAR calendar field isn't supported.");
        return 1;
    case WEEK_OF_MONTH:
        sprintf(output, "WEEK_OF_MONTH calendar field isn't supported.");
        return 1;
    case AM_PM:
        *v = info->tm_hour < 12 ? 0 : 1;
        break;
    case HOUR:
        *v = info->tm_hour % 12;
        break;
    case ZONE_OFFSET:
        *v = tm.zone_offset;
        break;
    case WEEK_YEAR:
        sprintf(output, "WEEK_YEAR calendar field isn't supported.");
        return 1;
    case ISO_DAY_OF_WEEK:
        *v = info->tm_wday + 1;
        break;
    case DST_OFFSET:
        *v = info->tm_isdst;
        break;
    default:
        sprintf(output, "Generic calendar field %d isn't supported.", field);
        return 1;
    }
    return 0;
}

// void calendar_getfield_at(lua_State *L, int date_table_index, const char *field, int value, char **current)
// {
//     lua_getfield(L, date_table_index, field);
//     lua_len(L, -1);
//     int length = lua_tointeger(L, -1);
//     int n = 2; // 2 values to pop from the stack.

//     if (value < length)
//     {
//         // current = eras[value];
//         int lua_type = lua_geti(L, -2, value + 1);
//         assert(lua_type == LUA_TSTRING);
//         *current = (char *)lua_tostring(L, -1);
//         n++; // one more string to be popped out.
//     }

//     lua_pop(L, n);
// }

void sprintf0d(luaL_Buffer *sb, int value, int width)
{
    long d = value;
    if (d < 0)
    {
        luaL_addchar(sb, '-');
        d = -d;
        --width;
    }
    int n = 10;
    for (int i = 2; i < width; i++)
    {
        n *= 10;
    }
    for (int i = 1; i < width && d < n; i++)
    {
        luaL_addchar(sb, '0');
        n /= 10;
    }
    luaL_addchar(sb, (char)d);
}

int toISODayOfWeek(int calendarDayOfWeek)
{
    return calendarDayOfWeek == SUNDAY ? 7 : calendarDayOfWeek - 1;
}

static const int LEAST_MAX_VALUES[] = {
    1,         // ERA
    292269054, // YEAR
    DECEMBER,  // MONTH
    52,        // WEEK_OF_YEAR
    4,         // WEEK_OF_MONTH
    28,        // DAY_OF_MONTH
    365,       // DAY_OF_YEAR
    SATURDAY,  // DAY_OF_WEEK
    4,         // DAY_OF_WEEK_IN
    PM,        // AM_PM
    11,        // HOUR
    23,        // HOUR_OF_DAY
    59,        // MINUTE
    59,        // SECOND
    999,       // MILLISECOND
    // ZONE_OFFSET, // ZONE_OFFSET
    // DST_OFFSET,   // DST_OFFSET (historical least maximum)
};

static const int MAX_VALUES[] = {
    1,         // ERA
    292278994, // YEAR
    DECEMBER,  // MONTH
    53,        // WEEK_OF_YEAR
    6,         // WEEK_OF_MONTH
    31,        // DAY_OF_MONTH
    366,       // DAY_OF_YEAR
    SATURDAY,  // DAY_OF_WEEK
    6,         // DAY_OF_WEEK_IN
    PM,        // AM_PM
    11,        // HOUR
    23,        // HOUR_OF_DAY
    59,        // MINUTE
    59,        // SECOND
    999,       // MILLISECOND
    // 14 * ONE_HOUR, // ZONE_OFFSET
    // 2 * ONE_HOUR   // DST_OFFSET (double summer time)
};

int calendar_getLeastMaximum(int i) { return LEAST_MAX_VALUES[i]; }

int calendar_getMaximum(int i) { return MAX_VALUES[i]; }

void zeroPaddingNumber(lua_State *L, int value, int minDigits, int maxDigits, luaL_Buffer *buffer)
{
    // Optimization for 1, 2 and 4 digit numbers. This should
    // cover most cases of formatting date/time related items.
    // Note: This optimization code assumes that maxDigits is
    // either 2 or Integer.MAX_VALUE (maxIntCount in format()).

    // try
    {
        char zeroDigit = 0;
        if (zeroDigit == 0)
        {
            // zeroDigit = ((DecimalFormat)numberFormat).getDecimalFormatSymbols().getZeroDigit();
            zeroDigit = '0';
        }
        if (value >= 0)
        {
            if (value < 100 && minDigits >= 1 && minDigits <= 2)
            {
                if (value < 10)
                {
                    if (minDigits == 2)
                    {
                        luaL_addchar(buffer, zeroDigit);
                    }
                    luaL_addchar(buffer, (char)(zeroDigit + value));
                }
                else
                {
                    luaL_addchar(buffer, (char)(zeroDigit + value / 10));
                    luaL_addchar(buffer, (char)(zeroDigit + value % 10));
                }
                return;
            }
            else if (value >= 1000 && value < 10000)
            {
                if (minDigits == 4)
                {
                    luaL_addchar(buffer, (char)(zeroDigit + value / 1000));
                    value %= 1000;
                    luaL_addchar(buffer, (char)(zeroDigit + value / 100));
                    value %= 100;
                    luaL_addchar(buffer, (char)(zeroDigit + value / 10));
                    luaL_addchar(buffer, (char)(zeroDigit + value % 10));
                    return;
                }
                if (minDigits == 2 && maxDigits == 2)
                {
                    zeroPaddingNumber(L, value % 100, 2, 2, buffer);
                    return;
                }
            }
        }
    }
    // catch (Exception e)
    // {
    // }

    // numberFormat.setMinimumIntegerDigits(minDigits);
    // numberFormat.setMaximumIntegerDigits(maxDigits);
    // numberFormat.format((long)value, buffer, DontCareFieldPosition.INSTANCE);
    lua_pushinteger(L, value);
    luaL_addvalue(buffer);
}

int subFormat(lua_State *L, tm_t tm, int patternCharIndex, int count, luaL_Buffer *buffer, char *output)
{
    struct tm *info = tm.tm;
    // int lua_type;
    char strftime_buffer[STRFTIME_BUFFER_LENGTH];

    int maxIntCount = INT_MAX;
    char *current = NULL;
    // int beginOffset = luaL_bufflen(buffer);

    int field = PATTERN_INDEX_TO_CALENDAR_FIELD[patternCharIndex];
    int value;
    int failed = 0;

    int zone_o, dst_o;

    if (field == WEEK_YEAR)
    {
        // if (calendar.isWeekDateSupported())
        // lua_type = lua_getfield(L, date_table_index, "isWeekDateSupported");
        // if (lua_type == LUA_TBOOLEAN && lua_toboolean(L, -1))
        // {
        //     // value = calendar.getWeekYear();
        //     lua_type = lua_getfield(L, date_table_index, "getWeekYear");
        //     assert(lua_type == LUA_TNUMBER);
        //     value = lua_tointeger(L, -1);
        //     lua_pop(L, 1);
        // }
        // else
        {
            // use calendar year 'y' instead
            patternCharIndex = PATTERN_YEAR;
            field = PATTERN_INDEX_TO_CALENDAR_FIELD[patternCharIndex];
            failed = calendar_get(L, tm, field, &value, output);
            if (failed)
                return failed;
        }
        // lua_pop(L, 1);
    }
    else if (field == ISO_DAY_OF_WEEK)
    {
        failed = calendar_get(L, tm, DAY_OF_WEEK, &value, output);
        if (failed)
            return failed;

        value = toISODayOfWeek(value);
    }
    else
    {
        failed = calendar_get(L, tm, field, &value, output);
        if (failed)
            return failed;
    }

    // int style = (count >= 4) ? LONG_C : SHORT_C;
    // if (!useDateFormatSymbols && field < ZONE_OFFSET && patternCharIndex != PATTERN_MONTH_STANDALONE)
    // {
    //     current = calendar.getDisplayName(field, style, locale);
    // }

    // Note: zeroPaddingNumber(L, ) assumes that maxDigits is either
    // 2 or maxIntCount. If we make any changes to this,
    // zeroPaddingNumber(L, ) must be fixed.

    switch (patternCharIndex)
    {
    case PATTERN_ERA: // 'G'
        // if (useDateFormatSymbols)
        {
            // const char **eras = formatData.getEras();
            // calendar_getfield_at(L, date_table_index, "getEras", value, &current);
            // current = eras[value];
        }
        if (current == NULL)
        {
            current = "";
        }
        break;

    case PATTERN_WEEK_YEAR: // 'Y'
    case PATTERN_YEAR:      // 'y'
        // lua_type = lua_getfield(L, date_table_index, "isGregorianCalendar");
        // if (lua_type == LUA_TBOOLEAN && lua_toboolean(L, -1))
        {
            if (count != 2)
            {
                zeroPaddingNumber(L, value, count, maxIntCount, buffer);
            }
            else
            {
                zeroPaddingNumber(L, value, 2, 2, buffer);
            } // clip 1996 to 96
        }
        // else
        // {
        //     if (current == NULL)
        //     {
        //         zeroPaddingNumber(L, value, style == LONG_C ? 1 : count, maxIntCount, buffer);
        //     }
        // }
        // lua_pop(L, 1);
        break;

    case PATTERN_MONTH_STANDALONE: // 'L'
    case PATTERN_MONTH:            // 'M' (context sensitive)
        // if (useDateFormatSymbols)
        {
            if (count >= 4)
            {
                // months = formatData.getMonths();
                // current = months[value];
                // calendar_getfield_at(L, date_table_index, "getMonths", value, &current);

                strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%B", info);
                current = strftime_buffer;
            }
            else if (count == 3)
            {
                // months = formatData.getShortMonths();
                // current = months[value];
                // calendar_getfield_at(L, date_table_index, "getShortMonths", value, &current);
                strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%b", info);
                current = strftime_buffer;
            }
        }
        // else
        // {
        //     if (count < 3)
        //     {
        //         current = NULL;
        //     }
        //     else if (forceStandaloneForm)
        //     {
        //         current = calendar.getDisplayName(field, style | 0x8000, locale);
        //         if (current == NULL)
        //         {
        //             current = calendar.getDisplayName(field, style, locale);
        //         }
        //     }
        // }
        if (current == NULL)
        {
            zeroPaddingNumber(L, value + 1, count, maxIntCount, buffer);
        }
        break;

        // case PATTERN_MONTH_STANDALONE: // 'L'
        //     assert(current == NULL);
        //     // if (locale == NULL)
        //     {
        //         const char **months;
        //         if (count >= 4)
        //         {
        //             months = formatData.getMonths();
        //             current = months[value];
        //         }
        //         else if (count == 3)
        //         {
        //             months = formatData.getShortMonths();
        //             current = months[value];
        //         }
        //     }
        //     // else
        //     // {
        //     //     if (count >= 3)
        //     //     {
        //     //         current = calendar.getDisplayName(field, style | 0x8000, locale);
        //     //     }
        //     // }
        //     if (current == NULL)
        //     {
        //         zeroPaddingNumber(L, value + 1, count, maxIntCount, buffer);
        //     }
        //     break;

    case PATTERN_HOUR_OF_DAY1: // 'k' 1-based.  eg, 23:59 + 1 hour =>> 24:59
        if (current == NULL)
        {
            // if (value == 0)
            // {
            //     zeroPaddingNumber(L, calendar_getMaximum(HOUR_OF_DAY) + 1, count, maxIntCount, buffer);
            // }
            // else
            {
                zeroPaddingNumber(L, value, count, maxIntCount, buffer);
            }
        }
        break;

    case PATTERN_DAY_OF_WEEK: // 'E'
        // if (useDateFormatSymbols)
        {
            if (count >= 4)
            {
                // weekdays = formatData.getWeekdays();
                // current = weekdays[value];
                // calendar_getfield_at(L, date_table_index, "getWeekdays", value, &current);
                strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%A", info);
                current = strftime_buffer;
            }
            else
            { // count < 4, use abbreviated form if exists
                // weekdays = formatData.getShortWeekdays();
                // current = weekdays[value];
                // calendar_getfield_at(L, date_table_index, "getShortWeekdays", value, &current);
                strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%a", info);
                current = strftime_buffer;
            }
        }
        break;

    case PATTERN_AM_PM: // 'a'
        // if (useDateFormatSymbols)
        {
            // const char **ampm = formatData.getAmPmStrings();
            // current = ampm[value];
            // calendar_getfield_at(L, date_table_index, "getAmPmStrings", value, &current);
            strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%p", info);
            current = strftime_buffer;
        }
        break;

    case PATTERN_HOUR1: // 'h' 1-based.  eg, 11PM + 1 hour =>> 12 AM
        if (current == NULL)
        {
            // if (value == 0)
            // {
            //     zeroPaddingNumber(L, calendar_getLeastMaximum(HOUR) + 1, count, maxIntCount, buffer);
            // }
            // else
            {
                zeroPaddingNumber(L, value, count, maxIntCount, buffer);
            }
        }
        break;

    case PATTERN_ZONE_NAME: // 'z'
        if (current == NULL)
        {
            // if (formatData.locale == NULL || formatData.isZoneStringsSet)
            // {
            //     int zoneIndex =
            //         formatData.getZoneIndex(calendar.getTimeZone().getID());
            //     if (zoneIndex == -1)
            //     {
            //         value = calendar_get(L, date_table_index, ZONE_OFFSET) +
            //                 calendar_get(L, date_table_index, DST_OFFSET);
            //         buffer.append(ZoneInfoFile.toCustomID(value));
            //     }
            //     else
            //     {
            //         int index = (calendar_get(L, date_table_index, DST_OFFSET) == 0) ? 1 : 3;
            //         if (count < 4)
            //         {
            //             // Use the short name
            //             index++;
            //         }
            //         String[][] zoneStrings = formatData.getZoneStringsWrapper();
            //         buffer.append(zoneStrings[zoneIndex][index]);
            //     }
            // }
            // else
            {
                // TimeZone tz = calendar.getTimeZone();
                // int tzstyle = (count < 4 ? TimeZone.SHORT_C : TimeZone.LONG_C);
                // buffer.append(tz.getDisplayName(daylight, tzstyle, formatData.locale));
                // bool daylight = (calendar_get(L, date_table_index, DST_OFFSET) != 0);

                // char *s;
                // if (count >= 4)
                // {
                //     calendar_getfield_at(L, date_table_index, "getTimeZone", 1, &s);
                // }
                // else
                // {
                //     calendar_getfield_at(L, date_table_index, "getShortTimeZone", 1, &s);
                // }

                if (tm.localtime)
                {
                    strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%Z", info);
                    luaL_addstring(buffer, strftime_buffer);
                }
                else
                {
                    luaL_addstring(buffer, tm.zone_name);
                }
            }
        }
        break;

    case PATTERN_ZONE_VALUE: // 'Z' ("-/+hhmm" form)
        // value = (calendar_get(L, info, ZONE_OFFSET) + calendar_get(L, info, DST_OFFSET)) / 60000;

        // int width = 4;
        // if (value >= 0)
        // {
        //     add_char(buffer, '+');
        // }
        // else
        // {
        //     width++;
        // }

        // int num = (value / 60) * 100 + (value % 60);
        // sprintf0d(buffer, num, width);

        strftime(strftime_buffer, STRFTIME_BUFFER_LENGTH, "%z", info);
        luaL_addstring(buffer, strftime_buffer);

        break;

    case PATTERN_ISO_ZONE: // 'X'

        failed = calendar_get(L, tm, ZONE_OFFSET, &zone_o, output);
        if (failed)
            return failed;

        failed = calendar_get(L, tm, DST_OFFSET, &dst_o, output);
        if (failed)
            return failed;

        value = zone_o + dst_o;

        if (value == 0)
        {
            luaL_addchar(buffer, 'Z');
            break;
        }

        value /= 60000;
        if (value >= 0)
        {
            luaL_addchar(buffer, '+');
        }
        else
        {
            luaL_addchar(buffer, '-');
            value = -value;
        }

        sprintf0d(buffer, value / 60, 2);
        if (count == 1)
        {
            break;
        }

        if (count == 3)
        {
            luaL_addchar(buffer, ':');
        }
        sprintf0d(buffer, value % 60, 2);
        break;

    default:
        // case PATTERN_DAY_OF_MONTH:         // 'd'
        // case PATTERN_HOUR_OF_DAY0:         // 'H' 0-based.  eg, 23:59 + 1 hour =>> 00:59
        // case PATTERN_MINUTE:               // 'm'
        // case PATTERN_SECOND:               // 's'
        // case PATTERN_MILLISECOND:          // 'S'
        // case PATTERN_DAY_OF_YEAR:          // 'D'
        // case PATTERN_DAY_OF_WEEK_IN_MONTH: // 'F'
        // case PATTERN_WEEK_OF_YEAR:         // 'w'
        // case PATTERN_WEEK_OF_MONTH:        // 'W'
        // case PATTERN_HOUR0:                // 'K' eg, 11PM + 1 hour =>> 0 AM
        // case PATTERN_ISO_DAY_OF_WEEK:      // 'u' pseudo field, Monday = 1, ..., Sunday = 7
        if (current == NULL)
        {
            zeroPaddingNumber(L, value, count, maxIntCount, buffer);
        }
        break;
    } // switch (patternCharIndex)

    if (current != NULL)
    {
        luaL_addstring(buffer, current);
    }

    // int fieldID = PATTERN_INDEX_TO_DATE_FORMAT_FIELD[patternCharIndex];
    // field_t f = PATTERN_INDEX_TO_DATE_FORMAT_FIELD_ID[patternCharIndex];

    // formatted(fieldID, f, f, beginOffset, luaL_bufflen(buffer), buffer);
    return failed;
}

int dtf_format(buffer_t *compiledPattern, time_t timer, const char *locale, int offset, const char *timezone, int local, char *output)
{
    int failed = 0;
    char_t *shifted;

    if (setlocale(LC_TIME, locale) == NULL)
    {
        sprintf(output, "Impossible to set the \"%s\" locale.", locale);
        return 1;
    }

    tm_t tm; //  allocate the main structure to hold all the data.

    tm.zone_name = timezone;
    tm.zone_offset = offset;
    tm.localtime = local;

    if (local)
    {
        tm.tm = localtime(&timer);
    }
    else
    {
        timer += offset;
        tm.tm = gmtime(&timer);
    }

    lua_State *L = luaL_newstate();
    luaL_Buffer toAppendTo;
    luaL_buffinit(L, &toAppendTo);

    for (int i = 0; i < compiledPattern->length;)
    {
        int tag = triple_shift(compiledPattern->buffer[i], 8);
        int count = compiledPattern->buffer[i++] & 0xff;
        if (count == 255)
        {
            count = compiledPattern->buffer[i++] << 16;
            count |= compiledPattern->buffer[i++];
        }

        switch (tag)
        {
        case TAG_QUOTE_ASCII_CHAR:
            luaL_addchar(&toAppendTo, (char)count);
            break;

        case TAG_QUOTE_CHARS:
            // luaL_addlstring(&toAppendTo, (char *)(compiledPattern->buffer + i), count);
            shifted = compiledPattern->buffer + i;
            for (int j = 0; j < count; j++)
            {
                luaL_addchar(&toAppendTo, (char)shifted[j]);
            }
            i += count;
            break;

        default:
            failed = subFormat(L, tm, tag, count, &toAppendTo, output);
            if (failed)
                return failed;
            else
                break;
        }
    }

    luaL_pushresult(&toAppendTo);
    strcpy(output, lua_tostring(L, -1));
    lua_close(L);

    return failed;
}
