#include "fetch_work.hpp"
#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>

#include "components/types.hpp"
#include "components/shared.hpp"
#include "rand.hpp"

/**
 * @brief straightforward
 * - client_update_shortfall - estimates requested work
 * - client_ask_for_work - for a project sends all calculated results, get new tasks, simulates getting input data
 * - client_work_fetch - finds suitable project, simulates unavailability and call [client_ask_for_work]
 *
 * @ref to real boinc
 * It's different from a real policy.
 * According to wiki: https://github.com/BOINC/boinc/wiki/ClientSched#work-fetch-policy
 *
 * for instance, here we calculate debt + shortfall, but in the documents - scheduling priority.
 */

/*****************************************************************************/
/* update shortfall(amount of work needed to keep 1 cpu busy during next ConnectionInterval) of client */
static void client_update_shortfall(client_t client)
{
    TaskT *task;
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;
    double total_time_proj;
    double total_time = 0;
    int64_t power; // (maximum 2⁶³-1)

    client->no_actions = 1;
    power = client->power;
    for (auto &[key, proj] : projects)
    {
        total_time_proj = 0;
        {

            std::unique_lock lock(*proj->tasks_swag_mutex);
            for (auto &task : proj->tasks_swag)
            {
                total_time_proj += (task.msg_task->get_remaining() * client->factor) / power;

                // printf("SHORTFALL(1) %g   %s    %g   \n",  sg4::Engine::get_clock(), proj->name,   MSG_task_get_remaining_computation(task->msg_task));
                client->no_actions = 0;
            }
        }
        {
            std::unique_lock lock(*proj->run_list_mutex);

            for (auto &task : proj->run_list)
            {
                total_time_proj += (task.msg_task->get_remaining() * client->factor) / power;
                client->no_actions = 0;
                // printf("SHORTFALL(2) %g  %s    %g   \n",  sg4::Engine::get_clock(), proj->name,   MSG_task_get_remaining_computation(task->msg_task));
            }
        }
        total_time += total_time_proj;
        /* amount of work needed - total already loaded */
        proj->shortfall = SharedDatabase::_group_info[client->group_number].connection_interval * proj->priority / client->sum_priority - total_time_proj;

        if (proj->shortfall < 0)
            proj->shortfall = 0;
    }
    client->total_shortfall = SharedDatabase::_group_info[client->group_number].connection_interval - total_time;
    if (client->total_shortfall < 0)
        client->total_shortfall = 0;
}

static void send_finished_work(client_t client, ProjectInstanceOnClient *proj)
{
    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[(int)proj->number]; // Boinc server info pointer

    // Check if there are executed results
    while (proj->total_tasks_executed > proj->total_tasks_checked)
    {

        // Create execution results message
        auto ssexecution_results = new SchedulingServerMessage();
        ssexecution_results->type = REPLY;
        ssexecution_results->content = new s_reply_t();
        ssexecution_results->datatype = ssmessage_content::SReplyT;

        // Increase number of tasks checked
        proj->total_tasks_checked++;

        // Executed task status [SUCCES, FAIL]
        if (uniform_int(0, 99, *g_rndg) < proj->success_percentage)
        {
            ((reply_t)ssexecution_results->content)->status = SUCCESS;
            // Executed task value [CORRECT, INCORRECT]
            if (uniform_int(0, 99, *g_rndg) < proj->canonical_percentage)
                ((reply_t)ssexecution_results->content)->value = CORRECT;
            else
                ((reply_t)ssexecution_results->content)->value = INCORRECT;
        }
        else
        {
            ((reply_t)ssexecution_results->content)->status = FAIL;
            ((reply_t)ssexecution_results->content)->value = INCORRECT;
        }

        // Pop executed result number and associated workunit
        ((reply_t)ssexecution_results->content)->result_number = proj->number_executed_task.pop();
        ((reply_t)ssexecution_results->content)->workunit = proj->workunit_executed_task.pop();

        // Calculate credits
        ((reply_t)ssexecution_results->content)->credits = (int32_t)((int64_t)project.job_duration / 1000000000.0 * CREDITS_CPU_S);
        // Create execution results task

        // Send message to the server
        auto &scheduling_server_mailbox = project.scheduling_servers[uniform_int(0, project.nscheduling_servers - 1, *g_rndg)];

        sg4::Mailbox::by_name(scheduling_server_mailbox)->put(ssexecution_results, REPLY_SIZE);

        if (project.output_file_storage == 0)
        {
            // Upload output files to data servers
            for (int32_t i = 0; i < project.dsreplication; i++)
            {
                dsmessage_t dsoutput_file = new s_dsmessage_t();
                dsoutput_file->type = REPLY;
                auto data_server_mailbox = project.data_servers[uniform_int(0, project.ndata_servers - 1, *g_rndg)];

                sg4::Mailbox::by_name(data_server_mailbox)->put(dsoutput_file, project.output_file_size);
            }
        }
        else
        {
            // Upload output files to data clients

            for (int32_t i = 0; i < project.dcreplication; i++)
            {
                std::string data_client_name = project.data_clients[uniform_int(0, project.ndata_clients - 1, *g_rndg)];
                int data_client_number = atoi(data_client_name.c_str() + 2) - g_total_number_ordinary_clients;
                if (!SharedDatabase::_dclient_info[data_client_number].working.load())
                {
                    i--;
                    continue;
                }

                dsmessage_t dsoutput_file = new s_dsmessage_t();
                dsoutput_file->type = REPLY;
                sg4::Mailbox::by_name(data_client_name)->put(dsoutput_file, project.output_file_size);
            }
        }
    }
}

