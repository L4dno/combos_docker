/**
 * I need to confess that this code was written by ChatGPT.
 * I think I'd like to codegen in the future but now I don't want to
 * spend much time on this part.
 *
 *
 * checked with
 ```cpp

    auto config = parameters::read_from_file("../parameters.yaml");
    parameters::write_to_file("test_how_look.yaml", config);
 ```
 */

#pragma once

#include <iostream>
#include <fstream>
#include <stdint.h>
#include <string>
#include <vector>
#include <cassert>
#include <optional>

#include "yaml-cpp/yaml.h"
#include <boost/algorithm/string.hpp>

namespace parameters
{

    const static std::string no_set_host_power = "host_power_null";
    const static std::string no_set_disk_capacity = "disk_capacity_null";

    inline double parse_trace_parameter(const std::string &config_param)
    {
        if (config_param != parameters::no_set_host_power &&
            config_param != parameters::no_set_disk_capacity)
            return atof(config_param.c_str());
        return -1;
    }

    struct SProject
    {
        int snumber;
        std::string name;
        int nscheduling_servers;
        int ndata_servers;
        int ndata_client_servers;
        std::string server_pw;
        int disk_bw;
        int ifgl_percentage;
        int ifcd_percentage;
        int averagewpif;
        int input_file_size;
        long long task_fpops;
        int output_file_size;
        int min_quorum;
        int target_nresults;
        int max_error_results;
        int max_total_results;
        int max_success_results;
        long long delay_bound;
        int output_file_storage;
        int dsreplication;
        int dcreplication;
    };

    struct GroupProject
    {
        int pnumber;
        int priority;
        std::string lsbw;
        std::string lslatency;
        std::string ldbw;
        std::string ldlatency;

        int success_percentage;
        int canonical_percentage;
    };

    struct Group
    {
        int n_clients;
        int ndata_clients;
        int connection_interval;
        int scheduling_interval;
        std::string gbw;
        std::string glatency;
        std::optional<std::string> traces_file;
        double max_speed;
        double min_speed;
        int pv_distri;
        double pa_param;
        double pb_param;

        std::optional<std::string> db_traces_file;
        int db_distri;
        double da_param;
        double db_param;
        int av_distri;
        double aa_param;
        double ab_param;
        int nv_distri;
        std::string na_param;
        std::string nb_param;
        int att_projs;
        std::vector<GroupProject> gprojects;
    };

    struct ClientSide
    {
        int n_groups;
        std::vector<Group> groups;
    };

    struct ServerSide
    {
        int n_projects;
        std::vector<SProject> sprojects;
    };

    struct Measures
    {
        bool clean_before_write;
        std::string save_filepath;
    };
    struct Timeline
    {
        std::string execute_state_log_path;
        std::vector<std::string> observable_clients;
    };

    struct ExperimentRun
    {
        std::optional<uint64_t> seed_for_deterministic_run;
        Timeline timeline;
        Measures measures;
    };

    struct Config
    {
        int simulation_time;
        int warm_up_time;
        ServerSide server_side;
        ClientSide client_side;
        ExperimentRun experiment_run;

        void set_with_sum_scheduling_servers(int &global_var) const
        {

            global_var = 0;
            for (auto &project : this->server_side.sprojects)
            {
                global_var += project.nscheduling_servers;
            }
        }
        void set_with_sum_data_servers(int &global_var) const
        {

            global_var = 0;
            for (auto &project : this->server_side.sprojects)
            {
                global_var += project.ndata_servers;
            }
        }
        void set_with_data_client_servers(int &global_var) const
        {

            global_var = 0;
            for (auto &project : this->server_side.sprojects)
            {
                global_var += project.ndata_client_servers;
            }
        }

        /**
int g_total_number_clients = 1000;          // Number of clients
int g_total_number_data_clients = 100;      // Number of data clients
int g_total_number_ordinary_clients = (g_total_number_clients - g_total_number_data_clients);
            */
    };

    inline std::string join(char delimeter, const std::vector<std::string> strs)
    {
        if (strs.empty())
            return "";
        std::string result = strs[0];
        for (int i = 1; i < strs.size(); ++i)
        {
            result += delimeter + strs[i];
        }
        return result;
    }

    inline std::vector<std::string> split(const std::string &str, std::string delimeter)
    {
        std::vector<std::string> tokens;

        boost::split(tokens, str, boost::is_any_of(delimeter));

        return tokens;
    }

