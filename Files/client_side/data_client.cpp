/**
 * @brief
 * - data_client_ask_for_files: works all the time, asking for workunits if have memory
 * and asks for input file fom data server
 * - data_client_requests: simulates periods of inavailabilty and available, call dispatcher in the end of period
 * - data_client_dispatcher: simulates disk access to input and output files
 */

#include "data_client.hpp"
#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>
#include <boost/random/linear_congruential.hpp>

#include "components/types.hpp"
#include "components/shared.hpp"
#include "rand.hpp"
#include "parameters_struct_from_yaml.hpp"

XBT_LOG_NEW_DEFAULT_CATEGORY(data_client, "The logging channel used in data_client");

/*
 *	Disk access data clients simulation
 */
void disk_access(int64_t size)
{
    // Calculate sleep time
    double sleep = std::min((double)maxtt - sg4::Engine::get_clock() - PRECISION, (double)size / 80000000);
    if (sleep < 0)
        sleep = 0;

    // Sleep
    sg4::this_actor::sleep_for(sleep);
}

static sg4::MutexPtr _dclient_mutex = sg4::Mutex::create(); // Data client mutex

/*
 *	Data client ask for input files
 */
int data_client_ask_for_files(ask_for_files_t params)
{

    simgrid::s4u::CommPtr comm = NULL; // Asynchronous communication
                                       // double backoff = 300;

    XBT_DEBUG("Start asking for files od %d project", params->project_number);
    // group_t group_info = NULL;			// Group information
    dclient_t dclient_info = NULL;         // Data client information
    double storage = 0, max_storage = 0;   // File storage in MB
    char project_number, project_priority; // Project number and priority
    int i;

    // params = MSG_process_get_data(MSG_process_self());
    project_number = params->project_number;
    project_priority = params->project_priority;
    // todo: doesn't we need it? So?
    // group_info = params->group_info;
    dclient_info = params->dclient_info;
    sg4::Mailbox *self_mailbox = sg4::Mailbox::by_name(params->mailbox);

    max_storage = storage = (project_priority / dclient_info->sum_priority) * dclient_info->total_storage * KB * KB;
    XBT_DEBUG("ksenia %d %f %d", project_priority, dclient_info->sum_priority, dclient_info->total_storage);
    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[(int)project_number]; // Database

    // Reduce input file storage if output files are uploaded to data clients
    if (project.output_file_storage == 1)
    {
        max_storage /= 2.0;
        storage = max_storage;
    }
    XBT_DEBUG("max storage is %f", max_storage);

    project.dcmutex->lock();
    for (i = 0; i < project.ndata_clients; i++)
    {
        if (project.data_clients[i].empty())
        {
            project.data_clients[i] = dclient_info->server_name;
            break;
        }
    }
    project.dcmutex->unlock();

    XBT_DEBUG("start of the main cycle");
    while (sg4::Engine::get_clock() < maxtt)
    {
        dclient_info->ask_for_files_mutex->lock();
        if (dclient_info->finish)
        {
            dclient_info->ask_for_files_mutex->unlock();
            break;
        }
        dclient_info->ask_for_files_mutex->unlock();

        // Delete local files when there are completed workunits
        XBT_DEBUG("Delete local files, starting with %f when project input file is %ld", storage, project.input_file_size);
        while (storage < max_storage)
        {
            project.dcmutex->lock();
            if (project.ncurrent_deleted_workunits >= project.averagewpif)
            {
                project.ncurrent_deleted_workunits -= project.averagewpif;
                storage += project.input_file_size;
            }
            else
            {
                project.dcmutex->unlock();
                break;
            }
            project.dcmutex->unlock();
        }

        if (storage >= 0)
        {
            XBT_DEBUG("ask for workunits");
            // backoff = 300;

            // ASK FOR WORKUNITS -> DATA CLIENT SERVER
            dcsmessage_t dcsrequest = new s_dcsmessage_t();
            dcsrequest->type = REQUEST;
            dcsrequest->content = new s_dcsrequest_t();
            dcsrequest->datatype = dcsmessage_content::SDcsrequestT;
            ((dcsrequest_t)dcsrequest->content)->answer_mailbox = self_mailbox->get_name();

            auto chosen_ind = uniform_int(0, project.ndata_client_servers - 1, *g_rndg);
            auto data_client_server_mailbox = project.data_client_servers[chosen_ind];

            sg4::Mailbox::by_name(data_client_server_mailbox)->put(dcsrequest, 1);

            dcmessage_t dcreply = self_mailbox->get<dcmessage>();
            XBT_DEBUG("Received message from data client server with %d workuntis", dcreply->nworkunits);

            if (dcreply->nworkunits > 0)
            {
                // ASK FOR INPUT FILES -> DATA SERVERS
                for (auto &[key, workunit] : dcreply->workunits)
                {
                    if (workunit->status != IN_PROGRESS)
                        continue;

                    XBT_DEBUG("workunit with %s key in progress", key.c_str());
                    // Download input files (or generate them locally)
                    if (uniform_int(0, 99, *g_rndg) < (int)project.ifgl_percentage)
                    {
                        // Download only if the workunit was not downloaded previously
                        if (uniform_int(0, 99, *g_rndg) < (int)project.ifcd_percentage)
                        {
                            for (i = 0; i < workunit->ninput_files; i++)
                            {

                                /**
                                 * below we try to get file from data_server or data_client if there is
                                 * one with replica. First we go throw data_client and at last fall into data_server
                                 */

                                if (workunit->input_files[i].empty())
                                    continue;

                                std::string server_with_data = workunit->input_files[i];

                                // check if we are look at data_client's replica and data client is available
                                // if not - go further
                                // BORRAR (esta mal, no generico) (as I understood, it's about part of [server_number]
                                //          or about that if first need to query server to really understand if they work
                                //          or about order that first [project.dcreplication] are from data_client
                                if (i < project.dcreplication)
                                {
                                    int server_number = atoi(server_with_data.c_str() + 2) - g_total_number_ordinary_clients;
                                    if (SharedDatabase::_dclient_info[server_number].working.load() == 0)
                                        continue;
                                }

                                XBT_DEBUG("get file from %s", server_with_data.c_str());

                                dsmessage_t dsinput_file_request = new s_dsmessage_t();
                                dsinput_file_request->type = REQUEST;
                                dsinput_file_request->answer_mailbox = self_mailbox->get_name();

                                sg4::Mailbox::by_name(server_with_data)->put(dsinput_file_request, KB);

                                int *dsinput_file_reply_task = self_mailbox->get<int>();

                                // Timeout reached -> exponential backoff 2^N
                                /*if(error == MSG_TIMEOUT){
                                    backoff*=2;
                                    //free(dsinput_file_request);
                                    continue;
                                }*/

                                // Log request
                                project.rfiles_mutex->lock();
                                project.dcrfiles[i]++;
                                project.rfiles_mutex->unlock();

                                storage -= project.input_file_size;
                                delete dsinput_file_reply_task;
                                dsinput_file_reply_task = NULL;
                                break;
                            }
                        }
                    }

                    // ksenia: i see not reason for this break. Previous there was only one workunit
                    // break;
                }

                // CONFIRMATION MESSAGE TO DATA CLIENT SERVER
                dcsmessage_t dcsreply = new s_dcsmessage_t();
                dcsreply->type = REPLY;
                dcsreply->content = new s_dcsreply_t();
                dcsreply->datatype = dcsmessage_content::SDcsreplyT;
                ((dcsreply_t)dcsreply->content)->dclient_name = dclient_info->server_name;
                ((dcsreply_t)dcsreply->content)->workunits = dcreply->workunits;

                sg4::Mailbox::by_name(dcreply->answer_mailbox)->put(dcsreply, REPLY_SIZE);
            }
            else
            {
                // Sleep if there are no workunits
                sg4::this_actor::sleep_for(1800);
            }

            delete (dcreply);
            dcsrequest = NULL;
            dcreply = NULL;
        }
        // Sleep if
        if (sg4::Engine::get_clock() >= maxwt || storage <= 0)
            sg4::this_actor::sleep_for(60);
    }

    XBT_DEBUG("finished");

    // Finish data client servers execution
    _dclient_mutex->lock();
    project.nfinished_dclients++;

    _dclient_mutex->unlock();
    return 0;
}

