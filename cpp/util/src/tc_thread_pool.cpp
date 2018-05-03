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

#include "util/tc_thread_pool.h"
#include "util/tc_common.h"

#include <iostream>

namespace tars
{

TC_ThreadPool::ThreadWorker::ThreadWorker(TC_ThreadPool *tpool)
: _tpool(tpool)
, _bTerminate(false)
{
}

void TC_ThreadPool::ThreadWorker::terminate()
{
    _bTerminate = true;
	//TC_FunctorWrapperInterface *TC_ThreadPool::get(ThreadWorker *ptw)
	//函数中调用了_jobqueue.timedWait函数，会阻塞在_jobqueue的条件变量上，这里调用notifyT
	//让_jobqueue通知所有等待线程苏醒，好退出循环
    _tpool->notifyT();
}

void TC_ThreadPool::ThreadWorker::run()
{	
    //调用初始化部分，不带参数的get返回的是_startqueue中的元素
	//注意_startqueue中的元素都是同一个FunctorWrapper，亦即所有线程
	//执行的都是同一个函数，用于初始化。如果线程池以void start()启动，
	//表示线程无需初始化
    TC_FunctorWrapperInterface *pst = _tpool->get();
    if(pst)
    {
        try
        {
            (*pst)();
        }
        catch ( ... )
        {
        }
        delete pst;
        pst = NULL;
    }

    //调用处理部分，带参数的get返回的是_jobqueue中的元素，每一个元素都是
	//一个【不同的】FunctorWrapper，表示具体执行的任务
	//run是ThreadWorker的成员函数，所以这里_bTerminate也是每个ThreadWorker的成员变量
	//这样就可以通过设置不同ThreadWorker对象的_bTerminate来实现对ThreadWorker启动的
	//线程的控制
    while (!_bTerminate)
    {	//get这里会调用timeWait，线程会阻塞在_jobqueue的条件变量上
        TC_FunctorWrapperInterface *pfw = _tpool->get(this);
        if(pfw != NULL)
        {
            auto_ptr<TC_FunctorWrapperInterface> apfw(pfw);

            try
            {
                (*pfw)();
            }
            catch ( ... )
            {
            }

            _tpool->idle(this);
        }
    }

    //结束【跳出循环后走到这里】
    _tpool->exit();
}

//////////////////////////////////////////////////////////////
//
//

TC_ThreadPool::KeyInitialize TC_ThreadPool::g_key_initialize;
pthread_key_t TC_ThreadPool::g_key;

void TC_ThreadPool::destructor(void *p)
{
    ThreadData *ttd = (ThreadData*)p;
    if(ttd)
    {
        delete ttd;
    }
}

void TC_ThreadPool::exit()
{
	//清除线程特有数据
    TC_ThreadPool::ThreadData *p = getThreadData();
    if(p)
    {
        delete p;
        int ret = pthread_setspecific(g_key, NULL);
        if(ret != 0)
        {
            throw TC_ThreadPool_Exception("[TC_ThreadPool::setThreadData] pthread_setspecific error", ret);
        }
    }

    _jobqueue.clear();
}

void TC_ThreadPool::setThreadData(TC_ThreadPool::ThreadData *p)
{
    TC_ThreadPool::ThreadData *pOld = getThreadData();
    if(pOld != NULL && pOld != p)
    {
        delete pOld;
    }

    int ret = pthread_setspecific(g_key, (void *)p);
    if(ret != 0)
    {
        throw TC_ThreadPool_Exception("[TC_ThreadPool::setThreadData] pthread_setspecific error", ret);
    }
}

TC_ThreadPool::ThreadData* TC_ThreadPool::getThreadData()
{
    return (ThreadData *)pthread_getspecific(g_key);
}

void TC_ThreadPool::setThreadData(pthread_key_t pkey, ThreadData *p)
{
    TC_ThreadPool::ThreadData *pOld = getThreadData(pkey);
    if(pOld != NULL && pOld != p)
    {
        delete pOld;
    }

    int ret = pthread_setspecific(pkey, (void *)p);
    if(ret != 0)
    {
        throw TC_ThreadPool_Exception("[TC_ThreadPool::setThreadData] pthread_setspecific error", ret);
    }
}

TC_ThreadPool::ThreadData* TC_ThreadPool::getThreadData(pthread_key_t pkey)
{
    return (ThreadData *)pthread_getspecific(pkey);
}

TC_ThreadPool::TC_ThreadPool()
: _bAllDone(true)
{
}

TC_ThreadPool::~TC_ThreadPool()
{
    stop();
    clear();
}

void TC_ThreadPool::clear()
{
    std::vector<ThreadWorker*>::iterator it = _jobthread.begin();
    while(it != _jobthread.end())
    {
        delete (*it);
        ++it;
    }

    _jobthread.clear();
    _busthread.clear();
}

void TC_ThreadPool::init(size_t num)
{
    stop();

    Lock sync(*this);

    clear();

    for(size_t i = 0; i < num; i++)
    {
        _jobthread.push_back(new ThreadWorker(this));
    }
}

void TC_ThreadPool::stop()
{
    Lock sync(*this);

    std::vector<ThreadWorker*>::iterator it = _jobthread.begin();
    while(it != _jobthread.end())
    {
        if ((*it)->isAlive())
        {
            (*it)->terminate();
            (*it)->getThreadControl().join();
        }
        ++it;
    }
    _bAllDone = true;
}

void TC_ThreadPool::start()
{
    Lock sync(*this);

    std::vector<ThreadWorker*>::iterator it = _jobthread.begin();
    while(it != _jobthread.end())
    {
        (*it)->start();
        ++it;
    }
    _bAllDone = false;
}

bool TC_ThreadPool::finish()
{
    return _startqueue.empty() && _jobqueue.empty() && _busthread.empty() && _bAllDone;
}

bool TC_ThreadPool::waitForAllDone(int millsecond)
{
    Lock sync(_tmutex);

start1:
    //任务队列和繁忙线程都是空的
    if (finish())
    {
        return true;
    }

    //永远等待直到任务全部完成
    if(millsecond < 0)
    {
        _tmutex.timedWait(1000);
        goto start1;
    }

    int64_t iNow= TC_Common::now2ms();
    int m       = millsecond;
start2:
	//等待_tmutex调用notifyT（在idle函数中，只有当_busthread为空时才会通知）
    bool b = _tmutex.timedWait(millsecond);
	//唤醒后先判断是否fininsh（注意条件变量的使用规范：唤醒后先判断唤醒条件是否还成立）
    //完成处理了
    if(finish())
    {
        return true;
    }
	//超时
    if(!b)
    {
        return false;
    }
	//如果唤醒后发现又有没完成的任务，更新剩余等待时间后继续等待
    millsecond = max((int64_t)0, m  - (TC_Common::now2ms() - iNow));
    goto start2;
	
    return false;
}

TC_FunctorWrapperInterface *TC_ThreadPool::get(ThreadWorker *ptw)
{
    TC_FunctorWrapperInterface *pFunctorWrapper = NULL;
	//没有待执行任务，返回NULL
    if(!_jobqueue.pop_front(pFunctorWrapper, 1000))
    {
        return NULL;
    }

    {
        Lock sync(_tmutex);
        _busthread.insert(ptw);
    }

    return pFunctorWrapper;
}

TC_FunctorWrapperInterface *TC_ThreadPool::get()
{
    TC_FunctorWrapperInterface *pFunctorWrapper = NULL;
    if(!_startqueue.pop_front(pFunctorWrapper))
    {
        return NULL;
    }

    return pFunctorWrapper;
}

void TC_ThreadPool::idle(ThreadWorker *ptw)
{
    Lock sync(_tmutex);
    _busthread.erase(ptw);

    //无繁忙线程, 通知等待在线程池结束的线程醒过来
    if(_busthread.empty())
    {
        _bAllDone = true;
        _tmutex.notifyAll();
    }
}

void TC_ThreadPool::notifyT()
{
    _jobqueue.notifyT();
}



}