/*
 *	Client ask for work:
 *
 *	- Request work to scheduling_server
 *	- Download input files from data server
 *	- Send execution results to scheduling_server
 *	- Upload output files to data server
 */
static int client_ask_for_work(client_t client, ProjectInstanceOnClient *proj, double percentage)
{
    /*

    WORK REQUEST NEEDS:

        - type: REQUEST
        - content: request_t
        - content->answer_mailbox: Client mailbox
        - content->group_power: Group power
        - content->power: Host power
        - content->percentage: Percentage of project (in relation to all projects)

    INPUT FILE REQUEST NEEDS:

        - type: REQUEST
        - answer_mailbox: Client mailbox

    EXECUTION RESULTS REPLY NEEDS:

        - type: REPLY
        - content: reply_t
        - content->result_number: executed result number
        - content->workunit: associated workunit
        - content->credits: number of credits to request

    OUTPUT FILE REPLY NEEDS:

        - type: REPLY

    */
    ProjectDatabaseValue &project = SharedDatabase::_pdatabase[(int)proj->number]; // Boinc server info pointer

    // msg_error_t error;				// Sending result
    // double backoff = 300;				// 1 minute initial backoff

    // Request work
    SchedulingServerMessage *sswork_request = new SchedulingServerMessage();
    sswork_request->type = REQUEST;
    sswork_request->content = new s_request_t();
    sswork_request->datatype = ssmessage_content::SRequestT;
    ((request_t)sswork_request->content)->answer_mailbox = proj->answer_mailbox;
    ((request_t)sswork_request->content)->group_power = SharedDatabase::_group_info[client->group_number].group_power;
    ((request_t)sswork_request->content)->power = client->power;
    ((request_t)sswork_request->content)->percentage = percentage;
    ((request_t)sswork_request->content)->host_name = sg4::this_actor::get_host()->get_name();

    auto &scheduling_server_mailbox = project.scheduling_servers[uniform_int(0, project.nscheduling_servers - 1, *g_rndg)];

    sg4::Mailbox::by_name(scheduling_server_mailbox)->put(sswork_request, REQUEST_SIZE);

    // Receive reply from scheduling server
    ResultBag *results_to_execute = sg4::Mailbox::by_name(proj->answer_mailbox)->get<ResultBag>(); // Get work

    if (results_to_execute->results.size() == 0)
        proj->on = 0;

    // todo: i can make requests asynchronous and don't wait for responses
    for (auto &sswork_reply : results_to_execute->results)
    {

        // Download input files (or generate them locally)
        if (uniform_int(0, 99, *g_rndg) < (int)project.ifgl_percentage)
        {
            // Download only if the workunit was not downloaded previously
            if (uniform_int(0, 99, *g_rndg) < (int)project.ifcd_percentage)
            {

                for (int32_t i = 0; i < sswork_reply->ninput_files; i++)
                {
                    if (sswork_reply->input_files[i].empty())
                        continue;
                    std::string server_with_data = sswork_reply->input_files[i];
                    if (!the_same_client_group(sg4::this_actor::get_host()->get_name(), server_with_data))
                    {
                        continue;
                    }

                    // BORRAR (esta mal)
                    if (i < project.dcreplication)
                    {
                        int server_number = atoi(server_with_data.c_str() + 2) - g_total_number_ordinary_clients;
                        // printf("resto: %d, server_number: %d\n", g_total_number_ordinary_clients, server_number);
                        // printf("%d\n", SharedDatabase::_dclient_info[server_number].working);
                        //  BORRAR
                        // if(i==1) printf("%d\n", SharedDatabase::_dclient_info[server_number].working);
                        if (SharedDatabase::_dclient_info[server_number].working.load() == 0)
                            continue;
                    }

                    dsmessage_t dsinput_file_request = new s_dsmessage_t();
                    dsinput_file_request->type = REQUEST;
                    dsinput_file_request->proj_number = proj->number;
                    dsinput_file_request->answer_mailbox = proj->answer_mailbox;

                    sg4::Mailbox::by_name(server_with_data)->put(dsinput_file_request, KB);

                    // error = MSG_task_receive_with_timeout(&dsinput_file_reply_task, proj->answer_mailbox, backoff);		// Send input file reply
                    dsmessage_t dsinput_file_reply_task = sg4::Mailbox::by_name(proj->answer_mailbox)->get<s_dsmessage_t>();

                    // Log request
                    project.rfiles_mutex->lock();
                    project.rfiles[i]++;
                    project.rfiles_mutex->unlock();

                    break;
                }
            }
        }

        // Insert received tasks in tasks swag
        TaskT *t = sswork_reply->corresponding_tasks;
        t->msg_task = simgrid::s4u::Exec::init();
        t->msg_task->set_name(t->name);
        t->msg_task->set_flops_amount(t->duration);
        t->project = proj;
        {
            std::unique_lock lock(*proj->tasks_swag_mutex);

            proj->tasks_swag.push_back(*t);
        }
        // Increase the total number of tasks received
        proj->total_tasks_received++;
    }
    // Free
    delete (results_to_execute);

    // Signal main client process
    client->on = 0;
    client->sched_cond->notify_all();

    return 0;
}