/*
 *	Data client requests function
 */
int data_client_requests(int argc, char *argv[])
{
    group_t group_info = NULL;                   // Group information
    dclient_t dclient_info = NULL;               // Data client information
    ask_for_files_t ask_for_files_params = NULL; // Ask for files params
    int32_t data_client_number, group_number;    // Data client number, group number
    int terminated_projects_count = 0;           // Index, termination count

    // Availability params
    double time = 0, random;

    // Check number of arguments
    if (argc != 4)
    {
        printf("Invalid number of parameters in data_client_requests\n");
        return 0;
    }

    // Init data client
    group_number = (int32_t)atoi(argv[1]);       // Group number
    data_client_number = (int32_t)atoi(argv[2]); // Data client number

    group_info = &SharedDatabase::_group_info[group_number];             // Group info
    dclient_info = &SharedDatabase::_dclient_info[data_client_number];   // Data client info
    dclient_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name
    dclient_info->navailable = 0;

    // Wait until group info is ready
    {
        std::unique_lock lock(*group_info->mutex);
        while (group_info->on == 0)
            group_info->cond->wait(lock);
    }

    dclient_info->working.store(uniform_int(1, 2, *g_rndg));
    if ((dclient_info->total_storage = parameters::parse_trace_parameter(argv[3])) == -1)
    {
        dclient_info->total_storage = (int32_t)ran_distri(group_info->db_distri, group_info->da_param, group_info->db_param, *g_rndg_for_disk_cap);
    }
    assert(dclient_info->total_storage >= 0 && "host's disk capacity can't be negative");

    // Create ask for files processes (1 per attached project)
    dclient_info->nprojects = atoi(group_info->proj_args[0]);
    dclient_info->sum_priority = 0;

    for (int i = 0; i < dclient_info->nprojects; i++)
    {
        dclient_info->sum_priority += (double)atof(group_info->proj_args[i * 5 + 3]);
    }
    for (int i = 0; i < dclient_info->nprojects; i++)
    {
        ask_for_files_params = new s_ask_for_files_t();
        ask_for_files_params->project_number = (char)atoi(group_info->proj_args[i * 5 + 2]);
        ask_for_files_params->project_priority = (char)atoi(group_info->proj_args[i * 5 + 3]);

        ask_for_files_params->group_info = group_info;
        ask_for_files_params->dclient_info = dclient_info;
        ask_for_files_params->mailbox = bprintf("%s%d", dclient_info->server_name.c_str(), ask_for_files_params->project_number);

        sg4::Actor::create("ask_for_files_thread", sg4::this_actor::get_host(), data_client_ask_for_files, ask_for_files_params);
    }

    sg4::Mailbox *self_mailbox = sg4::Mailbox::by_name(dclient_info->server_name);

    while (1)
    {

        // Available
        if (dclient_info->working.load() == 2)
        {
            dclient_info->working.store(1);
            random = (ran_distri(group_info->av_distri, group_info->aa_param, group_info->ab_param, *g_rndg_for_data_client_avail) * 3600.0);
            if (ceil(random + sg4::Engine::get_clock() >= maxtt))
                random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            time = sg4::Engine::get_clock() + random;
        }

        // Non available
        if (dclient_info->working.load() == 1 && ceil(sg4::Engine::get_clock()) >= time)
        {
            random = (ran_distri(group_info->nv_distri, group_info->na_param, group_info->nb_param, *g_rndg_for_data_client_avail) * 3600.0);
            if (ceil(random + sg4::Engine::get_clock() >= maxtt))
                random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            if (random > 0)
                dclient_info->working.store(0);
            dclient_info->navailable += random;
            sg4::this_actor::sleep_for(random);
            dclient_info->working.store(2);
        }

        // Receive message
        dsmessage_t msg = self_mailbox->get<dsmessage>();

        // Termination message
        if (msg->type == TERMINATION)
        {
            delete (msg);

            terminated_projects_count++;
            if (terminated_projects_count == dclient_info->nprojects)
                break;
            msg = NULL;
            continue;
        }

        // Insert request into queue
        dclient_info->mutex->lock();
        dclient_info->Nqueue++;
        dclient_info->client_requests.push(msg);

        // If queue is not empty, wake up dispatcher process
        if (dclient_info->Nqueue > 0)
            dclient_info->cond->notify_all();
        dclient_info->mutex->unlock();

        // Free
        msg = NULL;
    }

    // Terminate dispatcher execution
    dclient_info->mutex->lock();
    dclient_info->EmptyQueue = 1;
    dclient_info->cond->notify_all();
    dclient_info->mutex->unlock();

    return 0;
}

