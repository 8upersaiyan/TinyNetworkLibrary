#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>
//防止一个线程创建多个EventLoop   __thread：thread_local线程局部存储关键字用于声明一个线程局部变量
//当一个eventloop创建起来它就指向那个对象，在一个线程里再去创建一个对象，由于这个指针不为空，就不创建 
__thread EventLoop *t_loopInThisThread = nullptr;

//定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;//10秒钟 

//创建wakeupfd，用来notify唤醒subReactor处理新来的channel
//EFD_NONBLOCK  eventfd 将被设置为非阻塞模式。在非阻塞模式下，如果一个读写操作不能立即完成，线程状态不变立即返回一个返回值
//EFD_CLOEXEC eventfd将在“exec（）”函数调用后自动关闭
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()//构造函数 
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , timerQueue_(new TimerQueue(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))//this:需要知道Channnel所在的loop
    , currentActtiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)//这个线程已经有loop了，就不创建了 
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else//这个线程还没有loop，创建 
    {
        t_loopInThisThread = this;
    }

    //设置wakeupfd的事件类型以及发生事件后的回调操作,wakeupfd就是为了唤醒subloop的！
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    //每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()//析构函数 
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_); 
    t_loopInThisThread = nullptr;
}

//开启事件循环 驱动底层的poller执行poll  epoll_wait函数
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        //先清空activeChannels_容器
        activeChannels_.clear();
        //监听两类fd   一种是client的fd，一种wakeupfd
        //通过poller的poll方法底层调用epoll_wait把活跃Channel都放到activeChannels_容器中
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        //遍历 调用Channel中的handleEvent去执行具体事件类型的操作
        for (Channel *channel : activeChannels_)
        {
            //Poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);//事先已经绑定好 
        }

        //执行当前EventLoop事件循环需要处理的回调操作
        /** mainLoop只做accept新用户的连接
         * IO线程 mainLoop：accept  channel打包fd ---》 subloop 1个mainloop 3个subloop 
         * mainLoop 事先注册一个回调cb（需要subloop来执行）  mainloop wakeup subloop后，
		 执行下面的方法，执行之前mainloop注册的cb操作
        * IO thread: mainLoop accept fd 打包成channel分发给subLoop
        * mainLoop实现注册一个回调，交给subLoop来执行wakeup subLoop之后 让其执行注册的回调操作
        * 这些回调函数在
        * 
         */ 
        doPendingFunctors();//mainloop注册回调给subloop。 
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

//退出事件循环  1.loop在自己的线程中调用quit  2.在非loop的线程中，调用loop的quit
/**
 *              mainLoop
 * 
 *              通过wakeupfd                     no ==================== 生产者-消费者的线程安全的队列
                                                mainloop生产 subloop消费  逻辑好处理 但是muduo库没有这个 是通过wakefd通信 
                                                线程间直接notify唤醒 
 * 
 *  subLoop1     subLoop2     subLoop3    
 */ 
void EventLoop::quit()
{
    quit_ = true;

    //如果是在其它线程中，调用的quit 比如说：在一个subloop(worker)中，调用了mainLoop(IO)的quit
    //调用mainloop的quit，这时候，应该给它唤醒 ，它就从poll返回回来，
	//当前线程在work线程  轮询的算法 派发channel 
    if (!isInLoopThread())  
    {
        wakeup();//因为不知道主线程是什么情况，需要唤醒一下 
    }
}

//直接在当前loop中执行cb（callback） 
void EventLoop::runInLoop(Functor cb)
{
    // 每个EventLoop都保存创建自己的线程tid
    // 我们可以通过CurrentThread::tid()获取当前执行线程的tid然后和EventLoop保存的进行比较
    if (isInLoopThread())//在当前的loop线程中，执行cb
    {
        cb();
    }
    else //在非当前loop线程中执行cb , 就需要唤醒loop所在线程，执行cb
    {
        queueInLoop(cb);
    }
}

//不在当前的loop中执行，把cb放入队列中，唤醒loop所在的线程，执行cb
//一个loop运行在自己的线程里。比如在subloop2调用subloop3的 runInLoop
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);//因为有并发的访问 
        pendingFunctors_.emplace_back(cb);//直接构造cb放到vector里面 
    }

    //唤醒相应的，需要执行上面回调操作的loop的线程了
    // || callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调
    if (!isInLoopThread() || callingPendingFunctors_) 
    {
        wakeup();//唤醒loop所在线程，继续执行回调 
    }
}

//subLoop用的
void EventLoop::handleRead()//就是读，写啥读啥无所谓，就是为了唤醒loop线程执行回调 
{
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}

//mainLoop用的
//用来唤醒loop所在的线程的  向wakeupfd_写一个数据，wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)//子线程无法被唤醒 
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

//EventLoop的方法 =》 Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors()//执行回调 在loop中调用的方法 
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);//资源交换，把pendingFunctors_ 置为空
		//不需要pendingFunctors_了  不妨碍 mainloop向 pendingFunctors_写回调操作cb 
    }
    for (const Functor &functor : functors)
    {
        functor();//执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}