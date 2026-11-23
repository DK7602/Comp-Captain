#include "cc/TextFormat.h"

#include <cmath>
#include <limits>

namespace cc::text
{
    std::string_view trim (std::string_view s)
    {
        while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
            s.remove_prefix (1);
        while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            s.remove_suffix (1);
        return s;
    }

    bool parseDouble (std::string_view s, double& out)
    {
        s = trim (s);
        if (s.empty() || s.size() > 40)
            return false;

        size_t i = 0;
        bool negative = false;
        if (s[i] == '+' || s[i] == '-')
            negative = s[i++] == '-';

        double value = 0.0;
        bool digits = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        {
            value = value * 10.0 + (s[i++] - '0');
            digits = true;
        }
        if (i < s.size() && s[i] == '.')
        {
            ++i;
            double scale = 0.1;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                value += (s[i++] - '0') * scale;
                scale *= 0.1;
                digits = true;
            }
        }
        if (! digits)
            return false;

        if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
        {
            ++i;
            bool expNegative = false;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                expNegative = s[i++] == '-';
            int exponent = 0;
            bool expDigits = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9' && exponent < 400)
            {
                exponent = exponent * 10 + (s[i++] - '0');
                expDigits = true;
            }
            if (! expDigits)
                return false;
            value *= std::pow (10.0, expNegative ? -exponent : exponent);
        }

        if (i != s.size() || ! std::isfinite (value))
            return false;
        out = negative ? -value : value;
        return true;
    }

    bool parseInt (std::string_view s, std::int64_t& out)
    {
        s = trim (s);
        if (s.empty() || s.size() > 19)
            return false;
        size_t i = 0;
        bool negative = false;
        if (s[i] == '+' || s[i] == '-')
            negative = s[i++] == '-';
        if (i == s.size())
            return false;
        std::int64_t value = 0;
        for (; i < s.size(); ++i)
        {
            if (s[i] < '0' || s[i] > '9')
                return false;
            value = value * 10 + (s[i] - '0');
        }
        out = negative ? -value : value;
        return true;
    }

    std::string formatFixed (double value, int decimals)
    {
        if (! std::isfinite (value))
            value = 0.0;
        decimals = decimals < 0 ? 0 : (decimals > 9 ? 9 : decimals);
        std::int64_t scale = 1;
        for (int d = 0; d < decimals; ++d)
            scale *= 10;

        const bool negative = value < 0;
        const double limit = (double) std::numeric_limits<std::int64_t>::max() / (double) scale / 2.0;
        double magnitude = std::fabs (value);
        if (magnitude > limit)
            magnitude = limit;
        const auto scaled = (std::int64_t) std::llround (magnitude * (double) scale);

        std::string out = std::to_string (scaled / scale);
        if (decimals > 0)
        {
            std::string frac = std::to_string (scaled % scale);
            out += "." + std::string ((size_t) decimals - frac.size(), '0') + frac;
        }
        if (negative && scaled != 0)
            out.insert (out.begin(), '-');
        return out;
    }

    std::string escape (std::string_view s)
    {
        std::string out;
        out.reserve (s.size());
        for (char c : s)
        {
            if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else
                out += c;
        }
        return out;
    }

    std::string unescape (std::string_view s)
    {
        std::string out;
        out.reserve (s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                const char n = s[++i];
                out += n == 'n' ? '\n' : (n == 'r' ? '\r' : n);
            }
            else
            {
                out += s[i];
            }
        }
        return out;
    }

    void forEachKeyValue (std::string_view text, const std::function<void (std::string_view, std::string_view)>& fn)
    {
        while (! text.empty())
        {
            const auto end = text.find ('\n');
            const auto line = trim (text.substr (0, end));
            text = end == std::string_view::npos ? std::string_view {} : text.substr (end + 1);

            const auto eq = line.find ('=');
            if (eq == std::string_view::npos)
                continue;
            fn (trim (line.substr (0, eq)), trim (line.substr (eq + 1)));
        }
    }
}
