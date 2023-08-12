#pragma once

#include <iostream>
#include <string>

class Timestamp
{
    public:
        // 默认构造函数
        Timestamp();
        // 带参数的构造函数
        explicit Timestamp(int64_t microSecondsSinceRpoch_);
        //显示当前时间戳 并返回一个Timestamp对象
        static Timestamp now();
        //格式转化方法 将字符串转化成时间字符串
        std::string toString() const;

        int64_t microSecondsSinceEpoch() const { return microSecondsSinceRpoch_; }
        static const int kMicroSecondsPerSecond = 1000 * 1000;
        inline Timestamp addTime(Timestamp timestamp, double seconds)
        {
            int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
            return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
        }
    private:
        int64_t microSecondsSinceRpoch_;
};

