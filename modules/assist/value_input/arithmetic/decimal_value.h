#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_ARITHMETIC_DECIMAL_VALUE_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_ARITHMETIC_DECIMAL_VALUE_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	bigfloat.h
//	创建者		：	
//	创建时间	：	2024-08-18
//	功能描述	：	v模式拼音解析-大浮点数计算，对标微软输入法
//
// -------------------------------------------------------------------------- //

#include <deque>
#include <string>

class BigFloat {
public:
    BigFloat();
    explicit BigFloat(const char* strNum);
    explicit BigFloat(const std::string& strNum);
    explicit BigFloat(int Num);
    explicit BigFloat(double Num);

    BigFloat& operator=(const char* strNum);
    BigFloat& operator=(const std::string& strNum);
    BigFloat& operator=(int Num);
    BigFloat& operator=(double Num);
    friend BigFloat operator+(const BigFloat& left_, const BigFloat& right_);
    friend BigFloat operator-(const BigFloat& left_, const BigFloat& right_);
    friend BigFloat operator*(const BigFloat& left, const BigFloat& right);
    bool operator== (const BigFloat& right) const;
    bool operator== (const int& int_right) const;
    bool operator== (const double& double_right) const;
    bool operator!= (const BigFloat& right) const;
    bool operator!= (const int& int_right) const;
    bool operator!= (const double& double_right) const;

    operator double() const;
    operator std::string() const;
    double to_double() const;
    std::string to_string() const;

    void set_precision(int prec); 
    void lead_trim();
    void trail_trim(); 
    inline bool has_error() const
	{
		return error_;
	}
    inline int decimals() const
	{
		return decimals_;
	}

    inline int ints() const
	{
		return number_.size() - decimals_;
	}
    std::string exp() const;
private:
	char sign_;
	std::deque<char> number_;
	int decimals_;
	bool error_;
private:
    inline static int CharToInt(const char& val);
    inline static char IntToChar(const int& val);
	static int compare(const BigFloat& left, const BigFloat& right);
	static BigFloat sum(const BigFloat& left, const BigFloat& right);
	static BigFloat minus(const BigFloat& left, const BigFloat& right);
	static BigFloat multiply(const BigFloat& left, const BigFloat& right);
};

#endif // KBIGFLOAT_H
