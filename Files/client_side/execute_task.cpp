/**
 * about debts in major:
 *  https://boinc.berkeley.edu/trac/wiki/ClientSchedOld
 *
 * @brief
 * - client_execute_tasks: only takes task from queue, executes it and updates debts
 * - client_update_simulate_finish_time, client_update_deadline_missed - the names are speaking
 * - schedule_job: suspends current task (project) and runs a new task from the args
 * - client_cpu_scheduling: calls schedule_job with task that can miss deadline, otherwise with greatest debt
 * - client_main_loop: simulates times of availability and unavailability
 *
 * @ref
 * It's different from a real policy.
 * According to wiki: https://github.com/BOINC/boinc/wiki/ClientSched#work-fetch-policy
 * and https://github.com/BOINC/boinc/wiki/JobPrioritization
 *
 * for instance, here we calculate debt, but in the documents - scheduling priority
 */
#include "execute_task.hpp"
#include <iostream>
#include <simgrid/s4u.hpp>
#include <math.h>
#include <inttypes.h>
#include <boost/random/linear_congruential.hpp>

#include "components/types.hpp"
#include "components/shared.hpp"
#include "tools/execution_state.hpp"
#include "rand.hpp"
#include "tools/thermometer.hpp"
#include <boost/intrusive/list.hpp>

#define MAX_SHORT_TERM_DEBT 86400

namespace xbt = simgrid::xbt;
namespace es = execution_state;

void client_update_debt(client_t client)
{
    double a, w, w_short;
    double total_debt_short = 0;
    double total_debt_long = 0;
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;
    a = 0;
    double sum_priority_run_proj = 0; /* sum of priority of runnable projects, used to calculate short_term debt */
    int num_project_short = 0;

    /* calcule a */
    for (auto &[key, proj] : projects)
    {
        a += proj->wall_cpu_time;
        if (!proj->tasks_swag.empty() || !proj->run_list.empty())
        {
            sum_priority_run_proj += proj->priority;
            num_project_short++;
        }
    }

    /* update short and long debt */
    for (auto &[key, proj] : projects)
    {
        w = a * proj->priority / client->sum_priority;
        w_short = a * proj->priority / sum_priority_run_proj;
        // printf("Project(%s) w=%lf a=%lf wall=%lf\n", proj->name, w, a, proj->wall_cpu_time);

        proj->short_debt += w_short - proj->wall_cpu_time;
        proj->long_debt += w - proj->wall_cpu_time;
        /* http://www.boinc-wiki.info/Short_term_debt#Short_term_debt
         * if no actions in project short debt = 0 */
        if (proj->tasks_swag.empty() && proj->run_list.empty())
            proj->short_debt = 0;
        total_debt_short += proj->short_debt;
        total_debt_long += proj->long_debt;
    }

    /* normalize short_term */
    for (auto &[key, proj] : projects)
    {
        //	proj->long_debt -= (total_debt_long / dict_size(projects));

        // printf("Project(%s), long term debt: %lf, short term debt: %lf\n", proj->name, proj->long_debt, proj->short_debt);
        /* reset wall_cpu_time */
        proj->wall_cpu_time = 0;

        if (proj->tasks_swag.empty() && proj->run_list.empty())
            continue;
        proj->short_debt -= (total_debt_short / num_project_short);
        if (proj->short_debt > MAX_SHORT_TERM_DEBT)
            proj->short_debt = MAX_SHORT_TERM_DEBT;
        if (proj->short_debt < -MAX_SHORT_TERM_DEBT)
            proj->short_debt = -MAX_SHORT_TERM_DEBT;
    }
}

/*****************************************************************************/
/* Update client short and long term debt.
This function is called every schedulingInterval and when an action finishes
The wall_cpu_time must be updated when this function is called */
void client_clean_short_debt(const client_t client)
{
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;

    /* calcule a */
    for (auto &[key, proj] : projects)
    {
        proj->short_debt = 0;
        proj->wall_cpu_time = 0;
    }
}

