#include "scheduler.hpp"

#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>

#include "types.hpp"
#include "shared.hpp"

/**
 * @brief
 * takes client's request which is
 * - reply: send to validator results of computantions that present in msg->content
 * - request: send computations to client: take [result] item from [project.current_results]
 *            and create [result->number_tasks] tasks for it, with [workunit] equals to
 *            [result->workunit->number]
 */

namespace sg4 = simgrid::s4u;

/*
 *	Blank result
 */
AssignedResult *blank_result()
{
    AssignedResult *result = new AssignedResult();
    result->workunit = NULL;  // Associated workunit
    result->ninput_files = 0; // Number of input files
    // result->input_files.clear(); // Input files names (URLs)
    result->number_tasks = 0; // Number of tasks (usually one)
    // result->tasks;            // Tasks
    return result;
}

/*
 *	Select result from database
 */
std::vector<AssignedResult *> select_result(int project_number, request_t req)
{

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number];

    std::vector<AssignedResult *> bag_of_result;
    double sum_work = 0;
    double single_result_add_work = (double)project.job_duration / req->power;
    std::set<std::string> unique_workunits;
    std::list<AssignedResult *>::iterator current_results_it = project.current_results.begin();

    while (sum_work + single_result_add_work < req->percentage || sum_work == 0)
    {

        // Get result
        AssignedResult *result = nullptr;
        for (; current_results_it != project.current_results.end(); ++current_results_it)
        {
            // several data clients can download this file. We check if client has one of
            // them in its group
            bool is_file_accessible_in_group = false;
            for (auto input_file_holder : (*current_results_it)->workunit->input_files)
            {
                if (the_same_client_group(input_file_holder, req->host_name))
                {
                    is_file_accessible_in_group = true;
                    break;
                }
            }
            if (!is_file_accessible_in_group)
                continue;
            if (unique_workunits.count((*current_results_it)->workunit->number) == 0)
            {
                break;
            }
        }
        if (current_results_it == project.current_results.end())
        {
            project.wg_full->notify_all();
            break;
        }
        result = *current_results_it;
        auto next_it = current_results_it;
        next_it++;
        project.current_results.erase(current_results_it);
        current_results_it = next_it;

        if (result == nullptr)
        {
            project.wg_full->notify_all();
            break;
        }

        // Signal work generator if number of current results is 0
        project.ncurrent_results--;
        if (project.ncurrent_results == 0)
        {
            project.wg_full->notify_all();
        }

        sum_work += single_result_add_work;

        // Create task
        TaskT *task = new TaskT();
        task->workunit = result->workunit->number;
        task->name = std::string(bprintf("%" PRId32, result->workunit->nsent_results++));
        task->duration = project.job_duration * ((double)req->group_power / req->power);
        task->deadline = project.delay_bound;
        task->sent_time = sg4::Engine::get_clock();
        result->corresponding_tasks = task;
        // set sent time instead of creation time here
        result->workunit->times[result->number] = task->sent_time;

        unique_workunits.insert(task->workunit);
        bag_of_result.push_back(result);

        project.ssdmutex->lock();
        project.nresults_sent++;
        project.ssdmutex->unlock();
    }
    return bag_of_result;
}

/*
 *	Scheduling server requests function
 */
int scheduling_server_requests(int argc, char *argv[])
{
    // Check number of arguments
    if (argc != 3)
    {
        std::cerr << "Invalid number of parameter in scheduling_server_requests()" << std::endl;
        return 0;
    }

    // Init boinc server
    int32_t project_number = (int32_t)atoi(argv[1]);           // Project number
    int32_t scheduling_server_number = (int32_t)atoi(argv[2]); // Scheduling server number

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number];        // Database
    sserver_t sserver_info = &SharedDatabase::_sserver_info[scheduling_server_number]; // Scheduling server info

    sserver_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name

    // Wait until database is ready
    project.barrier->wait();

    sg4::Mailbox *self_mailbox = sg4::Mailbox::by_name(sserver_info->server_name);
    while (1)
    {
        // Receive message
        auto msg = self_mailbox->get<SchedulingServerMessage>();

        // Termination message
        if (msg->type == TERMINATION)
        {
            delete msg;
            break;
        }
        // Client answer with execution results
        else if (msg->type == REPLY)
        {
            project.ssrmutex->lock();
            project.nmessages_received++;
            project.nresults_received++;
            project.ssrmutex->unlock();
        }
        // Client work request
        else
        {
            project.ssrmutex->lock();
            project.nmessages_received++;
            project.nwork_requests++;
            project.ssrmutex->unlock();
        }

        // Insert request into queue
        sserver_info->mutex->lock();
        sserver_info->Nqueue++;
        sserver_info->client_requests.push(msg);

        // If queue is not empty, wake up dispatcher process
        if (sserver_info->Nqueue > 0)
            sserver_info->cond->notify_all();
        sserver_info->mutex->unlock();

        msg = nullptr;
    }

    // Terminate dispatcher execution
    sserver_info->mutex->lock();
    sserver_info->EmptyQueue = 1;
    sserver_info->cond->notify_all();
    sserver_info->mutex->unlock();

    return 0;
}