/*
 *	Data client dispatcher function
 */
int data_client_dispatcher(int argc, char *argv[])
{
    simgrid::s4u::CommPtr comm = NULL; // Asynchronous comm
    dsmessage_t msg = NULL;            // Client message
    dclient_t dclient_info = NULL;     // Data client information
    int32_t data_client_number;        // Data client number
    double t0, t1;                     // Time

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameters in data_client_dispatcher\n");
        return 0;
    }

    // Init data client
    data_client_number = (int32_t)atoi(argv[2]); // Data client number

    dclient_info = &SharedDatabase::_dclient_info[data_client_number]; // Data client info

    sg4::ActivitySet _dscomm;

    while (1)
    {
        std::unique_lock lock(*dclient_info->mutex);

        // Wait until queue is not empty
        while ((dclient_info->Nqueue == 0) && (dclient_info->EmptyQueue == 0))
        {
            dclient_info->cond->wait(lock);
        }

        // Exit the loop when requests function indicates it
        if ((dclient_info->EmptyQueue == 1) && (dclient_info->Nqueue == 0))
        {
            break;
        }

        // Pop client message
        msg = dclient_info->client_requests.front();
        dclient_info->client_requests.pop();
        dclient_info->Nqueue--;
        lock.unlock();

        t0 = sg4::Engine::get_clock();

        // Simulate server computation
        compute_server(20);

        ProjectDatabaseValue &project = SharedDatabase::_pdatabase[(int)msg->proj_number];

        // Reply with output file
        if (msg->type == REPLY)
        {
            disk_access(project.output_file_size);
        }
        // Input file request
        else if (msg->type == REQUEST)
        {
            // Read tasks from disk
            disk_access(project.input_file_size);

            // Create the message

            // Answer the client
            comm = sg4::Mailbox::by_name(msg->answer_mailbox)->put_async(new int(2), project.input_file_size);

            // Store the asynchronous communication created in the dictionary
            delete_completed_communications(_dscomm);
            _dscomm.push(comm);
        }

        delete (msg);
        msg = NULL;

        // Iteration end time
        t1 = sg4::Engine::get_clock();

        // Accumulate total time server is busy
        if (t0 < maxtt && t0 >= maxwt)
            dclient_info->time_busy += (t1 - t0);
    }
    _dscomm.wait_all();

    dclient_info->ask_for_files_mutex->lock();
    dclient_info->finish = 1;
    dclient_info->ask_for_files_mutex->unlock();

    return 0;
}

namespace for_testing
{
    void disk_access_test(int64_t size)
    {
        disk_access(size);
    }
    int data_client_ask_for_files_test(ask_for_files_t params)
    {
        return data_client_ask_for_files(params);
    }
}