/*
 *	Free task
 */
void free_task(TaskT *task)
{
    task->is_freed = true;
    if (task->project->running_task == task)
    {
        task->running = 0;
        task->msg_task->cancel();
        task->project->running_task = NULL;
    }

    task->project->client->deadline_missed.erase(task);

    {
        std::unique_lock lock(*task->project->run_list_mutex);
        if (task->run_list_hookup.is_linked())
            xbt::intrusive_erase(task->project->run_list, *task);
    }

    if (task->sim_tasks_hookup.is_linked())
        xbt::intrusive_erase(task->project->sim_tasks, *task);

    {
        std::unique_lock lock(*task->project->tasks_swag_mutex);

        if (task->tasks_hookup.is_linked())
            xbt::intrusive_erase(task->project->tasks_swag, *task);
    }
    // delete task;
}

/*****************************************************************************/

int client_execute_tasks(ProjectInstanceOnClient *proj)
{
    TaskT *task;
    int32_t number;

    // printf("Running thread to execute tasks from project %s in %s  %g\n", proj->name, 			MSG_host_get_name(MSG_host_self()),  MSG_get_host_speed(MSG_host_self()));

    /* loop, execute tasks from queue until they finish or are cancelled by the main thread */
    while (sg4::Engine::get_clock() < maxtt)
    {
        std::unique_lock task_ready_lock(*proj->tasks_ready_mutex);
        while (proj->tasks_ready.empty())
        {
            proj->tasks_ready_cv_is_empty->wait(task_ready_lock);
        }

        task = proj->tasks_ready.front();
        proj->tasks_ready.pop();
        task_ready_lock.unlock();

        // printf("TERMINO POP %s EN %f\n", proj->client->name, sg4::Engine::get_clock());
        // printf("%s Executing task(%s)(%p)\n", proj->client->name, task->name, task);
        proj->client->work_fetch_cond->notify_all();
        task->running = 1;
        proj->running_task = task;
        /* task finishs its execution, free structures */

        // printf("----(1)-------Task(%s)(%s) from project(%s) start  duration = %g   power=  %g %d\n", task->name, task, proj->name,  MSG_task_get_compute_duration(task->msg_task), MSG_get_host_speed(MSG_host_self()), sg4::Engine::get_clock(), MSG_host_get_core_number(MSG_host_self()));

        // t0 = sg4::Engine::get_clock();

        auto msg_task = task->msg_task;

        es::Switcher::switch_to(proj->client->name, es::State::Busy, task->project->name);
        task->last_start_time = sg4::Engine::get_clock();

        msg_task->set_host(sg4::this_actor::get_host());
        msg_task->start();

        // if (err == MSG_OK)
        try
        {
            msg_task->wait();

            es::Switcher::switch_to(proj->client->name, es::State::Idle);
            task->time_spent_on_execution += sg4::Engine::get_clock() - task->last_start_time;
            g_measure_task_duration_per_project.at(proj->name)->add_to_series(task->time_spent_on_execution);

            number = (int32_t)atoi(task->name.c_str());
            // printf("s%d TERMINO EJECUCION DE %d en %f\n", proj->number, number, sg4::Engine::get_clock());
            proj->number_executed_task.push(task->result_number);
            proj->workunit_executed_task.push(task->workunit);
            proj->total_tasks_executed++;
            // printf("%f\n", proj->client->workunit_executed_task);
            // t1 = sg4::Engine::get_clock();

            task->running = 0;
            proj->wall_cpu_time += sg4::Engine::get_clock() - proj->client->last_wall;
            proj->client->last_wall = sg4::Engine::get_clock();
            client_update_debt(proj->client);
            client_clean_short_debt(proj->client);

            proj->running_task = NULL;
            free_task(task);
            delete task;

            proj->client->on = 1;
            proj->client->sched_cond->notify_all();
            continue;
        }
        catch (simgrid::CancelException &)
        {
        }

        es::Switcher::switch_to(proj->client->name, es::State::Idle);
        // task->running = 0;
        proj->running_task = NULL;
        // free_task(task);
        continue;
    }

    proj->thread = NULL;

    // printf("Finished execute_tasks %s in %f\n", proj->client->name, sg4::Engine::get_clock());

    return 0;
}

