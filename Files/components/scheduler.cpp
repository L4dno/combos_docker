#include "scheduler.hpp"

#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>

#include "types.hpp"
#include "shared.hpp"
// typedef struct ssmessage s_ssmessage_t, *ssmessage_t; // Message to data server

namespace sg4 = simgrid::s4u;

/*
 *	Select result from database
 */
AssignedResult *select_result(int project_number, request_t req)
{

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number];

    // Get result
    AssignedResult *result = project.current_results.front();
    project.current_results.pop();

    // Signal work generator if number of current results is 0
    project.ncurrent_results--;
    if (project.ncurrent_results == 0)
        project.wg_full->notify_all();

    // Calculate number of tasks
    result->number_tasks = (int32_t)floor(req->percentage / ((double)project.job_duration / req->power));
    if (result->number_tasks == 0)
        result->number_tasks = (int32_t)1;

    // Create tasks
    result->tasks.resize((int)result->number_tasks);

    // Fill tasks
    for (int i = 0; i < result->number_tasks; i++)
    {
        TaskT *task = new TaskT();
        task->workunit = result->workunit->number;
        task->result_number = result->number;
        task->name = std::string(bprintf("%" PRId32, result->workunit->nsent_results++));
        task->duration = project.job_duration * ((double)req->group_power / req->power);
        task->deadline = project.delay_bound;
        task->start = sg4::Engine::get_clock();
        result->tasks[i] = task;
    }

    project.ssdmutex->lock();
    project.nresults_sent++;
    project.ssdmutex->unlock();

    return result;
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

    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(sserver_info->server_name);
    while (1)
    {
        // Receive message
        auto msg = mailbox->get<SchedulingServerMessage>();

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
    AssignedResult *result = nullptr;     // Data server answer
    simgrid::s4u::CommPtr comm = nullptr; // Asynchronous communication
    sserver_t sserver_info = nullptr;     // Scheduling server info
    int32_t i, project_number;            // Index, project number
    int32_t scheduling_server_number;     // Scheduling_server_number
    double t0, t1;                        // Time measure

    std::vector<sg4::CommPtr> _sscomm; // Asynchro communications storage (scheduling server with client)

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
                result = blank_result();
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

            comm = ans_mailbox->put_async(result, KB * result->ninput_files);

            // Store the asynchronous communication created in the dictionary

            delete_completed_communications(_sscomm);
            _sscomm.push_back(comm);

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
        result = nullptr;
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

    return 0;
}
