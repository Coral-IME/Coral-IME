#include <cmath>
#include <sstream>
#include "decimal_value.h"

inline int BigFloat::CharToInt(const char& val)
{
	return (val - '0');
}

inline char BigFloat::IntToChar(const int& val)
{
	return (val + '0');
}

int BigFloat::compare(const BigFloat& left, const BigFloat& right)
{
    if ((left.number_.size() - left.decimals_) > (right.number_.size() - right.decimals_))
        return 1;
    else if ((left.number_.size() - left.decimals_) < (right.number_.size() - right.decimals_))
        return 2;

    if (left.decimals_ > right.decimals_) {
        BigFloat tmp;
        tmp = right;
        while (left.decimals_ > tmp.decimals_) {
            tmp.decimals_++;
            tmp.number_.push_front('0');
        }

        for (int i = left.number_.size() - 1; i >= 0; i--) {
            if (left.number_[i] > tmp.number_[i])
                return 1;
            else if (left.number_[i] < tmp.number_[i])
                return 2;
        }
        return 0;
    }
    else if (left.decimals_ < right.decimals_) {
        BigFloat tmp;
        tmp = left;
        while (tmp.decimals_ < right.decimals_) {
            tmp.decimals_++;
            tmp.number_.push_front('0');
        }

        for (int i = tmp.number_.size() - 1; i >= 0; i--) {
            if (tmp.number_[i] > right.number_[i])
                return 1;
            else if (tmp.number_[i] < right.number_[i])
                return 2;
        }
        return 0;
    }
    else {
        for (int i = left.number_.size() - 1; i >= 0; i--) {
            if (left.number_[i] > right.number_[i])
                return 1;
            else if (left.number_[i] < right.number_[i])
                return 2;
        }
        return 0;
    }
};

BigFloat BigFloat::sum(const BigFloat& left, const BigFloat& right)
{
    BigFloat tmp;
    int carry = 0;
    size_t loopsize = (left.number_.size() > right.number_.size()) ? left.number_.size() : right.number_.size();

    for (size_t i = 0; i < loopsize; ++i) {
        int val1, val2;
        val1 = (i > left.number_.size() - 1) ? 0 : CharToInt(left.number_[i]);
        val2 = (i > right.number_.size() - 1) ? 0 : CharToInt(right.number_[i]);

        int aus = val1 + val2 + carry;

        carry = 0;
        if (aus > 9) {
            carry = 1;
            aus = aus - 10;
        }

        tmp.number_.push_back(IntToChar(aus));
    }
    if (carry != 0)
        tmp.number_.push_back(IntToChar(carry));

    return tmp;
};

BigFloat BigFloat::minus(const BigFloat& left, const BigFloat& right)
{
    BigFloat tmp;
    int carry = 0;
    int aus;

    for (size_t i = 0; i < left.number_.size(); ++i) {
        int val1, val2;
        val1 = CharToInt(left.number_[i]);
        val2 = (i > right.number_.size() - 1) ? 0 : CharToInt(right.number_[i]);

        val1 -= carry;

        if (val1 < val2) {
            aus = 10 + val1 - val2;
            carry = 1;
        }
        else {
            aus = val1 - val2;
            carry = 0;
        }

        tmp.number_.push_back(IntToChar(aus));
    }

    return tmp;
};

BigFloat BigFloat::multiply(const BigFloat& left, const BigFloat& right)
{
    BigFloat ris;
    BigFloat tmp;
    ris.number_.push_back('0');
    int carry = 0;

    for (size_t i = 0; i < right.number_.size(); ++i) {
        for (size_t k = 0; k < i; ++k) {
            tmp.number_.push_front('0');
        }

        for (size_t j = 0; j < left.number_.size(); ++j) {
            int aus = CharToInt(right.number_[i]) * CharToInt(left.number_[j]) + carry;
            carry = 0;
            if (aus > 9) {
                while (aus > 9) {
                    carry++;
                    aus -= 10;
                }
            }
            tmp.number_.push_back(IntToChar(aus));
        }

        if (carry != 0)
            tmp.number_.push_back(IntToChar(carry));
        carry = 0;

        ris = sum(ris, tmp);
        tmp.number_.clear();
    }

    return ris;
};