/*****************************************************************************/
/* verify whether the task will miss its deadline if it executes alone on cpu */
static int deadline_missed(TaskT *task)
{
    int64_t power; // (maximum 2⁶³-1)
    double remains;
    power = task->project->client->power;
    remains = task->msg_task->get_remaining() * task->project->client->factor;
    /* we're simulating only one cpu per host */
    if (sg4::Engine::get_clock() + (remains / power) > task->sent_time + task->deadline)
    {
        // printf("power: %ld\n", power);
        // printf("remains: %f\n", remains);
        return 1;
    }
    return 0;
}

/* simulate task scheduling and verify if it will miss its deadline */
static int task_deadline_missed_sim(client_t client, ProjectInstanceOnClient *proj, TaskT *task)
{
    return task->sim_finish > (task->sent_time + task->deadline - SharedDatabase::_group_info[client->group_number].scheduling_interval) * 0.9;
}

static void client_update_simulate_finish_time(client_t client)
{
    std::vector<std::unique_lock<simgrid::s4u::Mutex>> locks_inprojects;

    int total_tasks = 0;
    double clock_sim = sg4::Engine::get_clock();
    int64_t power = client->power;
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;

    for (auto &[key, proj] : projects)
    {
        locks_inprojects.push_back(std::unique_lock{*proj->run_list_mutex});
        locks_inprojects.push_back(std::unique_lock{*proj->tasks_swag_mutex});
        total_tasks += proj->tasks_swag.size() + proj->run_list.size();

        for (auto &task : proj->tasks_swag)
        {
            task.sim_remains = task.msg_task->get_remaining() * client->factor;
            proj->sim_tasks.push_back(task);
        }
        for (auto &task : proj->run_list)
        {
            task.sim_remains = task.msg_task->get_remaining() * client->factor;
            proj->sim_tasks.push_back(task);
        }
    }
    // printf("Total tasks %d\n", total_tasks);

    while (total_tasks)
    {
        double sum_priority = 0.0;
        TaskT *min_task = NULL;
        double min = 0.0;

        /* sum priority of projects with tasks to execute */
        for (auto &[key, proj] : projects)
        {
            if (!proj->sim_tasks.empty())
                sum_priority += proj->priority;
        }

        /* update sim_finish and find next action to finish */
        for (auto &[key, proj] : projects)
        {
            TaskT *task;

            for (auto &task : proj->sim_tasks)
            {
                task.sim_finish = clock_sim + (task.sim_remains / power) * (sum_priority / proj->priority) * proj->sim_tasks.size();
                if (min_task == NULL || min > task.sim_finish)
                {
                    min = task.sim_finish;
                    min_task = &task;
                }
            }
        }

        // printf("En %g  Task(%s)(%p):Project(%s) amount %lf remains %lf sim_finish %lf deadline %lf\n", sg4::Engine::get_clock(), min_task->name, min_task, min_task->project->name, min_task->duration, min_task->sim_remains, min_task->sim_finish, min_task->start + min_task->deadline);
        /* update remains of tasks */
        for (auto &[key, proj] : projects)
        {
            for (auto &task : proj->sim_tasks)
            {
                task.sim_remains -= (min - clock_sim) * power * (proj->priority / sum_priority) / proj->sim_tasks.size();
            }
        }
        /* remove action that has finished */
        total_tasks--;
        xbt::intrusive_erase(min_task->project->sim_tasks, *min_task);
        clock_sim = min;
    }
}