/*
 *	Scheduling server dispatcher function
 */
int scheduling_server_dispatcher(int argc, char *argv[])
{
    dsmessage_t work = nullptr;           // Termination message
    std::vector<AssignedResult *> result; // Data server answer
    simgrid::s4u::CommPtr comm = nullptr; // Asynchronous communication
    sserver_t sserver_info = nullptr;     // Scheduling server info
    int32_t i, project_number;            // Index, project number
    int32_t scheduling_server_number;     // Scheduling_server_number
    double t0, t1;                        // Time measure

    sg4::ActivitySet _sscomm; // Asynchro communications storage (scheduling server with client)

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameter in scheduling_server_dispatcher()\n");
        return 0;
    }

    // Init boinc dispatcher
    t0 = t1 = 0.0;
    project_number = (int32_t)atoi(argv[1]);           // Project number
    scheduling_server_number = (int32_t)atoi(argv[2]); // Scheduling server number

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number]; // Server info
    sserver_info = &SharedDatabase::_sserver_info[scheduling_server_number];    // Scheduling server info

    while (1)
    {
        std::unique_lock lock(*sserver_info->mutex);

        // Wait until queue is not empty
        while ((sserver_info->Nqueue == 0) && (sserver_info->EmptyQueue == 0))
        {
            sserver_info->cond->wait(lock);
        }

        // Exit the loop when boinc server indicates it
        if ((sserver_info->EmptyQueue == 1) && sserver_info->Nqueue == 0)
        {
            break;
        }

        // Iteration start time
        t0 = sg4::Engine::get_clock();

        // Simulate server computation
        compute_server(36000000);

        // Pop client message
        auto msg = sserver_info->client_requests.front();
        sserver_info->client_requests.pop();
        sserver_info->Nqueue--;
        lock.unlock();

        // Check if message is an answer with the computation results
        if (msg->type == REPLY)
        {
            project.v_mutex->lock();

            // Call validator
            project.current_validations.push(reinterpret_cast<reply_t>(msg->content));
            project.ncurrent_validations++;

            project.v_empty->notify_all();
            project.v_mutex->unlock();
        }
        // Message is an address request
        else
        {
            // Consumer
            project.r_mutex->lock();

            if (project.ncurrent_results == 0)
            {
                // NO WORKUNITS
                // result = blank_result();
                result = {};
            }
            else
            {
                // CONSUME
                result = select_result(project_number, (request_t)msg->content);
            }

            project.r_mutex->unlock();

            // Answer the client
            auto caller_reply_mailbox = ((request_t)msg->content)->answer_mailbox;
            auto ans_mailbox = sg4::Mailbox::by_name(caller_reply_mailbox);

            ResultBag *msg_pack = new ResultBag{.results = result};
            int msg_sz = 0;
            for (auto &task : result)
            {
                msg_sz += KB * task->ninput_files;
            }

            comm = ans_mailbox->put_async(msg_pack, msg_sz);

            // Store the asynchronous communication created in the dictionary

            delete_completed_communications(_sscomm);
            _sscomm.push(comm);

            switch (msg->datatype)
            {
            case ssmessage_content::SReplyT:
                delete (reply_t)msg->content;
                break;
            case ssmessage_content::SRequestT:
                delete (request_t)msg->content;
                break;
            default:
                break;
            }
        }

        // Iteration end time
        t1 = sg4::Engine::get_clock();

        // Accumulate total time server is busy
        if (t0 < maxtt)
            sserver_info->time_busy += (t1 - t0);

        // Free
        delete msg;
        msg = nullptr;
        result.clear();
    }

    // Wait until all scheduling servers finish
    project.ssdmutex->lock();
    project.nfinished_scheduling_servers++;
    project.ssdmutex->unlock();

    // Check if it is the last scheduling server
    if (project.nfinished_scheduling_servers == project.nscheduling_servers)
    {
        // Send termination message to data servers
        for (i = 0; i < project.ndata_servers; i++)
        {
            // Create termination message
            work = new s_dsmessage_t();

            // Group power = -1 indicates it is a termination message
            work->type = TERMINATION;

            // Send message
            sg4::Mailbox::by_name(project.data_servers[i])->put(work, KB);
        }
        // Free
        project.data_servers.clear();

        // Finish project back-end
        project.wg_end = 1;
        project.v_end = 1;
        project.a_end = 1;
        project.wg_full->notify_all();
        project.v_empty->notify_all();
        project.a_empty->notify_all();
    }

    _sscomm.wait_all();

    return 0;
}
