#pragma once
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace execution_state
{

    enum class State
    {
        Busy,
        Idle,
        Unavailable
    };

    class Switcher
    {
    public:
        // set traces of what clients we will save
        static void set_observable_clients(const std::vector<std::string> &observable_clients_);
        /*
        For a client save finished period of idleness/unavailability/busyness when it switches to another.
        It is used in execute_task.cpp, where two potential threads can call this function: client_main_loop
        and client_execute_tasks. To order events, firstly client_main_loop have to suspend client_execute_tasks actor,
        switch_to necessary states and then resume the actor.
        */
        static void switch_to(const std::string &client, State execution_state, const std::string &arg = "");
        // final step to save all traces in the file
        static void save_to_file(std::string path);

    private:
        struct TimeSegment
        {
            std::string type;
            double start, end;
        };

        static std::set<std::string> observable_clients;
        static std::unordered_map<std::string, std::vector<Switcher::TimeSegment>> events_periods;
    };
} // namespace execution_state