/* verify whether the actions in client's list will miss their deadline and put them in client->deadline_missed */
static void client_update_deadline_missed(client_t client)
{
    TaskT *task, task_next;
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;

    client_update_simulate_finish_time(client);

    for (auto &[key, proj] : projects)
    {
        std::unique_lock lock(*proj->tasks_swag_mutex);

        //  was x b t_swag_foreach_safe
        for (auto &task : proj->tasks_swag)
        {
            client->deadline_missed.erase(&task);

            if (task_deadline_missed_sim(client, proj, &task))
            {
                // printf("Client(%s), Project(%s), Possible Deadline Missed task(%s)(%p)\n", client->name, proj->name, MSG_task_get_name(task->msg_task), task);
                client->deadline_missed.insert(&task);
                // printf("((((((((((((((  HEAP PUSH      1   heap index %d\n", task->heap_index);
            }
        }
        std::unique_lock lock_(*proj->run_list_mutex);
        for (auto &task : proj->run_list)
        {
            client->deadline_missed.erase(&task);
            if (task_deadline_missed_sim(client, proj, &task))
            {
                // printf("Client(%s), Project(%s), Possible Deadline Missed task(%s)(%p)\n", client->name, proj->name, MSG_task_get_name(task->msg_task), task);
                client->deadline_missed.insert(&task);
                // printf("((((((((((((((  HEAP PUSH      2ii     heap index %d \n", task->heap_index);
            }
        }
    }
}

/*****************************************************************************/

/* void function, we don't need the enforcement policy since we don't simulate checkpointing and the deadlineMissed is updated at client_update_deadline_missed */
static void client_enforcement_policy()
{
    return;
}

