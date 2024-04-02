#include "execution_state.hpp"
#include <fstream>
#include <simgrid/s4u.hpp>

namespace execution_state
{

    namespace sg4 = simgrid::s4u;

    std::unordered_map<std::string, std::vector<Switcher::TimeSegment>> Switcher::events_periods = {};
    std::set<std::string> Switcher::observable_clients = {};

    void Switcher::set_observable_clients(const std::vector<std::string> &observable_clients_)
    {
        observable_clients = std::set<std::string>(observable_clients_.begin(), observable_clients_.end());
        for (auto &observable : observable_clients)
        {
            events_periods[observable] = {};
        }
    }

    std::string get_string(const State &state)
    {
        switch (state)
        {
        case State::Busy:
            return "busy";
        case State::Idle:
            return "idle";
        case State::Unavailable:
            return "unavailable";
        }
        throw "Invalid state";
    }

    void Switcher::switch_to(const std::string &client, State execution_state, const std::string &arg)
    {
        if (observable_clients.find(client) == observable_clients.end())
        {
            return;
        }
        auto tmstmp /* timestamp */ = sg4::Engine::get_clock();
        auto &client_events_periods = events_periods.at(client);
        if (!client_events_periods.empty())
        {
            client_events_periods.back().end = tmstmp;
            if (client_events_periods.back().start == tmstmp)
            {
                // the previous event has length 0 and can be erased from logs
                client_events_periods.back().type = get_string(execution_state).append(":").append(arg);
                // client_events_periods.back().start == tmstmp
                // client_events_periods.back().end == tmstmp
                return;
            }
        }
        client_events_periods.push_back(
            TimeSegment{
                .type = get_string(execution_state).append(":").append(arg),
                .start = tmstmp,
                .end = tmstmp // not set yet
            });
    }

    void Switcher::save_to_file(std::string path)
    {
        std::ofstream in_csv(path);
        in_csv << "client,type,start,end\n";

        for (const auto &[client, client_events_periods] : events_periods)
        {
            for (const auto &period : client_events_periods)
            {
                in_csv << client << ',' << period.type << ',' << period.start << ',' << period.end << '\n';
            }
        }
        in_csv << std::endl;
        in_csv.close();
    }

}