    inline void
    write_to_file(const std::string &filename, const Config &config)
    {
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;

        emitter << YAML::Key << "simulation_time" << YAML::Value << config.simulation_time;
        emitter << YAML::Key << "warm_up_time" << YAML::Value << config.warm_up_time;

        {
            // all parameters that were set for an experiment run, not for a simulation in particular
            emitter << YAML::Key << "experiment_run" << YAML::Value;
            auto &parameters_for_experiment = config.experiment_run;
            emitter << YAML::BeginMap;

            {
                emitter << YAML::Key << "timeline";
                emitter << YAML::BeginMap;
                emitter << YAML::Key << "execute_state_log_path" << YAML::Value << parameters_for_experiment.timeline.execute_state_log_path;
                emitter << YAML::Key << "observable_clients" << YAML::Value << join(',', parameters_for_experiment.timeline.observable_clients);
                emitter << YAML::EndMap;
            }

            if (parameters_for_experiment.seed_for_deterministic_run.has_value())
            {
                emitter << YAML::Key << "seed_for_deterministic_run" << YAML::Value << *parameters_for_experiment.seed_for_deterministic_run;
            }

            {
                emitter << YAML::Key << "measures";
                emitter << YAML::BeginMap;
                emitter << YAML::Key << "clean_before_write" << YAML::Value << parameters_for_experiment.measures.clean_before_write;
                emitter << YAML::Key << "save_filepath" << YAML::Value << parameters_for_experiment.measures.save_filepath;
                emitter << YAML::EndMap;
            }
            emitter << YAML::EndMap;
        }

        emitter << YAML::Key << "server_side" << YAML::Value;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "n_projects" << YAML::Value << config.server_side.n_projects;
        emitter << YAML::Key << "sprojects" << YAML::Value << YAML::BeginSeq;

        for (const auto &sproject : config.server_side.sprojects)
        {
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "snumber" << YAML::Value << sproject.snumber;
            emitter << YAML::Key << "name" << YAML::Value << sproject.name;

            emitter << YAML::Key << "nscheduling_servers" << YAML::Value << sproject.nscheduling_servers;
            emitter << YAML::Key << "ndata_servers" << YAML::Value << sproject.ndata_servers;
            emitter << YAML::Key << "ndata_client_servers" << YAML::Value << sproject.ndata_client_servers;
            emitter << YAML::Key << "server_pw" << YAML::Value << sproject.server_pw;
            emitter << YAML::Key << "disk_bw" << YAML::Value << sproject.disk_bw;

            emitter << YAML::Key << "ifgl_percentage" << YAML::Value << sproject.ifgl_percentage;
            emitter << YAML::Key << "ifcd_percentage" << YAML::Value << sproject.ifcd_percentage;
            emitter << YAML::Key << "averagewpif" << YAML::Value << sproject.averagewpif;
            emitter << YAML::Key << "input_file_size" << YAML::Value << sproject.input_file_size;
            emitter << YAML::Key << "task_fpops" << YAML::Value << sproject.task_fpops;
            emitter << YAML::Key << "output_file_size" << YAML::Value << sproject.output_file_size;
            emitter << YAML::Key << "min_quorum" << YAML::Value << sproject.min_quorum;
            emitter << YAML::Key << "target_nresults" << YAML::Value << sproject.target_nresults;
            emitter << YAML::Key << "max_error_results" << YAML::Value << sproject.max_error_results;
            emitter << YAML::Key << "max_total_results" << YAML::Value << sproject.max_total_results;
            emitter << YAML::Key << "max_success_results" << YAML::Value << sproject.max_success_results;
            emitter << YAML::Key << "delay_bound" << YAML::Value << sproject.delay_bound;
            emitter << YAML::Key << "output_file_storage" << YAML::Value << sproject.output_file_storage;
            emitter << YAML::Key << "dsreplication" << YAML::Value << sproject.dsreplication;
            emitter << YAML::Key << "dcreplication" << YAML::Value << sproject.dcreplication;

            emitter << YAML::EndMap;
        }

        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;

        emitter << YAML::Key << "client_side" << YAML::Value;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "n_groups" << YAML::Value << config.client_side.n_groups;
        emitter << YAML::Key << "groups" << YAML::Value << YAML::BeginSeq;

        for (const auto &group : config.client_side.groups)
        {
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "n_clients" << YAML::Value << group.n_clients;
            emitter << YAML::Key << "ndata_clients" << YAML::Value << group.ndata_clients;

            emitter << YAML::Key << "connection_interval" << YAML::Value << group.connection_interval;
            emitter << YAML::Key << "scheduling_interval" << YAML::Value << group.scheduling_interval;
            emitter << YAML::Key << "gbw" << YAML::Value << group.gbw;
            emitter << YAML::Key << "glatency" << YAML::Value << group.glatency;
            if (group.traces_file.has_value())
            {
                emitter << YAML::Key << "traces_file" << YAML::Value << *group.traces_file;
            }
            emitter << YAML::Key << "max_speed" << YAML::Value << group.max_speed;
            emitter << YAML::Key << "min_speed" << YAML::Value << group.min_speed;
            emitter << YAML::Key << "pv_distri" << YAML::Value << group.pv_distri;
            emitter << YAML::Key << "pa_param" << YAML::Value << group.pa_param;
            emitter << YAML::Key << "pb_param" << YAML::Value << group.pb_param;
            if (group.db_traces_file.has_value())
            {
                emitter << YAML::Key << "db_traces_file" << YAML::Value << *group.db_traces_file;
            }
            emitter << YAML::Key << "db_distri" << YAML::Value << group.db_distri;
            emitter << YAML::Key << "da_param" << YAML::Value << group.da_param;
            emitter << YAML::Key << "db_param" << YAML::Value << group.db_param;
            emitter << YAML::Key << "av_distri" << YAML::Value << group.av_distri;
            emitter << YAML::Key << "aa_param" << YAML::Value << group.aa_param;
            emitter << YAML::Key << "ab_param" << YAML::Value << group.ab_param;
            emitter << YAML::Key << "nv_distri" << YAML::Value << group.nv_distri;
            emitter << YAML::Key << "na_param" << YAML::Value << group.na_param;
            emitter << YAML::Key << "nb_param" << YAML::Value << group.nb_param;
            emitter << YAML::Key << "att_projs" << YAML::Value << group.att_projs;
            emitter << YAML::Key << "gprojects" << YAML::Value << YAML::BeginSeq;

            for (const auto &groupProject : group.gprojects)
            {
                emitter << YAML::BeginMap;
                emitter << YAML::Key << "pnumber" << YAML::Value << groupProject.pnumber;
                emitter << YAML::Key << "priority" << YAML::Value << groupProject.priority;
                emitter << YAML::Key << "lsbw" << YAML::Value << groupProject.lsbw;
                emitter << YAML::Key << "lslatency" << YAML::Value << groupProject.lslatency;
                emitter << YAML::Key << "ldbw" << YAML::Value << groupProject.ldbw;
                emitter << YAML::Key << "ldlatency" << YAML::Value << groupProject.ldlatency;

                emitter << YAML::Key << "success_percentage" << YAML::Value << groupProject.success_percentage;
                emitter << YAML::Key << "canonical_percentage" << YAML::Value << groupProject.canonical_percentage;

                emitter << YAML::EndMap;
            }

            emitter << YAML::EndSeq;

            emitter << YAML::EndMap;
        }

        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;

        emitter << YAML::EndMap;

        std::ofstream file(filename);
        file << emitter.c_str();
    }