static void schedule_job(client_t client, TaskT *job)
{

    /* task already running, just return */
    if (job->running)
    {
        if (client->running_project != job->project)
        {
            client->running_project->thread->suspend();
            es::Switcher::switch_to(client->name, es::State::Busy, job->project->name);
            job->project->thread->resume();

            client->running_project = job->project;
        }
        return;
    }

    /* schedule task */
    if (!job->scheduled)
    {
        {

            std::unique_lock lock(*job->project->tasks_ready_mutex);
            job->project->tasks_ready.push(job);
            job->project->tasks_ready_cv_is_empty->notify_all();
            job->scheduled = 1;
        }
    }
    /* if a task is running, cancel it and create new MSG_task */
    if (job->project->running_task != NULL)
    {
        TaskT *task_temp = job->project->running_task;
        task_temp->time_spent_on_execution += sg4::Engine::get_clock() - task_temp->last_start_time;

        double remains = task_temp->msg_task->get_remaining();
        task_temp->msg_task->cancel();
        task_temp->msg_task = sg4::Exec::init();
        task_temp->msg_task->set_name(task_temp->name);
        task_temp->msg_task->set_flops_amount(remains);

        task_temp->scheduled = 0;
        task_temp->running = 0;
    }
    /* stop running thread and start other */
    if (client->running_project)
    {
        client->running_project->thread->suspend();
    }

    es::Switcher::switch_to(client->name, es::State::Busy, job->project->name);

    job->project->thread->resume();

    client->running_project = job->project;
}
/*****************************************************************************/
/* this function is responsible to schedule the right task for the next SchedulingInterval.
     We're simulating only one cpu per host, so when this functions schedule a task it's enought for this turn
FIXME: if the task finish exactly at the same time of this function is called (i.e. remains = 0). We loose a schedulingInterval of processing time, cause we schedule it again */
static void client_cpu_scheduling(client_t client)
{
    ProjectInstanceOnClient *proj = nullptr, *great_debt_proj = nullptr;
    std::map<std::string, ProjectInstanceOnClient *> &projects = client->projects;
    double great_debt;

    /* schedule EDF task */
    /* We need to preemt the actions that may be executing on cpu, it is done by cancelling the action and creating a new one (with the remains amount updated) that will be scheduled later */
    while (!client->deadline_missed.empty())
    {
        TaskT *task = *client->deadline_missed.begin();
        client->deadline_missed.erase(client->deadline_missed.begin());
        // todo: this workaround shouldn't be there. Probably task is deleted before it is stopped using
        if (task->is_freed)
            continue;

        if (deadline_missed(task))
        {
            // printf("Task-1(%s)(%p) from project(%s) deadline, skip it\n", task->name, task, task->project->name);

            task->project->total_tasks_missed = task->project->total_tasks_missed + 1;

            client_update_debt(client);      // FELIX
            client_clean_short_debt(client); // FELIX
            if (task->msg_task)
            {
                task->msg_task->cancel();
            }
            free_task(task);

            continue;
        }

        // printf("Client (%s): Scheduling task(%s)(%p) of project(%s)\n", client->name, MSG_task_get_name(task->msg_task), task, task->project->name);
        //  update debt (anticipated). It isn't needed due to we only schedule one job per host.

        {
            std::unique_lock lock(*task->project->tasks_swag_mutex);

            if (task->tasks_hookup.is_linked())
                xbt::intrusive_erase(task->project->tasks_swag, *task);
        }
        if (!task->run_list_hookup.is_linked())
            task->project->run_list.push_back(*task);

        /* keep the task in deadline heap */
        client->deadline_missed.insert(task);
        schedule_job(client, task);
        return;
    }

    while (1)
    {
        great_debt_proj = NULL;
        TaskT *task = NULL;
        for (auto it = projects.begin(); it != projects.end(); ++it)
        {
            proj = it->second;
            if (((great_debt_proj == NULL) || (great_debt < proj->short_debt)) && (proj->run_list.size() || proj->tasks_swag.size()))
            {
                great_debt_proj = proj;
                great_debt = proj->short_debt;
            }
        }

        if (!great_debt_proj)
        {
            // printf(" scheduling: %g\n", sg4::Engine::get_clock());
            // xbt_cond_signal(proj->client->work_fetch_cond);   //FELIX
            proj->client->on = 1;
            proj->client->sched_cond->notify_all(); // FELIX
            return;
        }

        /* get task already running or first from tasks list */
        {
            std::unique_lock lock(*great_debt_proj->run_list_mutex);
            std::unique_lock lock_(*great_debt_proj->tasks_swag_mutex);

            if (!great_debt_proj->run_list.empty())
            {
                task = &(*great_debt_proj->run_list.begin());
            }
            else if (!great_debt_proj->tasks_swag.empty())
            {
                task = &(*great_debt_proj->tasks_swag.begin());
                great_debt_proj->tasks_swag.pop_front();
                great_debt_proj->run_list.push_front(*task);
            }
        }
        if (task)
        {
            if (deadline_missed(task))
            {
                // printf(">>>>>>>>>>>> Task-2(%s)(%p) from project(%s) deadline, skip it\n", task->name, task, task->project->name);
                ++(task->project->total_tasks_missed);

                task->msg_task->cancel();
                free_task(task);
                continue;
            }
            // printf("Client (%s): Scheduling task(%s)(%p) of project(%s)\n", client->name, MSG_task_get_name(task->msg_task), task, task->project->name);

            schedule_job(client, task);
        }
        client_enforcement_policy();
        return;
    }
}

enum class ClientState
{
    Available,
    Unavailable
};

double get_available_period_span(client_t client, boost::rand48 &rndg)
{
    double random = (ran_distri(SharedDatabase::_group_info[client->group_number].av_distri, SharedDatabase::_group_info[client->group_number].aa_param, SharedDatabase::_group_info[client->group_number].ab_param, rndg) * 3600.0);
    if (ceil(random + sg4::Engine::get_clock()) >= maxtt)
    {
        random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
    }
    return random;
}