/*
    Client work fetch
*/
int client_work_fetch(client_t client)
{
    static char first = 1;
    double work_percentage = 0;
    double control, sleep;
    // if scheduling server doesn't have results to send, we shut down project and never ever ask for work, even if it generated
    // todo: i need to check with documents, but for now we test it once in a time
    double project_testing_period = 3 * WORK_FETCH_PERIOD;
    double next_project_testing_period = project_testing_period;

    sg4::this_actor::sleep_for(maxwt);
    sg4::this_actor::sleep_for(uniform_ab(0, 3600, *g_rndg));

    // client_t client = MSG_process_get_data(MSG_process_self());
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;

    // printf("Running thread work fetch client %s\n", client->name);

    {
        std::unique_lock lock(*client->mutex_init);
        while (client->initialized == 0)
            client->cond_init->wait(lock);
    }

    while (ceil(sg4::Engine::get_clock()) < maxtt)
    {

        /* Wait when the client is suspended */
        client->ask_for_work_mutex->lock();

        while (client->suspended)
        {
            sleep = client->suspended;
            client->suspended = 0;
            client->ask_for_work_mutex->unlock();
            // printf("sleep time: %f\n", sleep);
            sg4::this_actor::sleep_for(sleep);
            client->ask_for_work_mutex->lock();

            continue;
        }

        client->ask_for_work_mutex->unlock();

        client_update_shortfall(client);

        // turn on projects
        if (sg4::Engine::get_clock() >= next_project_testing_period)
        {
            next_project_testing_period += project_testing_period;
            for (auto &[key, proj] : projects)
            {
                proj->on = 1;
            }
        }

        bool were_waiting_on = false;

        ProjectInstanceOnClient *selected_proj = nullptr;
        for (auto &[key, proj] : projects)
        {
            // todo: in the real system i saw results sent once they're done. If it's not true then i need to find more precious politics
            send_finished_work(client, proj);

            /* if there are no running tasks so we can download from all projects. Don't waste processing time */
            // if (client->running_project != NULL && client->running_project->running_task && proj->long_debt < -_group_power[client->group_number].scheduling_interval) {
            // printf("Shortfall %s: %f\n", proj->name, proj->shortfall);
            if (!proj->on)
            {
                were_waiting_on = true;
                continue;
            }
            if (!client->no_actions && proj->long_debt < -SharedDatabase::_group_info[client->group_number].scheduling_interval)
            {
                continue;
            }
            if (proj->shortfall == 0)
                continue;
            /* FIXME: CONFLIT: the article says (long_debt - shortfall) and the wiki(http://boinc.berkeley.edu/trac/wiki/ClientSched) says (long_debt + shortfall). I will use here the wiki definition because it seems have the same behavior of web client simulator.*/

            ///////******************************///////

            if ((selected_proj == NULL) || (control < (proj->long_debt + proj->shortfall)))
            {
                control = proj->long_debt + proj->shortfall;
                selected_proj = proj;
            }
            if (fabs(control - proj->long_debt - proj->shortfall) < PRECISION)
            {
                control = proj->long_debt + proj->shortfall;
                selected_proj = proj;
            }
        }

        if (selected_proj)
        {
            // printf("Selected project(%s) shortfall %lf %d\n", selected_proj->name, selected_proj->shortfall, selected_proj->shortfall > 0);
            /* prrs = sum_priority, all projects are potentially runnable */
            work_percentage = std::max(selected_proj->shortfall, client->total_shortfall / client->sum_priority);
            // printf("%s -> work_percentage: %f\n", selected_proj->name, work_percentage); // SAUL
            // printf("Heap size: %d\n", heap_size(client->deadline_missed)); // SAUL

            /* just ask work if there aren't deadline missed jobs
FIXME: http://www.boinc-wiki.info/Work-Fetch_Policy */
            if (client->deadline_missed.empty() && work_percentage > 0)
            {
                // printf("*************    ASK FOR WORK      %g   %g\n",   work_percentage, sg4::Engine::get_clock());
                client_ask_for_work(client, selected_proj, work_percentage);
            }
        }
        /* workaround to start scheduling tasks at time 0 */
        if (first)
        {
            // printf(" work_fetch: %g\n", sg4::Engine::get_clock());
            client->on = 0;
            client->sched_cond->notify_all();
            first = 0;
        }

        try
        {
            if (sg4::Engine::get_clock() >= (maxtt - WORK_FETCH_PERIOD))
                break;

            std::unique_lock lock(*client->work_fetch_mutex);
            if (!selected_proj || !client->deadline_missed.empty() || work_percentage == 0)
            {
                // printf("EXIT 1: remaining %f, time %f\n", max-sg4::Engine::get_clock(), sg4::Engine::get_clock());
                // sg4::ConditionVariableimedwait(client->work_fetch_cond, client->work_fetch_mutex, max(0, max-sg4::Engine::get_clock()));
                if (were_waiting_on)
                {
                    client->work_fetch_cond->wait_until(lock, next_project_testing_period);
                }
                else
                {
                    client->work_fetch_cond->wait(lock);
                }

                // printf("SALGO DE EXIT 1: remaining %f, time %f\n", max-sg4::Engine::get_clock(), sg4::Engine::get_clock());
            }
            else
            {
                // printf("EXIT 2: remaining %f time %f\n", max-sg4::Engine::get_clock(), sg4::Engine::get_clock());

                client->work_fetch_cond->wait_for(lock, WORK_FETCH_PERIOD);

                // printf("SALGO DE EXIT 2: remaining %f, time %f\n", max-sg4::Engine::get_clock(), sg4::Engine::get_clock());
            }
        }
        catch (std::exception &e)
        {
            std::cout << "exception at the line " << __LINE__ << ' ' << e.what() << std::endl;
            // printf("Error %d %d\n", (int)sg4::Engine::get_clock(), (int)max);
        }
    }

    // Sleep until max simulation time
    if (sg4::Engine::get_clock() < maxtt)
        sg4::this_actor::sleep_for(maxtt - sg4::Engine::get_clock());

    // Signal main client thread
    client->ask_for_work_mutex->lock();
    client->suspended = -1;
    client->ask_for_work_cond->notify_all();
    client->ask_for_work_mutex->unlock();

    // printf("Finished work_fetch %s: %d in %f\n", client->name, client->finished, sg4::Engine::get_clock());

    return 0;
}