BigFloat::BigFloat()
{
	sign_ = '\0';
	decimals_ = 0;
	error_ = 1;
}

BigFloat::BigFloat(const char* strNum)
{
	*this = strNum;
}

BigFloat::BigFloat(const std::string& strNum)
{
    *this = strNum;
}

BigFloat::BigFloat(int Num)
{
	*this = Num;
}

BigFloat::BigFloat(double Num)
{
	*this = Num;
}

BigFloat& BigFloat::operator=(const char* strNum)
{
    number_.clear();
    decimals_ = 0;
    sign_ = '\0';
    error_ = 0;

    int count = 0;
    if (strNum[count] == '+') {
        sign_ = '+';
        count++;
    }
    else if (strNum[count] == '-') {
        sign_ = '-';
        count++;
    }
    else if (isdigit(strNum[count]))
        sign_ = '+';
    else {
        error_ = 1;
        return *this;
    }

    bool start_dec = false;

    while (strNum[count] != '\0') {
        if (!start_dec)
            if (strNum[count] == '.') {
                if (number_.empty()) {
                    sign_ = '\0';
                    error_ = 1;
                    return *this;
                }
                start_dec = true;
                count++;
            }

        if (isdigit(strNum[count])) {
            number_.push_front(strNum[count]);
            count++;
            if (start_dec)
                decimals_++;
        }
        else {
            sign_ = '\0';
            number_.clear();
            decimals_ = 0;
            error_ = 1;
            return *this;
        }
    }
    if (count == 1 && (strNum[0] == '-' || strNum[0] == '+'))
    {
        error_ = 1;
    }

    this->lead_trim();
    return *this;
};

BigFloat& BigFloat::operator=(const std::string& strNum)
{
    *this = strNum.c_str();
    return *this;
};

BigFloat& BigFloat::operator=(int Num)
{
    *this = std::to_string(Num);
    return *this;
};

BigFloat& BigFloat::operator=(double Num)
{
    *this = std::to_string(Num);
    return *this;
};