std::pair<int, int> client_main_loop(client_t client)
{
    int64_t power = client->power;
    double next_non_avail_start = 0, random = 0, last_avail_start = 0;
    ClientState cl_state = ClientState::Available;
    double sum_available_time = 0, sum_unavailable_time = 0;
    auto &rndg = *g_rndg_for_client_avail;

    next_non_avail_start = sg4::Engine::get_clock() + get_available_period_span(client, rndg);
    while (ceil(sg4::Engine::get_clock()) < maxtt)
    {
        // Simulate the period of availability
        while (cl_state == ClientState::Available)
        {
            /* increase wall_cpu_time to the project running task */
            if (client->running_project && client->running_project->running_task)
            {
                client->running_project->wall_cpu_time += sg4::Engine::get_clock() - client->last_wall;
                client->last_wall = sg4::Engine::get_clock();
            }

            client_update_debt(client);
            client_update_deadline_missed(client);
            client_cpu_scheduling(client);

            if (client->on)
            {
                client->work_fetch_cond->notify_all();
            }

            // sleep till the end of the simulation or the next non-availability period
            try
            {
                double time_wait = std::min(
                    std::min(maxtt, next_non_avail_start) - sg4::Engine::get_clock(),
                    SharedDatabase::_group_info[client->group_number].scheduling_interval);
                if (time_wait < 0)
                    time_wait = 0;
                std::unique_lock lock(*client->sched_mutex);
                client->sched_cond->wait_for(lock, time_wait);
            }
            catch (std::exception &e)
            {
                std::cout << "exception at the line " << __LINE__ << ' ' << e.what() << std::endl;
            }
            if (sg4::Engine::get_clock() >= maxtt)
            {
                sum_available_time += sg4::Engine::get_clock() - last_avail_start;
                break;
            }
            if (sg4::Engine::get_clock() >= next_non_avail_start - DOUBLE_EPS)
            {
                sum_available_time += sg4::Engine::get_clock() - last_avail_start;
                cl_state = ClientState::Unavailable;
            }
        }

        /*************** SIMULATE CLIENT DOWN TIME ****/

        if (cl_state == ClientState::Unavailable)
        {
            random = (ran_distri(SharedDatabase::_group_info[client->group_number].nv_distri, SharedDatabase::_group_info[client->group_number].na_param, SharedDatabase::_group_info[client->group_number].nb_param, rndg) * 3600.0);

            g_measure_non_availability_duration->add_to_series(random);

            if (ceil(random + sg4::Engine::get_clock()) > maxtt)
            {
                random = std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            }

            sum_unavailable_time += random;

            if (client->running_project)
            {
                client->running_project->thread->suspend();
                auto &running_task = client->running_project->running_task;
                if (running_task != nullptr)
                {
                    running_task->time_spent_on_execution += sg4::Engine::get_clock() - running_task->last_start_time;
                    running_task->msg_task->suspend();
                }
            }
            es::Switcher::switch_to(client->name, es::State::Unavailable);

            client->ask_for_work_mutex->lock();
            client->suspended = random;
            client->work_fetch_cond->notify_all();
            client->ask_for_work_mutex->unlock();

            // printf(" Cliente %s sleep %e\n", client->name, sg4::Engine::get_clock());

            sg4::this_actor::sleep_for(random);

            if (client->running_project)
            {
                es::Switcher::switch_to(client->name, es::State::Busy, client->running_project->name);
                auto &running_task = client->running_project->running_task;
                if (running_task != nullptr)
                {
                    running_task->last_start_time = sg4::Engine::get_clock();
                    running_task->msg_task->resume();
                }
                client->running_project->thread->resume();
            }
            else
            {
                es::Switcher::switch_to(client->name, es::State::Idle);
            }

            // prepare for the availability period
            cl_state = ClientState::Available;
            last_avail_start = sg4::Engine::get_clock();
            next_non_avail_start = sg4::Engine::get_clock() + get_available_period_span(client, rndg);
        }

        /*************** END OF SIMULATION OF DOWNTIME ****/
    }
    es::Switcher::switch_to(client->name, es::State::Unavailable);
    return {sum_available_time, sum_unavailable_time};
}