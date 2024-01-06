#pragma once
#include <queue>
#include <simgrid/cond.h>
// #include <simgrid/engine.h>
#include <simgrid/s4u.hpp>

namespace sg4 = simgrid::s4u;

template <class T>
class TSQueue
{
public:
    TSQueue() : data{}, mutex(sg4::Mutex::create()), cv_is_empty(sg4::ConditionVariable::create()) {}

    void push(T &&el)
    {
        std::unique_lock lock(*mutex);

        data.push(std::move(el));
        cv_is_empty->notify_all();
    }

    void push(const T &el)
    {
        std::unique_lock lock(*mutex);

        data.push(el);
        cv_is_empty->notify_all();
    }

    T pop()
    {
        std::unique_lock lock(*mutex);
        while (data.empty())
        {
            cv_is_empty->wait(lock);
        }
        auto el = std::move(data.front());
        data.pop();
        return el;
    }

    size_t empty()
    {
        std::unique_lock lock(*mutex);

        return data.size();
    }

private:
    std::queue<T> data;
    sg4::MutexPtr mutex;
    sg4::ConditionVariablePtr cv_is_empty;
};