BigFloat operator+(const BigFloat& left_, const BigFloat& right_)
{
    BigFloat tmp;
    if ((left_.error_) || (right_.error_)) {
        return tmp;
    }

    BigFloat left, right;
    left = left_;
    right = right_;

    if (left.decimals_ > right.decimals_)
        while (left.decimals_ > right.decimals_) {
            right.decimals_++;
            right.number_.push_front('0');
        }
    else if (left.decimals_ < right.decimals_)
        while (left.decimals_ < right.decimals_) {
            left.decimals_++;
            left.number_.push_front('0');
        }

    if ((left.sign_ == '+') && (right.sign_ == '-')) {
        int check = BigFloat::compare(left, right);
        if (check == 0) {
            tmp = 0;
            return tmp;
        }
        if (check == 1) {
            tmp = BigFloat::minus(left, right);
            tmp.sign_ = '+';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
        if (check == 2) {
            tmp = BigFloat::minus(right, left);
            tmp.sign_ = '-';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
    }
    if ((left.sign_ == '-') && (right.sign_ == '+')) {
        int check = BigFloat::compare(left, right);
        if (check == 0) {
            tmp = 0;
            return tmp;
        }
        if (check == 1) {
            tmp = BigFloat::minus(left, right);
            tmp.sign_ = '-';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
        if (check == 2) {
            tmp = BigFloat::minus(right, left);
            tmp.sign_ = '+';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
    }

    if ((left.sign_ == '+') && (right.sign_ == '+')) {
        tmp = BigFloat::sum(left, right);
        tmp.sign_ = '+';
        tmp.decimals_ = left.decimals_;
        tmp.error_ = 0;
        return tmp;
    }
    if ((left.sign_ == '-') && (right.sign_ == '-')) {
        tmp = BigFloat::sum(left, right);
        tmp.sign_ = '-';
        tmp.decimals_ = left.decimals_;
        tmp.error_ = 0;
        return tmp;
    }

    return tmp;  
};

BigFloat operator+(const BigFloat& left, const int& int_right)
{
    BigFloat right;
    right = int_right;
    return left + right;
};

BigFloat operator+(const BigFloat& left, const double& double_right)
{

    BigFloat right;
    right = double_right;
    return left + right;
};

BigFloat operator-(const BigFloat& left_, const BigFloat& right_)
{
    BigFloat tmp;
    if ((left_.error_) || (right_.error_)) {
        return tmp;
    }

    BigFloat left, right;
    left = left_;
    right = right_;

    if (left.decimals_ > right.decimals_)
        while (left.decimals_ > right.decimals_) {
            right.decimals_++;
            right.number_.push_front('0');
        }
    else if (left.decimals_ < right.decimals_)
        while (left.decimals_ < right.decimals_) {
            left.decimals_++;
            left.number_.push_front('0');
        }

    if ((left.sign_ == '+') && (right.sign_ == '-')) {
        tmp = BigFloat::sum(left, right);
        tmp.sign_ = '+';
        tmp.decimals_ = left.decimals_;
        tmp.error_ = 0;
        return tmp;
    }
    if ((left.sign_ == '-') && (right.sign_ == '+')) {
        tmp = BigFloat::sum(left, right);
        tmp.sign_ = '-';
        tmp.decimals_ = left.decimals_;
        tmp.error_ = 0;
        return tmp;
    }

    if ((left.sign_ == '+') && (right.sign_ == '+')) {
        int check = BigFloat::compare(left, right);
        if (check == 0) {
            tmp = 0;
            return tmp;
        }
        if (check == 1) {
            tmp = BigFloat::minus(left, right);
            tmp.sign_ = '+';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
        if (check == 2) {
            tmp = BigFloat::minus(right, left);
            tmp.sign_ = '-';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
    }

    if ((left.sign_ == '-') && (right.sign_ == '-')) {
        int check = BigFloat::compare(left, right);
        if (check == 0) {
            tmp = 0;
            return tmp;
        }
        if (check == 1) {
            tmp = BigFloat::minus(left, right);
            tmp.sign_ = '-';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
        if (check == 2) {
            tmp = BigFloat::minus(right, left);
            tmp.sign_ = '+';
            tmp.decimals_ = left.decimals_;
            tmp.lead_trim();
            tmp.error_ = 0;
            return tmp;
        }
    }

    return tmp; 
};

BigFloat operator-(const BigFloat& left, const int& int_right)
{
    BigFloat right;
    right = int_right;
    return left - right;
};

BigFloat operator-(const BigFloat& left, const double& double_right)
{
    BigFloat right;
    right = double_right;
    return left - right;
};

BigFloat operator*(const BigFloat& left, const BigFloat& right)
{
    BigFloat tmp;
    if ((left.error_) || (right.error_)) {
        return tmp;
    }

    tmp = BigFloat::multiply(left, right);
    if (((left.sign_ == '-') && (right.sign_ == '-')) || ((left.sign_ == '+') && (right.sign_ == '+')))
        tmp.sign_ = '+';
    else
        tmp.sign_ = '-';

    tmp.decimals_ = left.decimals_ + right.decimals_;
    tmp.error_ = 0;
    tmp.lead_trim();

    return tmp;
};

BigFloat operator*(const BigFloat& left, const int& int_right)
{
    BigFloat right;
    right = int_right;
    return left * right;
};

BigFloat operator*(const BigFloat& left, const double& double_right)
{
    BigFloat right;
    right = double_right;
    return left * right;
};

bool BigFloat::operator== (const BigFloat& right) const
{
    if ((error_) || (right.error_))
        return false;

    int check = compare(*this, right);
    if ((check == 0) && (sign_ == right.sign_))
        return true;
    return false;
};

bool BigFloat::operator== (const int& int_right) const
{
    BigFloat right(int_right);
    return *this == right;
};

bool BigFloat::operator== (const double& double_right) const
{
    BigFloat right(double_right);
    return *this == right;
};

bool BigFloat::operator!= (const BigFloat& right) const
{
    if ((error_) || (right.error_))
        return false;

    return !(*this == right);
};

bool BigFloat::operator!= (const int& int_right) const
{
    BigFloat right(int_right);
    return *this != right;
};

bool BigFloat::operator!= (const double& double_right) const
{
    BigFloat right(double_right);
    return *this != right;
};

BigFloat::operator double() const
{
    return this->to_double();
}

BigFloat::operator std::string() const
{
    return this->to_string();
}

double BigFloat::to_double() const
{
    double var = 0;

    if (error_) {
        var = std::nan("");
        return var;
    }

    double dec = 1;
    if (decimals_ > 0) {
        int aus = decimals_;
        while (aus != 0) {
            dec /= 10;
            aus--;
        }
    }

    for (size_t i = 0; i < number_.size(); i++) {
        var += CharToInt(number_[i]) * dec;
        dec *= 10;
    }

    if (sign_ == '-')
        var *= -1;

    return var;
};

std::string BigFloat::to_string() const
{
    std::string var;
    std::stringstream ss;

    if (error_) {
        var = "nan";
        return var;
    }
    if (sign_ == '-') {
        ss << sign_;
    }
    for (int i = number_.size() - 1; i >= 0; i--) {
        ss << number_[i];
        if ((i == decimals_) && (i != 0))
            ss << ".";
    }
    ss >> var;
    return var;
};

void BigFloat::set_precision(int prec)
{
    if ((error_) || (prec < 0))
        return;
    if (this->decimals_ < prec) {
        while (this->decimals_ < prec) {
            this->decimals_++;
            this->number_.push_front('0');
        }
    }
    else if (this->decimals_ > prec) {
        char last = '0';
        while (this->decimals_ > prec) {
            last = number_[0];
            this->decimals_--;
            this->number_.pop_front();
        }
        if (CharToInt(last) >= 5) {
            if (prec == 0)
            {
                (*this) = (*this) + 1;
            }
            else {
                std::stringstream ss;
                ss << "0.";
                for (int i = 1; i < prec; i++)
                    ss << "0";
                ss << "1";
                BigFloat aus(ss.str());
                *this = *this + aus;
            }
        }
    }
};

void BigFloat::lead_trim()
{
    for (int i = number_.size() - 1; i > decimals_; --i) {
        if (number_[i] == '0')
            number_.pop_back();
        else
            break;
    }
};

void BigFloat::trail_trim()
{
    if (!error_)
        while ((number_[0] == '0') && (decimals_ > 0)) {
            number_.pop_front();
            decimals_--;
        }
}


std::string BigFloat::exp() const
{
    std::stringstream out;
    if (error_) {
        out << "nan";
        return out.str();
    }

    int check = BigFloat::compare(*this, BigFloat(1));
    if (check == 0) {
        out << sign_;
        out << "1e+0";
    }
    if (check == 1) {
        out << sign_;
        int i = number_.size() - 1;
        out << number_[i];
        i--;
        if (i > 0) {
            out << '.';
            for (; (i >= (int)number_.size() - 6) && (i >= 0); --i) {
                out << number_[i];
            }
        }
        out << "e+" << ints() - 1;
    }
    if (check == 2) {
        int exp = 0;
        int count = number_.size() - 1;
        while (number_[count] == '0' && count > 0) {
            count--;
            exp++;
        }
        if (count == 0)
            if (number_[count] == '0')
                out << "+0";
            else
                out << sign_ << number_[count] << "e-" << exp;
        else {
            out << sign_ << number_[count] << '.';
            for (int i = count - 1; (i >= (int)count - 5) && (i >= 0); --i) {
                out << number_[i];
            }
            out << "e-" << exp;
        }
    }
    return out.str();
};