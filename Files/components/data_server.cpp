#include "data_server.hpp"
#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>

#include "types.hpp"
#include "shared.hpp"

/**
 * @brief
 * takes client's request which is
 * - reply: simulate disk access to output files
 * - request: simulate disk access to input files and answer the client's request
 */

/*
 *	Disk access simulation
 */
void disk_access(int32_t server_number, int64_t size)
{
    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[server_number]; // Server info

    // Calculate sleep time

    double sleep = std::min((double)maxtt - sg4::Engine::get_clock() - PRECISION, (double)size / project.disk_bw);
    if (sleep < 0)
        sleep = 0;

    // Sleep
    sg4::this_actor::sleep_for(sleep);
}

/*
 *	Data server requests function
 */
int data_server_requests(int argc, char *argv[])
{
    // Check number of arguments
    if (argc != 2)
    {
        printf("Invalid number of parameter in data_server_requests()\n");
        return 0;
    }

    // Init parameters
    int32_t server_number = (int32_t)atoi(argv[1]);                         // Data server number
    dserver_t dserver_info = &SharedDatabase::_dserver_info[server_number]; // Data server info pointer
    dserver_info->server_name = sg4::this_actor::get_host()->get_name();    // Data server name

    sg4::Mailbox *self_mailbox = sg4::Mailbox::by_name(dserver_info->server_name);

    while (1)
    {
        // Receive message
        dsmessage_t req = self_mailbox->get<dsmessage>();

        // Termination message
        if (req->type == TERMINATION)
        {
            delete req;
            break;
        }

        // Insert request into queue
        dserver_info->mutex->lock();
        dserver_info->Nqueue++;
        dserver_info->client_requests.push(req);

        // If queue is not empty, wake up dispatcher process
        if (dserver_info->Nqueue > 0)
            dserver_info->cond->notify_all(); // wake up dispatcher
        dserver_info->mutex->unlock();

        // Free
        req = NULL;
    }

    // Terminate dispatcher execution
    dserver_info->mutex->lock();
    dserver_info->EmptyQueue = 1;
    dserver_info->cond->notify_all();
    dserver_info->mutex->unlock();

    return 0;
}

/*
 *	Data server dispatcher function
 */
int data_server_dispatcher(int argc, char *argv[])
{
    dserver_t dserver_info = NULL;
    simgrid::s4u::CommPtr comm = NULL; // Asynch communication
    int32_t server_number, project_number;
    double t0, t1;

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameter in data_server_dispatcher()\n");
        return 0;
    }

    // Init data dispatcer
    server_number = (int32_t)atoi(argv[1]);
    project_number = (int32_t)atoi(argv[2]);

    dserver_info = &SharedDatabase::_dserver_info[server_number];               // Data server info pointer
    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number]; // Boinc server info pointer

    sg4::ActivitySet _dscomm; // Asynchro communications storage (data server with client)

    while (1)
    {
        std::unique_lock lock(*dserver_info->mutex);

        // Wait while queue is not empty
        while ((dserver_info->Nqueue == 0) && (dserver_info->EmptyQueue == 0))
        {
            dserver_info->cond->wait(lock);
        }

        // Exit the loop when boinc server indicates it
        if ((dserver_info->EmptyQueue == 1) && (dserver_info->Nqueue == 0))
        {
            break;
        }

        // Iteration start time
        t0 = sg4::Engine::get_clock();

        // Simulate server computation
        compute_server(20);

        // Pop client message
        dsmessage_t req = dserver_info->client_requests.front();
        dserver_info->client_requests.pop();
        dserver_info->Nqueue--;
        lock.unlock();

        // Reply with output file
        if (req->type == REPLY)
        {
            disk_access(project_number, project.output_file_size);
            project.dsuploads_mutex->lock();
            project.dsuploads++;
            project.dsuploads_mutex->unlock();
        }
        // Input file request
        else
        {
            // Read tasks from disk
            disk_access(project_number, project.input_file_size);

            // Answer the client
            comm = sg4::Mailbox::by_name(req->answer_mailbox)->put_async(new int(1), project.input_file_size);

            // Store the asynchronous communication created in the dictionary

            delete_completed_communications(_dscomm);
            _dscomm.push(comm);
        }

        // Iteration end time
        t1 = sg4::Engine::get_clock();

        // Accumulate total time server is busy
        if (t0 < maxtt && t0 >= maxwt)
            dserver_info->time_busy += (t1 - t0);

        // Free
        delete req;
        req = NULL;
    }

    _dscomm.wait_all();

    return 0;
}
