/**
 * @brief
 * takes client's request which is
 * - request: assigns workunits to client
 * - reply: assigns where to get input files and marks
 *          data client as confirmed data from request part.
 *          if all replicas confirm - creates results for workunits
 */

#include "data_client_server.hpp"
#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>

#include "types.hpp"
#include "shared.hpp"

/*
 *	Data client server requests function
 */
int data_client_server_requests(int argc, char *argv[])
{
    dcsmessage_t msg = NULL;                           // Data client message
    dcserver_t dcserver_info = NULL;                   // Data client server info
    int32_t data_client_server_number, project_number; // Data client server number, project number

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameters in data_client_server_requests\n");
        return 0;
    }

    // Init data client server requests
    project_number = (int32_t)atoi(argv[1]);            // Project number
    data_client_server_number = (int32_t)atoi(argv[2]); // Data client server number

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number]; // Database
    dcserver_info = &SharedDatabase::_dcserver_info[data_client_server_number]; // Data client server info

    dcserver_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name

    // Wait until database is ready
    project.barrier->wait();

    // Set asynchronous receiving in mailbox
    // MSG_mailbox_set_async(dcserver_info->server_name);
    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(dcserver_info->server_name);

    while (1)
    {
        // Receive message

        msg = mailbox->get<dcsmessage>();

        // Termination message
        if (msg->type == TERMINATION)
        {
            delete msg;
            break;
        }

        // Insert request into queue
        dcserver_info->mutex->lock();
        dcserver_info->Nqueue++;
        dcserver_info->client_requests.push(msg);

        // If queue is not empty, wake up dispatcher process
        if (dcserver_info->Nqueue > 0)
            dcserver_info->cond->notify_all();
        dcserver_info->mutex->unlock();

        msg = NULL;
    }

    dcserver_info->mutex->lock();
    dcserver_info->EmptyQueue = 1;
    dcserver_info->cond->notify_all();
    dcserver_info->mutex->unlock();

    return 0;
}

/*
 *	Data client server dispatcher function
 */
int data_client_server_dispatcher(int argc, char *argv[])
{
    dcsmessage_t msg = NULL;                           // Data client message
    dcmessage_t ans_msg = NULL;                        // Answer to data client
    dcserver_t dcserver_info = NULL;                   // Data client server info
    int32_t data_client_server_number, project_number; // Data client server number, project number

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameters in data_client_server_dispatcher\n");
        return 0;
    }

    // Init data client server dispatcher
    project_number = (int32_t)atoi(argv[1]);            // Project number
    data_client_server_number = (int32_t)atoi(argv[2]); // Data client server number

    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[project_number]; // Database
    dcserver_info = &SharedDatabase::_dcserver_info[data_client_server_number]; // Data client server info

    while (1)
    {
        std::unique_lock lock(*dcserver_info->mutex);

        // Wait until queue is not empty
        while ((dcserver_info->Nqueue == 0) && (dcserver_info->EmptyQueue == 0))
        {
            dcserver_info->cond->wait(lock);
        }

        // Exit the loop when data client server requests indicates it
        if ((dcserver_info->EmptyQueue == 1) && (dcserver_info->Nqueue == 0))
        {
            break;
        }
        msg = dcserver_info->client_requests.front();
        dcserver_info->client_requests.pop();
        dcserver_info->Nqueue--;
        lock.unlock();

        // Confirmation message
        if (msg->type == REPLY)
        {
            for (auto &[key, workunit] : ((dcsreply_t)msg->content)->workunits)
            {
                for (int i = 0; i < workunit->ninput_files; i++)
                {
                    if (workunit->input_files[i].empty())
                    {
                        workunit->input_files[i] = ((dcsreply_t)msg->content)->dclient_name;
                        break;
                    }
                }
                workunit->ndata_clients_confirmed++;

                // Generate target_nresults instances when workunit is confirmed from all data clients
                // todo: isn't more logical (at first glance) not to wait for all?s
                if (workunit->ndata_clients_confirmed == project.dcreplication)
                {
                    for (int i = 0; i < project.target_nresults; i++)
                    {
                        auto result = generate_result(project, workunit, 0);
                        {
                            std::unique_lock lock(*project.r_mutex);
                            project.current_results.push_back(result);
                        }
                    }
                }
            }
            ((dcsreply_t)msg->content)->workunits.clear();
        }
        // Request message
        else
        {
            ans_msg = new s_dcmessage_t();
            ans_msg->answer_mailbox = dcserver_info->server_name;
            ans_msg->nworkunits = 0;
            ans_msg->workunits.clear();

            project.w_mutex->lock();
            // iterate over prepared workunits
            for (auto &[key, workunit] : project.current_workunits)
            {
                if (ans_msg->nworkunits < project.averagewpif)
                {
                    // if workunit isn't finished yet
                    // and in overall that workunit has to be sent to [project.dcreplication] hosts
                    // and wait till at least one data client confirms before start sending more
                    if (workunit->status == IN_PROGRESS &&
                        workunit->ndata_clients < project.dcreplication &&
                        (workunit->ndata_clients_confirmed > 0 ||
                         workunit->ndata_clients == 0))
                    {
                        workunit->ndata_clients++;
                        // todo: is it done inside ... loop?
                        ans_msg->answer_mailbox = dcserver_info->server_name;
                        ans_msg->nworkunits++;
                        ans_msg->workunits[workunit->number] = workunit;
                    }
                }
                else
                    break;
            }

            if (ans_msg->workunits.empty())
            {
                project.wg_dcs = 1;
                project.wg_full->notify_all();
            }
            project.w_mutex->unlock();

            sg4::Mailbox::by_name(((dcsrequest_t)msg->content)->answer_mailbox)->put(ans_msg, REPLY_SIZE);
        }

        switch (msg->datatype)
        {
        case dcsmessage_content::SDcsreplyT:
            delete (dcsreply_t)msg->content;
            break;
        case dcsmessage_content::SDcsrequestT:
            delete (dcsrequest_t)msg->content;
            break;
        default:
            break;
        }

        delete msg;
        msg = NULL;
        ans_msg = NULL;
    }

    return 0;
}

/* ########## END DATA CLIENT SERVER ########## */
