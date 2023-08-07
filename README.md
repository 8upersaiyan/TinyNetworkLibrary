﻿# CKmuduo
本项目是参考 muduo 实现的基于 Reactor 模型的多线程网络库。使用 C++ 11 编写去除 muduo 对 boost 的依赖，内部实现了一个小型的 HTTP 服务器。
# 开发环境
* 操作系统：`Ubuntu 18.04.6 LTS`
* 编译器：`VsCode`
* 编译环境：`g++ (GCC) 10.2.1`
* 版本控制：`git`
* 项目构建：`cmake version 3.20.2`
# 项目描述
* 学习陈硕老师的 C++ muduo 网络库优秀的代码设计及编程方法。
* 去掉了Muduo库中的Boost依赖，完全使用C++标准，如使用std::function<>。
* 底层使用 Epoll + LT 模式的 I/O 复用模型，并且结合非阻塞 I/O 实现主从 Reactor 模型。
* 采用「one loop per thread」线程模型，并向上封装线程池避免线程创建和销毁带来的性能开销。
* 实现 Channel、Poller、EventLoop、TcpServer、Buffer、TcpConnection 等重要部分。
* 示例中实现了http服务器与echo服务器，使用了Apache Benchmark做了压测。
# 具体实现细节博客
https://blog.csdn.net/super8ayan/category_12359543.html?spm=1001.2014.3001.5482
# 使用方法
执行`autobuild.sh`脚本执行`CMakeList`生成`libmymuduo.so`库
并将库文件拷贝到 /usr/lib路径下

    #!/bin/bash
    set -e
    # 如果没有build目录，创建该目录
    if [ ! -d `pwd`/build ]; then
        mkdir `pwd`/build
    fi
    rm -rf `pwd`/build/*
    cd `pwd`/build &&
        cmake .. &&
        make
    # 回到项目根目录
    cd ..
    # 把头文件拷贝到 /usr/include/mymuduo  so库拷贝到 /usr/lib    PATH
    if [ ! -d /usr/include/mymuduo ]; then 
        mkdir /usr/include/mymuduo
    fi
    # 拷贝头文件 
    for header in $(find src -type f -name "*.h"); 
    do
        cp $header /usr/include/mymuduo
    done
    #拷贝库文件
    cp `pwd`/lib/libmymuduo.so /usr/lib
    ldconfig
# 使用运行案例
## EchoServer 回响服务器
这里以一个简单的回声服务器作为案例，EchoServer默认监听端口为8080。

    cd ./example
    ./EchoServer

执行情况：
![image](https://github.com/8upersaiyan/CKmuduo/assets/102213169/4fa91142-34a6-4ad7-a546-3eebb984db75)




