#pragma once
#ifndef KCONVERTCALENDAR_H
#define KCONVERTCALENDAR_H

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <forward_list>
#include <bitset>

// 封装localtime_s/localtime_r，统一参数
// Microsoft CRT 中的 localtime_s 实现与 C 标准不兼容，因为它有相反的参数顺序。
// macos和linux中的函数名不一样

#if defined _WIN32 || _WIN64
#define LOCALTIME_R(a, b) localtime_s(b, a)
#else
#define LOCALTIME_R(a, b) localtime_r(a, b)
#endif
namespace Calendar
{
#include "initcalendardate.h"
    // 日期,为了简化tm结构体,默认为公历
    class Date
    {
    public:
        enum class Type
        {
            NONGLI,
            GONGLI
        };

    private:
        // 距离1900的年数
        uint8_t d_year = 0;
        // 月[0,11]
        uint8_t d_month = 0;
        // 天[1,31]
        uint8_t d_day = 0;
        uint16_t d_diff_days = 0; // 农历的第几天[1,360]
        // 当年的闰几月
        uint8_t leap_month = 0;
        Type d_type = Type::GONGLI;

        static std::forward_list<int> decode(std::bitset<12> month_code, uint8_t leap_month, bool big)
        {
            std::forward_list<int> arr;
            // 例如010010101110，依次表示1、2、3...大小月情况
            for (std::size_t i = 0; i < month_code.size(); i++)
            {
                if (month_code[i])
                {
                    arr.push_front(30);
                }
                else
                {
                    arr.push_front(29);
                }
            }
            // 插入闰月的天数
            if (leap_month)
            {
                auto it = arr.begin();
                std::advance(it, leap_month - 1);
                arr.insert_after(it, big ? 30 : 29);
            }
            return arr;
        }

        // 30以内的数字转为中文字符串,用于月份和日期
        template <typename Int>
        static std::string md2s(Int i)
        {
            if (i == 10)
                return u8"十";
            if (i == 20)
                return u8"廿十";
            if (i == 30)
                return u8"三十";
            if (i < 10)
                return ZHNUMS[i];
            if (i > 10 && i < 20)
                return std::string(u8"十").append(ZHNUMS[i % 10]);
            // i > 20 && i < 30
            return std::string(u8"廿").append(ZHNUMS[i % 10]);
        }

    public:
        // 设置公历日期
        Date &setGongli(const uint8_t &y, const uint8_t &m, const uint8_t &d) noexcept
        {
            d_year = y;
            d_month = m;
            d_day = d;
            leap_month = 0;
            d_type = Type::GONGLI;
            return *this;
        }

        Date() = default;

        Date(const uint8_t &y, const uint8_t &m, const uint8_t &d) noexcept
        {
            this->setGongli(y, m, d);
        }

        Date(tm t) noexcept : Date(t.tm_year, t.tm_mon, t.tm_mday)
        {
        }

        Date(const uint8_t arr[3]) : Date(arr[0], arr[1], arr[2])
        {
        }

        operator tm() noexcept
        {
            tm tm_;
            tm_.tm_year = d_year; // 年，由于tm结构体存储的是从1900年开始的时间，所以tm_year为int临时变量减去1900。
            tm_.tm_mon = d_month; // 月，由于tm结构体的月份存储范围为0-11，所以tm_mon为int临时变量减去1。
            tm_.tm_mday = d_day;  // 日。
            tm_.tm_hour = 0;      // 时。
            tm_.tm_min = 0;       // 分。
            tm_.tm_sec = 0;       // 秒。
            tm_.tm_isdst = 0;     // 非夏令时。
            return tm_;
        }

        // 判断是否农历
        bool isZh() noexcept
        {
            return d_type == Type::NONGLI;
        }

        // 判断闰月
        bool isLeap() noexcept
        {
            return isZh() && d_month == leap_month;
        }

        uint16_t year() noexcept
        {
            return d_year + 1900;
        }

        uint8_t month() noexcept
        {
            if (isZh() && leap_month && d_month >= leap_month)
            {
                return d_month;
            }
            return d_month + 1;
        }

        uint8_t day() noexcept
        {
            return d_day;
        }

        Type type()
        {
            return d_type;
        }

        bool operator<(const Date &d) noexcept
        {
            if ((*this) == d || d_year > d.d_year || d_month > d.d_month || d_day > d.d_day)
            {
                return false;
            }
            return true;
        }

