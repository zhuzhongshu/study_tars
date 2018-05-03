/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#include "util/tc_thread.h"
#include <cerrno>

namespace tars
{

TC_ThreadControl::TC_ThreadControl(pthread_t thread) : _thread(thread)
{
}

TC_ThreadControl::TC_ThreadControl() : _thread(pthread_self())
{
}

void TC_ThreadControl::join()
{
    if(pthread_self() == _thread)
    {
        throw TC_ThreadThreadControl_Exception("[TC_ThreadControl::join] can't be called in the same thread");
    }

    void* ignore = 0;
    int rc = pthread_join(_thread, &ignore);
    if(rc != 0)
    {
        throw TC_ThreadThreadControl_Exception("[TC_ThreadControl::join] pthread_join error ", rc);
    }
}

void TC_ThreadControl::detach()
{
    if(pthread_self() == _thread)
    {
        throw TC_ThreadThreadControl_Exception("[TC_ThreadControl::join] can't be called in the same thread");
    }

    int rc = pthread_detach(_thread);
    if(rc != 0)
    {
        throw TC_ThreadThreadControl_Exception("[TC_ThreadControl::join] pthread_join error", rc);
    }
}

pthread_t TC_ThreadControl::id() const
{
    return _thread;
}

void TC_ThreadControl::sleep(long millsecond)
{
    struct timespec ts;
    ts.tv_sec = millsecond / 1000;
    ts.tv_nsec = (millsecond % 1000)*1000000;
	/*
	unix、linux系统尽量不要使用usleep和sleep而应该使用nanosleep，
	使用nanosleep应注意判断返回值和错误代码，否则容易造成cpu占用率100%
	*/

    nanosleep(&ts, 0);
}

void TC_ThreadControl::yield()
{
	/*
	sched_yield()这个函数可以使用另一个级别等于或高于当前线程的线程先运行。
	如果没有符合条件的线程，那么这个函数将会立刻返回然后继续执行当前线程的程序。
	在成功完成之后返回零，否则返回-1.
	*/
    sched_yield();
}

TC_Thread::TC_Thread() : _running(false),_tid(-1)
{
}

void TC_Thread::threadEntry(TC_Thread *pThread)
{
    pThread->_running = true;

    {
        TC_ThreadLock::Lock sync(pThread->_lock);
        pThread->_lock.notifyAll();
    }

    try
    {
        pThread->run();//这个是具体执行任务的函数，
    }
    catch(...)
    {
        pThread->_running = false;
        throw;
    }
    pThread->_running = false;
}

TC_ThreadControl TC_Thread::start()
{
    TC_ThreadLock::Lock sync(_lock);

    if(_running)
    {
        throw TC_ThreadThreadControl_Exception("[TC_Thread::start] thread has start");
    }
	//这里才开始新建线程
    int ret = pthread_create(&_tid,
                   0,
                   (void *(*)(void *))&threadEntry,
                   (void *)this);//把this指针作为threadEntry的参数传入

    if(ret != 0)
    {
        throw TC_ThreadThreadControl_Exception("[TC_Thread::start] thread start error", ret);
    }

    _lock.wait();
	//返回控制该线程的类
    return TC_ThreadControl(_tid);
}

TC_ThreadControl TC_Thread::getThreadControl() const
{	//这里返回的是新建的一个TC_ThreadControl类
    return TC_ThreadControl(_tid);
}

bool TC_Thread::isAlive() const
{
    return _running;
}

}