    inline Config read_from_file(const std::string &input_file)
    {
        Config config;

        try
        {
            YAML::Node root = YAML::LoadFile(input_file);

            // Deserialize common fields
            config.simulation_time = root["simulation_time"].as<int>();
            config.warm_up_time = root["warm_up_time"].as<int>();

            if (root["experiment_run"])
            {
                // read all parameters for experiment
                auto &parameters_for_experiment = config.experiment_run;
                const YAML::Node &parameters = root["experiment_run"];

                if (parameters["timeline"])
                {
                    const YAML::Node &timeline = parameters["timeline"];
                    parameters_for_experiment.timeline.execute_state_log_path = timeline["execute_state_log_path"].as<std::string>();
                    // format is "c{cluster_ind}{client_ind},c{cluster_ind}{client_ind}". Cluster's index starts with 1, when client's - with 0.
                    parameters_for_experiment.timeline.observable_clients = split(timeline["observable_clients"].as<std::string>(), ",");
                }

                if (parameters["seed_for_deterministic_run"])
                {
                    parameters_for_experiment.seed_for_deterministic_run = parameters["seed_for_deterministic_run"].as<uint64_t>();
                }

                if (parameters["measures"])
                {
                    const YAML::Node &measures = parameters["measures"];
                    parameters_for_experiment.measures.clean_before_write = measures["clean_before_write"].as<bool>();
                    parameters_for_experiment.measures.save_filepath = measures["save_filepath"].as<std::string>();

                    std::cout << "config.measures.clean_before_write: " << parameters_for_experiment.measures.clean_before_write << std::endl;
                    std::cout << "config.measures.save_filepath: " << parameters_for_experiment.measures.save_filepath << std::endl;
                }
            }

            // Deserialize server_side
            const YAML::Node &serverSideNode = root["server_side"];
            config.server_side.n_projects = serverSideNode["n_projects"].as<int>();
            const YAML::Node &sprojectsNode = serverSideNode["sprojects"];
            for (const auto &sprojectNode : sprojectsNode)
            {
                SProject sproject;
                sproject.snumber = sprojectNode["snumber"].as<int>();
                sproject.name = sprojectNode["name"].as<std::string>();
                sproject.nscheduling_servers = sprojectNode["nscheduling_servers"].as<int>();
                sproject.ndata_servers = sprojectNode["ndata_servers"].as<int>();
                sproject.ndata_client_servers = sprojectNode["ndata_client_servers"].as<int>();
                sproject.server_pw = sprojectNode["server_pw"].as<std::string>();
                sproject.disk_bw = sprojectNode["disk_bw"].as<int>();
                sproject.ifgl_percentage = sprojectNode["ifgl_percentage"].as<int>();
                sproject.ifcd_percentage = sprojectNode["ifcd_percentage"].as<int>();
                sproject.averagewpif = sprojectNode["averagewpif"].as<int>();
                sproject.input_file_size = sprojectNode["input_file_size"].as<int>();
                sproject.task_fpops = sprojectNode["task_fpops"].as<long long>();
                sproject.output_file_size = sprojectNode["output_file_size"].as<int>();
                sproject.min_quorum = sprojectNode["min_quorum"].as<int>();
                sproject.target_nresults = sprojectNode["target_nresults"].as<int>();
                sproject.max_error_results = sprojectNode["max_error_results"].as<int>();
                sproject.max_total_results = sprojectNode["max_total_results"].as<int>();
                sproject.max_success_results = sprojectNode["max_success_results"].as<int>();
                sproject.delay_bound = sprojectNode["delay_bound"].as<long long>();
                sproject.output_file_storage = sprojectNode["output_file_storage"].as<int>();
                sproject.dsreplication = sprojectNode["dsreplication"].as<int>();
                sproject.dcreplication = sprojectNode["dcreplication"].as<int>();

                config.server_side.sprojects.push_back(sproject);
            }

            // Deserialize client_side
            const YAML::Node &clientSideNode = root["client_side"];
            config.client_side.n_groups = clientSideNode["n_groups"].as<int>();

            // assert((config.client_side.n_groups == 1) && "code doesn't support multiple groups :(");

            const YAML::Node &groupsNode = clientSideNode["groups"];

            for (const auto &groupNode : groupsNode)
            {
                Group group;
                group.n_clients = groupNode["n_clients"].as<int>();
                group.ndata_clients = groupNode["ndata_clients"].as<int>();
                group.connection_interval = groupNode["connection_interval"].as<int>();
                group.scheduling_interval = groupNode["scheduling_interval"].as<int>();
                group.gbw = groupNode["gbw"].as<std::string>();
                group.glatency = groupNode["glatency"].as<std::string>();
                if (groupNode["traces_file"])
                {
                    group.traces_file = groupNode["traces_file"].as<std::string>();
                }
                group.max_speed = groupNode["max_speed"].as<double>();
                group.min_speed = groupNode["min_speed"].as<double>();
                group.pv_distri = groupNode["pv_distri"].as<int>();
                group.pa_param = groupNode["pa_param"].as<double>();
                group.pb_param = groupNode["pb_param"].as<double>();
                if (groupNode["db_traces_file"])
                {
                    group.db_traces_file = groupNode["db_traces_file"].as<std::string>();
                }
                group.db_distri = groupNode["db_distri"].as<int>();
                group.da_param = groupNode["da_param"].as<double>();
                group.db_param = groupNode["db_param"].as<double>();
                group.av_distri = groupNode["av_distri"].as<int>();
                group.aa_param = groupNode["aa_param"].as<double>();
                group.ab_param = groupNode["ab_param"].as<double>();
                group.nv_distri = groupNode["nv_distri"].as<int>();
                group.na_param = groupNode["na_param"].as<std::string>();
                group.nb_param = groupNode["nb_param"].as<std::string>();
                group.att_projs = groupNode["att_projs"].as<int>();

                const YAML::Node &gprojectsNode = groupNode["gprojects"];
                for (const auto &groupProjectNode : gprojectsNode)
                {
                    GroupProject groupProject;
                    groupProject.pnumber = groupProjectNode["pnumber"].as<int>();
                    groupProject.priority = groupProjectNode["priority"].as<int>();
                    groupProject.lsbw = groupProjectNode["lsbw"].as<std::string>();
                    groupProject.lslatency = groupProjectNode["lslatency"].as<std::string>();
                    groupProject.ldbw = groupProjectNode["ldbw"].as<std::string>();
                    groupProject.ldlatency = groupProjectNode["ldlatency"].as<std::string>();

                    groupProject.success_percentage = groupProjectNode["success_percentage"].as<int>();
                    groupProject.canonical_percentage = groupProjectNode["canonical_percentage"].as<int>();

                    group.gprojects.push_back(groupProject);
                }

                config.client_side.groups.push_back(group);
            }
        }
        catch (const YAML::Exception &e)
        {
            std::cout << "Error while parsing YAML: " << e.what() << std::endl;
        }

        return config;
    }

} // namespace parameters