        bool operator==(const Date &d) noexcept
        {
            return (d_year == d.d_year && d_month == d.d_month && d_day == d.d_day);
        }

        std::string yearStr()
        {
            auto _year = year();
            if (!isZh())
            {
                std::stringstream ss;
                ss << _year;
                return ss.str();
            }
            std::string str;
            for (int x = (int)std::pow(10, 3); x > 0; x /= 10)
            {
                auto d = std::div(_year, x);
                _year = d.rem;              // 余数
                str.append(ZHNUMS[d.quot]); // 商
            }
            return str;
        }

        std::string monthStr()
        {
            std::string str;
            int m = month();
            if (!isZh())
            {
                std::stringstream ss;
                if (m < 10)
                {
                    ss << '0';
                }
                ss << m;
                ss >> str;
                return str;
            }
            if (m == 1)
            {
                return u8"正";
            }
            else if (m == 12)
            {
                return u8"腊";
            }

            if (isLeap())
            {
                str = u8"润";
            }
            str.append(md2s(m));
            return str;
        }

        std::string dayStr()
        {
            std::string str;
            if (!isZh())
            {
                std::stringstream ss;
                int d = day();
                if (d < 10)
                {
                    ss << '0';
                }
                ss << d;
                ss >> str;
                return ss.str();
            }
            auto d = day();
            if (d < 10)
                return std::string(u8"初").append(md2s(d));
            return md2s(d);
        }

        // 生肖
        std::string zodiacStr()
        {
            if (!isZh())
                return "";
            return shengxiao[d_year % 12];
        }

        // 干支纪年
        std::string gzStr()
        {
            if (!isZh())
                return "";
            uint16_t y = d_year + 36;
            std::string str{TIANGAN[y % 10]};
            str.append(DIZHI[y % 12]);
            return str;
        }

        std::string toStr()
        {
            if (!isZh())
            {
                return yearStr() + '-' + monthStr() + '-' + dayStr();
            }
            std::string str = yearStr() + u8"年" + monthStr() + u8"月" + dayStr() + " " + gzStr() + u8" (";
            str.append(zodiacStr() + u8"年)");
            return str;
        }

        // 将日期改为公历
        Date &gongli()
        {
            if (d_type == Type::GONGLI)
            {
                return *this;
            }
            // 现在还是农历
            // 当前农历新年对应的时间戳
            auto first_time = Date(CHINESENEWYEAR[d_year]).timestamp();
            // 公历的时间戳
            time_t times = (d_diff_days - 1) * static_cast<time_t>(DAY_TIMES) + first_time;
            tm _t = {};
            LOCALTIME_R(&times, &_t);
            setGongli(_t.tm_year, _t.tm_mon, _t.tm_mday); // 转为公历
            return *this;
        }

        // 将日期改为农历
        Date &nongli()
        {
            if (d_type == Type::NONGLI)
            {
                return *this;
            }
            auto ts = timestamp(); // 现在还是公历
            if (*this < Date(CHINESENEWYEAR[d_year]))
            {
                d_year--; // 农历新年还没有到，所以年份-1
            }
            // 下面开始转为农历
            d_type = Type::NONGLI;
            auto year_code = CHINESEYEARCODE[d_year];
            // 闰月
            leap_month = (year_code & 0xf);
            auto days_arr = decode((year_code & 0xffff) >> 4, leap_month, year_code >> 16);
            d_diff_days = static_cast<uint16_t>((ts - Date(CHINESENEWYEAR[d_year]).timestamp()) / DAY_TIMES + 1); // 当前农历年的第几天
            // 已经度过的天数
            uint16_t days = 0;
            d_month = 0;
            for (auto &&i : days_arr)
            {
                // 月份已经到了
                if ((days + i) >= d_diff_days)
                {
                    d_day = d_diff_days - days;
                    break;
                }
                d_month++;
                days += i;
            }
            return *this;
        }

        // 根据当前日期返回时间戳
        time_t timestamp()
        {
            gongli(); // 先转为公历
            tm t = (tm)(*this);
            return mktime(&t);
        }
    };
}

inline std::ostream &operator<<(std::ostream &out, const typename Calendar::Date::Type &t)
{
    using Type = typename Calendar::Date::Type;
    switch (t)
    {
    case Type::GONGLI:
        out << "公";
        break;
    case Type::NONGLI:
        out << u8"农";
        break;
    }
    return out;
}

#endif // KCONVERTCALENDAR_H
