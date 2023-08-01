#include "Timestamp.h"
#include <time.h>

Timestamp::Timestamp()
    :microSecondsSinceRpoch_(0)
    {}

// 带参数的构造函数
Timestamp::Timestamp(int64_t microSecondsSinceRpoch)
    : microSecondsSinceRpoch_(microSecondsSinceRpoch)
    {}

//显示当前时间 
Timestamp Timestamp::now()
{
    time_t timenow = time(NULL);
    return Timestamp(timenow);
}

//格式转化方法 将字符串转化成时间字符串
std::string Timestamp::toString() const
{
    char buf[128] = {0};
    tm * tm_time = localtime(&microSecondsSinceRpoch_);
    snprintf(buf,128,"%4d/%02d/%02d %02d:%02d:%02d",
    tm_time->tm_year + 1900,
    tm_time->tm_mon + 1,
    tm_time->tm_mday,
    tm_time->tm_hour,
    tm_time->tm_min,
    tm_time->tm_sec
    );
    return buf;
}

// #include <iostream>
// int main()
// {
//     std::cout<<Timestamp::now().toString()<<std::endl;
//     return 0;
// }