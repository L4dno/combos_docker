
/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

/* BOINC architecture simulator */

#include <iostream>
#include <algorithm>
#include <atomic>
#include <string>
#include <xbt/utility.hpp>
#include <set>
#include <queue>
#include <stdio.h>
#include <locale.h> // Big numbers nice output
#include <math.h>
#include <inttypes.h>
#include <simgrid/cond.h>
#include <simgrid/engine.h>
#include <simgrid/s4u.hpp>
#include <boost/intrusive/list.hpp>
#include "thread_safe_queue.hpp"

namespace sg4 = simgrid::s4u;
namespace intrusive = boost::intrusive;
namespace xbt = simgrid::xbt;

/* Create a log channel to have nice outputs. */
#include "xbt/asserts.h"
#include "rand.h"

XBT_LOG_NEW_DEFAULT_CATEGORY(boinc_simulator, "Messages specific for this boinc simulator");

#define WARM_UP_TIME 20			// Warm up time in hours
#define MAX_SHORT_TERM_DEBT 86400
#define MAX_TIMEOUT_SERVER 86400 * 365 // One year without client activity, only to finish simulation for a while
#define MAX_SIMULATED_TIME 100		// Simulation time in hours
#define WORK_FETCH_PERIOD 60           // Work fetch period
#define KB 1024                        // 1 KB in bytes
#define PRECISION 0.00001              // Accuracy (used in client_work_fetch())
#define CREDITS_CPU_S 0.002315         // Credits per second (1 GFLOP machine)
#define NUMBER_PROJECTS 1		// Number of projects
#define NUMBER_SCHEDULING_SERVERS 1	// Number of scheduling servers
#define NUMBER_DATA_SERVERS 1		// Number of data servers
#define NUMBER_DATA_CLIENT_SERVERS 1	// Number of data client servers
#define NUMBER_CLIENT_GROUPS 1         // Number of client groups
#define NUMBER_CLIENTS 1000		// Number of clients
#define NUMBER_DATA_CLIENTS 100		// Number of data clients
#define NUMBER_ORDINARY_CLIENTS (NUMBER_CLIENTS - NUMBER_DATA_CLIENTS)
#define REQUEST_SIZE 10 * KB // Request size
#define REPLY_SIZE 10 * KB   // Reply size
#define MAX_BUFFER 300000    // Max buffer

/* Project back end */
int init_database(int argc, char *argv[]);
int work_generator(int argc, char *argv[]);
int validator(int argc, char *argv[]);
int assimilator(int argc, char *argv[]);

/* Scheduling server */
int scheduling_server_requests(int argc, char *argv[]);
int scheduling_server_dispatcher(int argc, char *argv[]);

/* Data server */
int data_server_requests(int argc, char *argv[]);
int data_server_dispatcher(int argc, char *argv[]);

/* Data client server */
int data_client_server_requests(int argc, char *argv[]);
int data_client_server_dispatcher(int argc, char *argv[]);

/* Client */
// int client_execute_tasks(project_t proj);
int client(int argc, char *argv[]);

/* Test all */
int test_all(const char *platform_file, const char *application_file);

/* Types */
typedef enum
{
    ERROR,
    IN_PROGRESS,
    VALID
} workunit_status; // Workunit status
typedef enum
{
    REQUEST,
    REPLY,
    TERMINATION,
    NO
} message_type; // Message type
typedef enum
{
    FAIL,
    SUCCESS
} result_status; // Result status
typedef enum
{
    CORRECT,
    INCORRECT
} result_value; // Result value
// todo: smart ptrs against leaks
typedef struct ssmessage s_ssmessage_t, *ssmessage_t;             // Message to scheduling server
typedef struct request s_request_t, *request_t;                   // Client request to scheduling server
typedef struct reply s_reply_t, *reply_t;                         // Client reply to scheduling server
typedef struct result s_result_t, *result_t;                      // Result
typedef struct dsmessage s_dsmessage_t, *dsmessage_t;             // Message to data server
typedef struct dcsrequest s_dcsrequest_t, *dcsrequest_t;          // Data client request to data client server
typedef struct dcsreply s_dcsreply_t, *dcsreply_t;                // Message to data server
typedef struct dcsmessage s_dcsmessage_t, *dcsmessage_t;          // Message to data client server
typedef struct dcmessage s_dcmessage_t, *dcmessage_t;             // Message to data server
typedef struct dcworkunit s_dcworkunit_t, *dcworkunit_t;          // Data client workunit
typedef struct workunit s_workunit_t, *workunit_t;                // Workunit
typedef struct task s_task_t, *task_t;                            // Task
typedef struct project s_project_t, *project_t;                   // Project
typedef struct client s_client_t, *client_t;                      // Client
typedef struct project_database s_pdatabase_t, *pdatabase_t;      // Project database
typedef struct scheduling_server s_sserver_t, *sserver_t;         // Scheduling server
typedef struct data_server s_dserver_t, *dserver_t;               // Data server
typedef struct data_client_server s_dcserver_t, *dcserver_t;      // Data client server
typedef struct data_client s_dclient_t, *dclient_t;               // Data client
typedef struct client_group s_group_t, *group_t;                  // Client group
typedef struct ask_for_files s_ask_for_files_t, *ask_for_files_t; // Ask for files params

typedef enum
{
    SReplyT,
    SRequestT
} ssmessage_content;

/* Message to scheduling server */
struct ssmessage
{
    message_type type; // REQUEST, REPLY, TERMINATION
    void *content;     // Content (request or reply)
    ssmessage_content datatype;
};

/* Client request to scheduling server */
struct request
{
    std::string answer_mailbox; // Answer mailbox
    int32_t group_power;        // Client group power
    int64_t power;              // Client power
    double percentage;          // Percentage of project
};

/* Client reply to scheduling server */
struct reply
{
    result_status status;  // Result status
    result_value value;    // Result value
    std::string workunit;  // Workunit
    int32_t result_number; // Result number
    int32_t credits;       // Credits requested
};

/* Result (workunit instance) */
struct result
{
    workunit_t workunit;                  // Associated workunit
    int32_t ninput_files;                 // Number of input files
    std::vector<std::string> input_files; // Input files names (URLs)
    int32_t number_tasks;                 // Number of tasks (usually one)
    std::vector<task_t> tasks;            // Tasks
};

/* Message to data server */
struct dsmessage
{
    message_type type;          // REQUEST, REPLY, TERMINATION
    char proj_number;           // Project number
    std::string answer_mailbox; // Answer mailbox
};

/* Data client request to data client server */
struct dcsrequest
{
    std::string answer_mailbox; // Answer mailbox
};

/* Confirmation message to data client server */
struct dcsreply
{
    std::string dclient_name;                    // Data client name
    std::map<std::string, workunit_t> workunits; // Workunits
};

typedef enum
{
    SDcsrequestT,
    SDcsreplyT
} dcsmessage_content;

/* Message to data client server */
struct dcsmessage
{
    message_type type; // REQUEST, REPLY, TERMINATION
    void *content;     // Content (dcs_request, dcs_reply)
    dcsmessage_content datatype;
};

/* Reply to data client */
struct dcmessage
{
    std::string answer_mailbox;                  // Answer mailbox
    int32_t nworkunits;                          // Number of workunits
    std::map<std::string, workunit_t> workunits; // Workunits
};

/* Workunit */
struct workunit
{
    std::string number;                                 // Workunit number
    workunit_status status;                             // ERROR, IN_PROGRESS, VALID
    char ndata_clients;                                 // Number of times this workunit has been sent to data clients
    char ndata_clients_confirmed;                       // Number of times this workunit has been confirmed from data clients
    char ntotal_results;                                // Number of created results
    char nsent_results;                                 // Number of sent results
    char nresults_received;                             // Number of received results
    char nvalid_results;                                // Number of valid results
    char nsuccess_results;                              // Number of success results
    char nerror_results;                                // Number of erroneous results
    char ncurrent_error_results;                        // Number of current error results
    int32_t credits;                                    // Number of credits per valid result
    std::vector<double> times;                          // Time when each result was sent
    int32_t ninput_files;                               // Number of input files
    std::vector<std::string> input_files;               // Input files names (URLs)
    std::atomic_int number_past_through_assimilator{0}; // all schedule service, validator and assimilator use workunit,
                                                        // but a condition when we can delete it doesn't consider this -
                                                        // only that data services don't need it anymore
};

/* Task */
struct task
{
    std::string workunit; // Workunit of the task
    std::string name;     // Task name
    char scheduled;       // Task scheduled (it is in tasks_ready list) [0,1]
    char running;         // Task is actually running on cpu [0,1]
    intrusive::list_member_hook<> tasks_hookup;
    intrusive::list_member_hook<> run_list_hookup;
    intrusive::list_member_hook<> sim_tasks_hookup;
    sg4::ExecPtr msg_task;
    project_t project;  // Project reference
    int64_t heap_index; // (maximum 2⁶³-1)
    int64_t deadline;   // Task deadline (maximum 2⁶³-1)
    double duration;    // Task duration in flops
    double start;
    double sim_finish; // Simulated finish time of task
    double sim_remains;
    // double comp_cost; // Task flops to execute. Can be less than duration if it was halt.
};

using TaskSwagT = intrusive::list<task, intrusive::member_hook<task, intrusive::list_member_hook<>,
                                                               &task::tasks_hookup>>;
using SimTaskSwagT = intrusive::list<task, intrusive::member_hook<task, intrusive::list_member_hook<>,
                                                                  &task::sim_tasks_hookup>>;
using RunListSwagT = intrusive::list<task, intrusive::member_hook<task, intrusive::list_member_hook<>,
                                                                  &task::run_list_hookup>>;

/* Client project */
struct project
{

    /* General data of project */

    std::string name;           // Project name
    std::string answer_mailbox; // Client answer mailbox
    char priority;              // Priority (maximum 255)
    char number;                // Project number (maximum 255)
    char on;                    // Project is working

    /* Data to control tasks and their execution */

    task_t running_task;    // which is the task that is running on thread */
    sg4::ActorPtr thread;   // thread running the tasks of this project */
    client_t client;        // Client pointer
    TaskSwagT tasks_swag;   // nearly runnable jobs of project, insert at tail to keep ordered
    SimTaskSwagT sim_tasks; // nearly runnable jobs of project, insert at tail to keep ordered
    RunListSwagT run_list;  // list of jobs running, normally only one tasks, but if a deadline may occur it can put another here
    sg4::MutexPtr tasks_ready_mutex;
    sg4::ConditionVariablePtr tasks_ready_cv_is_empty;
    std::queue<task_t> tasks_ready;        // synchro queue, thread to execute task */
    TSQueue<int32_t> number_executed_task; // Queue with executed task numbers

    TSQueue<std::string> workunit_executed_task; // Cola size tareas ejecutadas

    /* Statistics data */

    int32_t total_tasks_checked;  // (maximum 2³¹-1)
    int32_t total_tasks_executed; // (maximum 2³¹-1)
    int32_t total_tasks_received; // (maximum 2³¹-1)
    int32_t total_tasks_missed;   // (maximum 2³¹-1)

    /* Data to simulate boinc scheduling behavior */

    double anticipated_debt;
    double short_debt;
    double long_debt;
    double wall_cpu_time; // cpu time used by project during most recent period (SchedulingInterval or action finish)
    double shortfall;
};

struct TaskCmp
{
    bool operator()(const task_t a, const task_t b) const
    {
        return a->start + a->deadline > b->start + b->deadline;
    }
};

/* Client */
struct client
{
    std::string name;
    std::map<std::string, project_t> projects; // all projects of client
    // todo: here I need operator < to be correct
    std::set<task_t, TaskCmp> deadline_missed;
    project_t running_project;
    sg4::ActorPtr work_fetch_thread;
    sg4::ConditionVariablePtr sched_cond;
    sg4::MutexPtr sched_mutex;
    sg4::ConditionVariablePtr work_fetch_cond;
    sg4::MutexPtr work_fetch_mutex;
    sg4::MutexPtr mutex_init;
    sg4::ConditionVariablePtr cond_init;
    sg4::MutexPtr ask_for_work_mutex;
    sg4::ConditionVariablePtr ask_for_work_cond;
    char n_projects;      // Number of projects attached
    char finished;        // Number of finished threads (maximum 255)
    char no_actions;      // No actions [0,1]
    char on;              // Client will know who sent the signal
    char initialized;     // Client initialized or not [0,1]
    int32_t group_number; // Group_number
    int64_t power;        // Host power
    double sum_priority;  // sum of projects' priority
    double total_shortfall;
    double last_wall; // last time where the wall_cpu_time was updated
    double factor;    // host power factor
    double suspended; // Client is suspended (>0) or not (=0)
};

/* Project database */
struct project_database
{

    /* Project attributes */

    char project_number;                          // Project number
    char nscheduling_servers;                     // Number of scheduling servers
    char ndata_servers;                           // Number of data servers
    char ndata_client_servers;                    // Number of data client servers
    std::vector<std::string> scheduling_servers;  // Scheduling servers names
    std::vector<std::string> data_servers;        // Data servers names
    std::vector<std::string> data_client_servers; // Data client servers names
    std::vector<std::string> data_clients;        // Data clients names
    std::string project_name;                     // Project name
    int32_t nclients;                             // Number of clients
    int32_t nordinary_clients;                    // Number of ordinary clients
    int32_t ndata_clients;                        // Number of data clients
    int32_t nfinished_oclients;                   // Number of finished ordinary clients
    int32_t nfinished_dclients;                   // Number of finished data clients
    int64_t disk_bw;                              // Disk bandwidth of data servers

    /* Redundancy and scheduling attributes */

    int32_t min_quorum;          // Minimum number of successful results required for the validator. If a scrict majority agree, they are considered correct
    int32_t target_nresults;     // Number of results to create initially per workunit
    int32_t max_error_results;   // If the number of client error results exeed this, the workunit is declared to have an error
    int32_t max_total_results;   // If the number of results for this workunit exeeds this, the workunit is declared to be in error
    int32_t max_success_results; // If the number of success results for this workunit exeeds this, and a consensus has not been reached, the workunit is declared to be in error
    int64_t delay_bound;         // The time by which a result must be completed (deadline)

    /* Results attributes */

    char ifgl_percentage;      // Percentage of input files generated locally
    char ifcd_percentage;      // Number of workunits that share the same input files
    char averagewpif;          // Average workunits per input files
    char success_percentage;   // Percentage of success results
    char canonical_percentage; // Percentage of success results that make up a consensus
    int64_t input_file_size;   // Size of the input files needed for a result associated with a workunit of this project
    int64_t output_file_size;  // Size of the output files needed for a result associated with a workunit of this project
    int64_t job_duration;      // Job length in FLOPS

    /* Result statistics */

    int64_t nmessages_received; // Number of messages received (requests+replies)
    int64_t nwork_requests;     // Number of requests received
    int64_t nresults;           // Number of created results
    int64_t nresults_sent;      // Number of sent results
    int64_t nvalid_results;     // Number of valid results
    int64_t nresults_received;  // Number of results returned from the clients
    int64_t nresults_analyzed;  // Number of analyzed results
    int64_t nsuccess_results;   // Number of success results
    int64_t nerror_results;     // Number of error results
    int64_t ndelay_results;     // Number of results delayed

    /* Workunit statistics */

    int64_t total_credit;               // Total credit granted
    int64_t nworkunits;                 // Number of workunits created
    int64_t nvalid_workunits;           // Number of workunits validated
    int64_t nerror_workunits;           // Number of erroneous workunits
    int64_t ncurrent_deleted_workunits; // Number of current deleted workunits

    /* Work generator */

    int64_t ncurrent_results;                            // Number of results currently in the system
    int64_t ncurrent_error_results;                      // Number of error results currently in the system
    int64_t ncurrent_workunits;                          // Number of current workunits
    std::map<std::string, workunit_t> current_workunits; // Current workunits
    std::queue<result_t> current_results;                // Current results
    std::queue<workunit_t> current_error_results;        // Current error results
    sg4::MutexPtr w_mutex;                               // Workunits mutex
    sg4::MutexPtr r_mutex;                               // Results mutex
    sg4::MutexPtr er_mutex;                              // Error results mutex
    sg4::ConditionVariablePtr wg_empty;                  // Work generator CV empty
    sg4::ConditionVariablePtr wg_full;                   // Work generator CV full
    int wg_dcs;                                          // Work generator (data client server)
    int wg_end;                                          // Work generator end

    /* Validator */

    int64_t ncurrent_validations;            // Number of results currently in the system
    std::queue<reply_t> current_validations; // Current results
    sg4::MutexPtr v_mutex;                   // Results mutex
    sg4::ConditionVariablePtr v_empty;       // Validator CV empty
    int v_end;                               // Validator end

    /* Assimilator */

    int64_t ncurrent_assimilations;                // Number of workunits waiting for assimilation
    std::queue<std::string> current_assimilations; // Current workunits waiting for asimilation
    sg4::MutexPtr a_mutex;                         // Assimilator mutex
    sg4::ConditionVariablePtr a_empty;             // Assimilator CV empty
    int a_end;                                     // Assimilator end

    /* Download logs */

    sg4::MutexPtr rfiles_mutex;     // Input file requests mutex ordinary clients
    sg4::MutexPtr dcrfiles_mutex;   // Input file requests mutex data clients
    sg4::MutexPtr dsuploads_mutex;  // Output file uploads mutex to data servers
    sg4::MutexPtr dcuploads_mutex;  // Output file uploads mutex to data clients
    std::vector<int64_t> rfiles;    // Input file requests ordinary clients
    std::vector<int64_t> dcrfiles;  // Input file requests data clients
    int64_t dsuploads;              // Output file uploads to data servers
    std::vector<int64_t> dcuploads; // Output file uploads to data clients

    /* File deleter */
    int64_t ncurrent_deletions;            // Number of workunits that should be deleted
    TSQueue<workunit_t> current_deletions; // Workunits that should be deleted

    /* Files */

    int32_t output_file_storage; // Output file storage [0-> data servers, 1->data clients]
    int32_t dsreplication;       // Files replication in data servers
    int32_t dcreplication;       // Files replication in data clients

    int64_t ninput_files; // Number of input files currently in the system
    // std::qu e ue<std::string> input_files; // Current input files
    // sg4::MutexPtr i_mutex;             // Input files mutex
    // sg4::ConditionVariablePtr i_empty; // Input files CV empty
    // sg4::ConditionVariablePtr i_full;  // Input files CV full

    // int64_t noutput_files; // Number of output files currently in the system
    // std::que ue<int64_t> output_files;  // Current output files
    // sg4::MutexPtr o_mutex;             // Output file mutex
    // sg4::ConditionVariablePtr o_empty; // Onput files CV empty
    // sg4::ConditionVariablePtr o_full;  // Onput files CV full

    /* Synchronization */

    sg4::MutexPtr ssrmutex;            // Scheduling server request mutex
    sg4::MutexPtr ssdmutex;            // Scheduling server dispatcher mutex
    sg4::MutexPtr dcmutex;             // Data client mutex
    sg4::BarrierPtr barrier;           // Wait until database is initialized
    char nfinished_scheduling_servers; // Number of finished scheduling servers
};

/* Scheduling server */
struct scheduling_server
{
    char project_number;                     // Project number
    std::string server_name;                 // Server name
    sg4::MutexPtr mutex;                     // Mutex
    sg4::ConditionVariablePtr cond;          // VC
    std::queue<ssmessage_t> client_requests; // Requests queue
    int32_t EmptyQueue;                      // Queue end [0,1]
    int64_t Nqueue;                          // Queue size
    double time_busy;                        // Time the server is busy
};

/* Data server */
struct data_server
{
    char project_number;                     // Project number
    std::string server_name;                 // Server name
    sg4::MutexPtr mutex;                     // Mutex
    sg4::ConditionVariablePtr cond;          // VC
    std::queue<dsmessage_t> client_requests; // Requests queue
    int32_t EmptyQueue;                      // Queue end [0,1]
    int64_t Nqueue;                          // Queue size
    double time_busy;                        // Time the server is busy
};

/* Data client server */
struct data_client_server
{
    int32_t group_number;                     // Group number
    std::string server_name;                  // Host name
    sg4::MutexPtr mutex;                      // Mutex
    sg4::ConditionVariablePtr cond;           // VC
    std::queue<dcsmessage_t> client_requests; // Requests queue
    int32_t EmptyQueue;                       // Queue end [0,1]
    int64_t Nqueue;                           // Queue size
    double time_busy;                         // Time the server is busy
};

/* Data client */
struct data_client
{
    std::atomic_char working = 0;            // Data client is working -> 1
    char nprojects;                          // Number of attached projects
    std::string server_name;                 // Server name
    sg4::ActorPtr ask_for_files_thread;      // Ask for files thread
    sg4::MutexPtr mutex;                     // Mutex
    sg4::MutexPtr ask_for_files_mutex;       // Ask for files mutex
    sg4::ConditionVariablePtr cond;          // CV
    std::queue<dsmessage_t> client_requests; // Requests queue
    int32_t total_storage;                   // Storage in MB
    int32_t finish;                          // Storage amount consumed in MB
    int32_t EmptyQueue;                      // Queue end [0,1]
    int64_t disk_bw;                         // Disk bandwidth
    int64_t Nqueue;                          // Queue size
    double time_busy;                        // Time the server is busy
    double navailable;                       // Time the server is available
    double sum_priority;                     // Sum of project priorities
};

/* Client group */
struct client_group
{
    char on;                        // 0 -> Empty, 1-> proj_args length
    char sp_distri;                 // Speed distribution
    char db_distri;                 // Disk speed distribution
    char av_distri;                 // Availability distribution
    char nv_distri;                 // Non-availability distribution
    sg4::MutexPtr mutex;            // Mutex
    sg4::ConditionVariablePtr cond; // Cond
    char **proj_args;               // Arguments
    int32_t group_power;            // Group power
    int32_t n_clients;              // Number of clients of the group
    int32_t n_ordinary_clients;     // Number of ordinary clients of the group
    int32_t n_data_clients;         // Number of data clients of the group
    int64_t total_power;            // Total power
    double total_available;         // Total time clients available
    double total_notavailable;      // Total time clients not available
    double connection_interval;
    double scheduling_interval;
    double sa_param;  // Speed A parameter
    double sb_param;  // Speed B parameter
    double da_param;  // Disk speed A parameter
    double db_param;  // Disk speed B parameter
    double aa_param;  // Availability A parameter
    double ab_param;  // Availability B parameter
    double na_param;  // Non availability A parameter
    double nb_param;  // Non availability B parameter
    double max_power; // Maximum host power
    double min_power; // Minimum host power
};

/* Data client ask for files */
struct ask_for_files
{
    char project_number;    // Project number
    char project_priority;  // Project priority
    std::string mailbox;    // Process mailbox
    group_t group_info;     // Group information
    dclient_t dclient_info; // Data client information
};

/* Simulation time */
const double maxtt = (MAX_SIMULATED_TIME + WARM_UP_TIME) * 3600; // Total simulation time in seconds
const double maxst = (MAX_SIMULATED_TIME) * 3600;                // Simulation time in seconds
const double maxwt = (WARM_UP_TIME) * 3600;                      // Warm up time in seconds

/* Server info */
pdatabase_t _pdatabase;    // Projects databases
sserver_t _sserver_info;   // Scheduling servers information
dserver_t _dserver_info;   // Data servers information
dcserver_t _dcserver_info; // Data client servers information
dclient_t _dclient_info;   // Data clients information
group_t _group_info;       // Client groups information

/* Synchronization */
sg4::MutexPtr _oclient_mutex; // Ordinary client mutex
sg4::MutexPtr _dclient_mutex; // Data client mutex

/* Availability statistics */
int64_t _total_power;       // Total clients power (maximum 2⁶³-1)
double _total_available;    // Total time clients available
double _total_notavailable; // Total time clients notavailable

/*
 *	Parse memory usage
 */
int parseLine(char *line)
{
    int i = strlen(line);
    while (*line < '0' || *line > '9')
        line++;
    line[i - 3] = '\0';
    i = atoi(line);
    return i;
}

/*
 *	Memory usage in KB
 */
int memoryUsage()
{
    FILE *file = fopen("/proc/self/status", "r");

    if (file == NULL)
        exit(1);

    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL)
    {
        if (strncmp(line, "VmRSS:", 6) == 0)
        {
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    return result;
}

/*
 *	Free task
 */
static void free_task(task_t task)
{
    if (task->project->running_task == task)
    {
        task->running = 0;
        task->msg_task->cancel();
        task->project->running_task = NULL;
    }

    task->project->client->deadline_missed.erase(task);

    if (task->run_list_hookup.is_linked())
        xbt::intrusive_erase(task->project->run_list, *task);

    if (task->sim_tasks_hookup.is_linked())
        xbt::intrusive_erase(task->project->sim_tasks, *task);

    if (task->tasks_hookup.is_linked())
        xbt::intrusive_erase(task->project->tasks_swag, *task);

    delete task;
}

static void free_project(project_t proj)
{

    auto clean_queue = []<typename T>(T &q)
    {
        while (!q.empty())
            q.pop();
    };
    clean_queue(proj->tasks_ready);
    clean_queue(proj->number_executed_task);
    clean_queue(proj->workunit_executed_task);

    proj->tasks_swag.clear();
    proj->sim_tasks.clear();
    proj->run_list.clear();
}

/*
 *	Disk access simulation
 */
void disk_access(int32_t server_number, int64_t size)
{
    pdatabase_t database = &_pdatabase[server_number]; // Server info

    // Calculate sleep time

    double sleep = std::min((double)maxtt - sg4::Engine::get_clock() - PRECISION, (double)size / database->disk_bw);
    if (sleep < 0)
        sleep = 0;

    // Sleep
    sg4::this_actor::sleep_for(sleep);
}

/*
 *	Disk access data clients simulation
 *  the name is killing me
 */
void disk_access2(int64_t size)
{
    // Calculate sleep time
    double sleep = std::min((double)maxtt - sg4::Engine::get_clock() - PRECISION, (double)size / 80000000);
    if (sleep < 0)
        sleep = 0;

    // Sleep
    sg4::this_actor::sleep_for(sleep);
}

/*
 *	Process has done i out of n rounds,
 *	and we want a bar of width w and resolution r.
 */
static inline void loadBar(int x, int n, int r, int w)
{
    // Only update r times.
    if (x % (n / r + 1) != 0)
        return;

    // Calculuate the ratio of complete-to-incomplete.
    float ratio = x / (float)n;
    int c = ratio * w;

    // Show the percentage complete.
    printf("Progress: %3d%% [", (int)(ratio * 100));

    // Show the load bar.
    for (x = 0; x < c; x++)
        printf("=");

    for (x = c; x < w; x++)
        printf(" ");

    // ANSI Control codes to go back to the
    // previous line and clear it.
    printf("]\n\033[F\033[J");
}

/*
 *	 Server compute simulation. Wait till the end of a executing task
 */
void compute_server(int flops)
{
    sg4::this_actor::execute(flops);
}

/*
 *	Print server results
 */
void print_results(int, char **)
{
    int memory = 0; // memory usage
    int memoryAux;  // memory aux
    double sleep;   // Sleep time

    // Init variables
    int k = 0, l = 0, m = 0;
    sleep = maxtt / 100.0; // 1 hour

    // Print progress
    for (int progress = 0; ceil(sg4::Engine::get_clock()) < maxtt;)
    {
        progress = (int)round(sg4::Engine::get_clock() / maxtt * 100) + 1;
        loadBar((int)round(progress), 100, 200, 50);
        memoryAux = memoryUsage();
        if (memoryAux > memory)
            memory = memoryAux;
        sg4::this_actor::sleep_for(sleep); // Sleep while simulation
    }

    setlocale(LC_NUMERIC, "en_US.UTF-8");

    printf("\n Memory usage: %'d KB\n", memory);

    printf("\n Total number of clients: %'d\n", NUMBER_CLIENTS);
    printf(" Total number of ordinary clients: %'d\n", NUMBER_CLIENTS - NUMBER_DATA_CLIENTS);
    printf(" Total number of data clients: %'d\n\n", NUMBER_DATA_CLIENTS);

    // Iterate servers information
    for (int i = 0; i < NUMBER_PROJECTS; i++)
    {
        pdatabase_t database = &_pdatabase[i]; // Server info pointer

        // Print results
        printf("\n ####################  %s  ####################\n", database->project_name.c_str());
        printf("\n Simulation ends in %'g h (%'g sec)\n\n", sg4::Engine::get_clock() / 3600.0 - WARM_UP_TIME, sg4::Engine::get_clock() - maxwt);

        double ocload = 0, dcload = 0;
        for (int j = 0; j < database->dsreplication + database->dcreplication; j++)
        {
            printf("  OC. Number of downloads from data server %" PRId64 ": %" PRId64 "\n", j, database->rfiles[j]);
            if (j >= database->dcreplication)
                ocload += database->rfiles[j];
        }

        printf("\n");

        for (int j = 0; j < database->dsreplication + database->dcreplication; j++)
        {
            printf("  DC. Number of downloads from data server %" PRId64 ": %" PRId64 "\n", j, database->dcrfiles[j]);
            if (j >= database->dcreplication)
                dcload += database->dcrfiles[j];
        }

        printf("OC: %f, DC: %f\n", ocload, dcload);

        printf("\n");
        for (int j = 0; j < (int64_t)database->nscheduling_servers; j++, l++)
            printf("  Scheduling server %" PRId64 ":\tBusy: %0.1f%%\n", j, _sserver_info[l].time_busy / maxst * 100);
        for (int j = 0; j < (int64_t)database->ndata_servers; j++, k++)
            printf("  Data server %" PRId64 ":\tBusy: %0.1f%% (OC: %0.1f%%, DC: %0.1f%%)\n", j, _dserver_info[k].time_busy / maxst * 100, (ocload * database->input_file_size + database->dsuploads * database->output_file_size) / ((ocload + dcload) * database->input_file_size + database->dsuploads * database->output_file_size) * 100 * (_dserver_info[k].time_busy / maxst), (dcload * database->input_file_size) / ((ocload + dcload) * database->input_file_size + database->dsuploads * database->output_file_size) * 100 * (_dserver_info[k].time_busy / maxst));
        printf("\n  Number of clients: %'d\n", database->nclients);
        printf("  Number of ordinary clients: %'d\n", database->nordinary_clients);
        printf("  Number of data clients: %'d\n\n", database->ndata_clients);

        double time_busy = 0;
        int64_t storage = 0;
        double tnavailable = 0;
        for (int j = 0; j < (int64_t)database->ndata_clients; j++, m++)
        {
            time_busy += (_dclient_info[m].time_busy);
            storage += (int64_t)(_dclient_info[m]).total_storage;
            tnavailable += _dclient_info[m].navailable;
        }

        // printf("time busy : %f\n", time_busy);
        time_busy = time_busy / database->ndata_clients / maxst * 100;
        storage /= (double)database->ndata_clients;

        printf("\n  Data clients average load: %0.1f%%\n", time_busy);
        printf("  Data clients average storage: %'" PRId64 " MB\n", storage);
        printf("  Data clients availability: %0.1f%%\n\n", (maxst - (tnavailable / database->ndata_clients)) / maxtt * 100);

        printf("\n  Messages received: \t\t%'" PRId64 " (work requests received + results received)\n", database->nmessages_received);
        printf("  Work requests received: \t%'" PRId64 "\n", database->nwork_requests);
        printf("  Results created: \t\t%'" PRId64 " (%0.1f%%)\n", database->nresults, (double)database->nresults / database->nwork_requests * 100);
        printf("  Results sent: \t\t%'" PRId64 " (%0.1f%%)\n", database->nresults_sent, (double)database->nresults_sent / database->nresults * 100);
        printf("  Results received: \t\t%'" PRId64 " (%0.1f%%)\n", database->nresults_received, (double)database->nresults_received / database->nresults * 100);
        printf("  Results analyzed: \t\t%'" PRId64 " (%0.1f%%)\n", database->nresults_analyzed, (double)database->nresults_analyzed / database->nresults_received * 100);
        printf("  Results success: \t\t%'" PRId64 " (%0.1f%%)\n", database->nsuccess_results, (double)database->nsuccess_results / database->nresults_analyzed * 100);
        printf("  Results failed: \t\t%'" PRId64 " (%0.1f%%)\n", database->nerror_results, (double)database->nerror_results / database->nresults_analyzed * 100);
        printf("  Results too late: \t\t%'" PRId64 " (%0.1f%%)\n", database->ndelay_results, (double)database->ndelay_results / database->nresults_analyzed * 100);
        printf("  Results valid: \t\t%'" PRId64 " (%0.1f%%)\n", database->nvalid_results, (double)database->nvalid_results / database->nresults_analyzed * 100);
        printf("  Workunits total: \t\t%'" PRId64 "\n", database->nworkunits);
        printf("  Workunits completed: \t\t%'" PRId64 " (%0.1f%%)\n", database->nvalid_workunits + database->nerror_workunits, (double)(database->nvalid_workunits + database->nerror_workunits) / database->nworkunits * 100);
        printf("  Workunits not completed: \t%'" PRId64 " (%0.1f%%)\n", (database->nworkunits - database->nvalid_workunits - database->nerror_workunits), (double)(database->nworkunits - database->nvalid_workunits - database->nerror_workunits) / database->nworkunits * 100);
        printf("  Workunits valid: \t\t%'" PRId64 " (%0.1f%%)\n", database->nvalid_workunits, (double)database->nvalid_workunits / database->nworkunits * 100);
        printf("  Workunits error: \t\t%'" PRId64 " (%0.1f%%)\n", database->nerror_workunits, (double)database->nerror_workunits / database->nworkunits * 100);
        printf("  Throughput: \t\t\t%'0.1f mens/s\n", (double)database->nmessages_received / maxst);
        printf("  Credit granted: \t\t%'" PRId64 " credits\n", (long int)database->total_credit);
        printf("  FLOPS average: \t\t%'" PRId64 " GFLOPS\n\n", (int64_t)((double)database->nvalid_results * (double)database->job_duration / maxst / 1000000000.0));
    }

    fflush(stdout);
}

/*
 * to free memory, we delete all completed asynchronous communications. It shouldn't affect the clocks in the simmulator.
 */
void delete_completed_communications(std::vector<sg4::CommPtr> &pending_comms)
{
    do
    {
        ssize_t ready_comm_ind = sg4::Comm::test_any(pending_comms);
        if (ready_comm_ind == -1)
        {
            break;
        }
        swap(pending_comms.back(), pending_comms[ready_comm_ind]);
        pending_comms.pop_back();
    } while (!pending_comms.empty());
}

/*
 *	Init database
 */
int init_database(int argc, char *argv[])
{
    int i, project_number;
    pdatabase_t database;

    if (argc != 22)
    {
        printf("Invalid number of parameter in init_database()\n");
        return 0;
    }

    project_number = atoi(argv[1]);
    database = &_pdatabase[project_number];

    // Init database
    database->project_number = project_number;               // Project number
    database->project_name = std::string(argv[2]);           // Project name
    database->output_file_size = (int64_t)atoll(argv[3]);    // Answer size
    database->job_duration = (int64_t)atoll(argv[4]);        // Workunit duration
    database->ifgl_percentage = (char)atoi(argv[5]);         // Percentage of input files generated locally
    database->ifcd_percentage = (char)atoi(argv[6]);         // Number of workunits that share the same input files
    database->averagewpif = (char)atoi(argv[7]);             // Average workunits per input files
    database->min_quorum = (int32_t)atoi(argv[8]);           // Quorum
    database->target_nresults = (int32_t)atoi(argv[9]);      // target_nresults
    database->max_error_results = (int32_t)atoi(argv[10]);   // max_error_results
    database->max_total_results = (int32_t)atoi(argv[11]);   // Maximum number of times a task must be sent
    database->max_success_results = (int32_t)atoi(argv[12]); // max_success_results
    database->delay_bound = (int64_t)atoll(argv[13]);        // Workunit deadline
    database->success_percentage = (char)atoi(argv[14]);     // Success results percentage
    database->canonical_percentage = (char)atoi(argv[15]);   // Canonical results percentage
    database->input_file_size = (int64_t)atoll(argv[16]);    // Input file size
    database->disk_bw = (int64_t)atoll(argv[17]);            // Disk bandwidth
    database->ndata_servers = (char)atoi(argv[18]);          // Number of data servers
    database->output_file_storage = (int32_t)atoi(argv[19]); // Output file storage [0 -> data servers, 1 -> data clients]
    database->dsreplication = (int32_t)atoi(argv[20]);       // File replication in data servers
    database->dcreplication = (int32_t)atoi(argv[21]);       // File replication in data clients
    database->nmessages_received = 0;                        // Store number of messages rec.
    database->nresults = 0;                                  // Number of results created
    database->nresults_sent = 0;                             // Number of results sent
    database->nwork_requests = 0;                            // Store number of requests rec.
    database->nvalid_results = 0;                            // Number of valid results (with a consensus)
    database->nresults_received = 0;                         // Number of results received (replies)
    database->nresults_analyzed = 0;                         // Number of results analyzed
    database->nsuccess_results = 0;                          // Number of success results
    database->nerror_results = 0;                            // Number of erroneous results
    database->ndelay_results = 0;                            // Number of delayed results
    database->total_credit = 0;                              // Total credit granted
    database->nworkunits = 0;                                // Number of workunits created
    database->nvalid_workunits = 0;                          // Number of valid workunits
    database->nerror_workunits = 0;                          // Number of erroneous workunits
    database->ncurrent_deleted_workunits = 0;                // Number of current deleted workunits
    database->nfinished_scheduling_servers = 0;              // Number of finished scheduling servers

    // File input file requests
    database->dsuploads = 0;
    database->rfiles.resize(database->dsreplication + database->dcreplication);
    database->dcrfiles.resize(database->dsreplication + database->dcreplication);
    for (i = 0; i < database->dsreplication + database->dcreplication; i++)
    {
        database->rfiles[i] = 0;
        database->dcrfiles[i] = 0;
    }

    // Fill with data server names
    database->data_servers.reserve(database->ndata_servers);
    for (i = 0; i < database->ndata_servers; i++)
    {
        auto Mal = bprintf("d%" PRId32 "%" PRId32, project_number + 1, i);
        database->data_servers.push_back(Mal);
    }

    database->barrier->wait();

    return 0;
}

/*
 *	Generate workunit
 */
workunit_t generate_workunit(pdatabase_t database)
{
    workunit_t workunit = new s_workunit_t();
    workunit->number = std::string(bprintf("%" PRId64, database->nworkunits));
    workunit->status = IN_PROGRESS;
    workunit->ndata_clients = 0;
    workunit->ndata_clients_confirmed = 0;
    workunit->ntotal_results = 0;
    workunit->nsent_results = 0;
    workunit->nresults_received = 0;
    workunit->nvalid_results = 0;
    workunit->nsuccess_results = 0;
    workunit->nerror_results = 0;
    workunit->ncurrent_error_results = 0;
    workunit->credits = -1;
    workunit->times.reserve(database->max_total_results);
    workunit->ninput_files = database->dcreplication + database->dsreplication;
    workunit->input_files.resize(workunit->ninput_files);
    database->ncurrent_workunits++;

    int i;
    for (i = 0; i < database->dcreplication; i++)
        workunit->input_files[i] = "";
    for (; i < workunit->ninput_files; i++)
        workunit->input_files[i] = database->data_servers[uniform_int(0, database->ndata_servers - 1)];

    database->nworkunits++;

    return workunit;
}

/*
 *	Generate result
 */
result_t generate_result(pdatabase_t database, workunit_t workunit, int X)
{

    result_t result = new s_result_t();
    result->workunit = workunit;
    result->ninput_files = workunit->ninput_files;
    result->input_files = workunit->input_files;
    database->ncurrent_results++;
    database->nresults++;

    // workunit->times[(int)workunit->ntotal_results++] = sg4::Engine::get_clock();
    workunit->times.push_back(sg4::Engine::get_clock());
    workunit->ntotal_results++;

    if (X == 1)
        workunit->ncurrent_error_results--;

    // todo: у нас кто-нибудь ждет на wh_empty?...
    if (database->ncurrent_results >= 1)
        database->wg_empty->notify_all();

    return result;
}

/*
 *	Blank result
 */
result_t blank_result()
{
    result_t result = new s_result_t();
    result->workunit = NULL;  // Associated workunit
    result->ninput_files = 0; // Number of input files
    // result->input_files.clear(); // Input files names (URLs)
    result->number_tasks = 0; // Number of tasks (usually one)
    // result->tasks;            // Tasks
    return result;
}

/*
 *	Work generator
 */
int work_generator(int argc, char *argv[])
{

    if (argc != 2)
    {
        printf("Invalid number of parameter in work_generator()\n");
        return 0;
    }

    int project_number = atoi(argv[1]);
    pdatabase_t database = &_pdatabase[project_number];

    // Wait until the database is initiated

    database->barrier->wait();

    while (!database->wg_end)
    {

        // this is strange - double lock. probably meant w_mutex
        std::unique_lock lock(*(database->w_mutex));

        while (database->ncurrent_workunits >= MAX_BUFFER && !database->wg_end && !database->wg_dcs)
            database->wg_full->wait(lock);

        if (database->wg_end)
        {
            lock.unlock();
            break;
        }

        // // BORRAR
        // double t0, t1;
        // t0 = sg4::Engine::get_clock();

        database->wg_dcs = 0;
        workunit_t workunit = NULL;

        // Check if there are error results
        database->er_mutex->lock();

        // // BORRAR
        // t1 = sg4::Engine::get_clock();
        // if (t1 - t0 > 1)
        //     printf("%f: WF1 -> %f s\n", sg4::Engine::get_clock(), t1 - t0);

        // Regenerate result when error result
        if (database->ncurrent_error_results > 0)
        {
            while (database->ncurrent_error_results > 0)
            {
                // Get workunit associated with the error result
                workunit = database->current_error_results.front();
                database->current_error_results.pop();
                database->ncurrent_error_results--;
                database->er_mutex->unlock();

                // Generate new instance from the workunit
                database->r_mutex->lock();
                result_t result = generate_result(database, workunit, 1);
                database->current_results.push(result);
                database->r_mutex->unlock();

                database->er_mutex->lock();
            }
        }
        // Create new workunit
        else
        {
            // Generate workunit
            workunit_t workunit = generate_workunit(database);
            database->current_workunits[workunit->number] = workunit;
            // todo: don't I need to notify someone here...
        }

        database->er_mutex->unlock();
        lock.unlock();

        // // BORRAR
        // t1 = sg4::Engine::get_clock();
        // if (t1 - t0 > 1)
        //     printf("%f: WF3 -> %f s\n", sg4::Engine::get_clock(), t1 - t0);
    }

    return 0;
}

/*
 *	Validator
 */
int validator(int argc, char *argv[])
{
    workunit_t workunit;
    reply_t reply = NULL;

    if (argc != 2)
    {
        printf("Invalid number of parameter in validator()\n");
        return 0;
    }

    int project_number = atoi(argv[1]);
    pdatabase_t database = &_pdatabase[project_number];

    // Wait until the database is initiated
    database->barrier->wait();

    while (!database->v_end)
    {

        std::unique_lock lock(*database->v_mutex);

        while (database->ncurrent_validations == 0 && !database->v_end)
            database->v_empty->wait(lock);

        if (database->v_end)
        {
            break;
        }

        // Get received result
        reply = database->current_validations.front();
        database->current_validations.pop();
        database->ncurrent_validations--;
        lock.unlock();

        // Get asociated workunit
        workunit = database->current_workunits.at(reply->workunit);
        workunit->nresults_received++;

        // Delay result
        if (workunit->times.size() <= reply->result_number)
        {
            std::cout << "UB: " << workunit->times.size() << ' ' << reply->result_number << std::endl;
        }
        if (sg4::Engine::get_clock() - workunit->times[reply->result_number] >= database->delay_bound)
        {
            reply->status = FAIL;
            workunit->nerror_results++;
            database->ndelay_results++;
        }
        // Success result
        else if (reply->status == SUCCESS)
        {
            workunit->nsuccess_results++;
            database->nsuccess_results++;
            if (reply->value == CORRECT)
            {
                workunit->nvalid_results++;
                if (workunit->credits == -1)
                    workunit->credits = reply->credits;
                else
                    workunit->credits = workunit->credits > reply->credits ? reply->credits : workunit->credits;
            }
        }
        // Error result
        else
        {
            workunit->nerror_results++;
            database->nerror_results++;
        }
        database->nresults_analyzed++;

        // Check workunit
        database->er_mutex->lock();
        if (workunit->status == IN_PROGRESS)
        {
            if (workunit->nvalid_results >= database->min_quorum)
            {
                database->w_mutex->lock();
                workunit->status = VALID;
                database->w_mutex->unlock();
                database->nvalid_results += (int64_t)(workunit->nvalid_results);
                database->total_credit += (int64_t)(workunit->credits * workunit->nvalid_results);
            }
            else if (workunit->ntotal_results >= database->max_total_results ||
                     workunit->nerror_results >= database->max_error_results ||
                     workunit->nsuccess_results >= database->max_success_results)
            {
                database->w_mutex->lock();
                workunit->status = ERROR;
                database->w_mutex->unlock();
            }
        }
        else if (workunit->status == VALID && reply->status == SUCCESS && reply->value == CORRECT)
        {
            database->nvalid_results++;
            database->total_credit += (int64_t)(workunit->credits);
        }

        // If result is an error and task is not completed, call work generator in order to create a new instance
        if (reply->status == FAIL)
        {
            if (workunit->status == IN_PROGRESS &&
                workunit->nsuccess_results < database->max_success_results &&
                workunit->nerror_results < database->max_error_results &&
                workunit->ntotal_results < database->max_total_results)
            {
                database->current_error_results.push(workunit);
                database->ncurrent_error_results++;
                workunit->ncurrent_error_results++;
            }
        }

        // Call asimilator if workunit has been completed
        if ((workunit->status != IN_PROGRESS) &&
            (workunit->nresults_received == workunit->ntotal_results) &&
            (workunit->ncurrent_error_results == 0))
        {
            database->a_mutex->lock();
            database->current_assimilations.push(workunit->number);
            database->ncurrent_assimilations++;
            database->a_empty->notify_all();
            database->a_mutex->unlock();
        }
        database->er_mutex->unlock();

        delete reply;
        reply = NULL;
    }

    return 0;
}

/*
 *	File deleter
 */
int file_deleter(pdatabase_t database, std::string workunit_number)
{
    int64_t current_deletions;

    // Check if workunit can be deleted

    auto can_delete_condition = [](workunit_t workunit)
    {
        return workunit->ndata_clients == workunit->ndata_clients_confirmed &&
               workunit->ndata_clients_confirmed == workunit->number_past_through_assimilator.load();
    };

    workunit_t workunit = database->current_workunits.at(workunit_number);
    if (can_delete_condition(workunit))
    {
        // The workunit is ready to be deleted
        database->current_workunits.erase(workunit_number);
        database->dcmutex->lock();
        database->ncurrent_deleted_workunits++;
        database->dcmutex->unlock();
    }
    else
    {
        // The workunit should not be deleted yet, so push it in the deletions queue
        database->ncurrent_deletions++;
        database->current_deletions.push(workunit);
    }

    // Check deletions queue
    workunit = NULL;
    current_deletions = database->ncurrent_deletions;
    for (int64_t i = 0; i < current_deletions; i++)
    {
        workunit = database->current_deletions.pop();
        if (can_delete_condition(workunit))
        {
            // The workunit is ready to be deleted
            database->current_workunits.erase(workunit->number);
            database->dcmutex->lock();
            database->ncurrent_deleted_workunits++;
            database->dcmutex->unlock();
            database->ncurrent_deletions--;
        }
        else
        {
            // The workunit should not be deleted yet, so push it again in the queue
            database->current_deletions.push(workunit);
        }
    }

    return 0;
}

/*
 *	Assimilator
 */
int assimilator(int argc, char *argv[])
{

    if (argc != 2)
    {
        printf("Invalid number of parameter in assimilator()\n");
        return 0;
    }

    int project_number = atoi(argv[1]);
    pdatabase_t database = &_pdatabase[project_number];

    // Wait until the database is initiated
    database->barrier->wait();

    while (!database->a_end)
    {

        std::unique_lock lock(*database->a_mutex);

        while (database->ncurrent_assimilations == 0 && !database->a_end)
            database->a_empty->wait(lock);

        if (database->a_end)
        {
            break;
        }

        // Get workunit number to assimilate
        std::string workunit_number = database->current_assimilations.front();
        database->current_assimilations.pop();
        database->ncurrent_assimilations--;
        lock.unlock();

        // Get completed workunit
        workunit_t workunit = database->current_workunits.at(workunit_number);

        // Update workunit stats
        if (workunit->status == VALID)
            database->nvalid_workunits++;
        else
            database->nerror_workunits++;

        workunit->number_past_through_assimilator.fetch_add(1);

        // Delete completed workunit from database
        file_deleter(database, workunit->number);
    }

    return 0;
}

/*
 *	Select result from database
 */
result_t select_result(int project_number, request_t req)
{

    pdatabase_t database = &_pdatabase[project_number];

    // Get result
    result_t result = database->current_results.front();
    database->current_results.pop();

    // Signal work generator if number of current results is 0
    database->ncurrent_results--;
    if (database->ncurrent_results == 0)
        database->wg_full->notify_all();

    // Calculate number of tasks
    result->number_tasks = (int32_t)floor(req->percentage / ((double)database->job_duration / req->power));
    if (result->number_tasks == 0)
        result->number_tasks = (int32_t)1;

    // Create tasks
    result->tasks.resize((int)result->number_tasks);

    // Fill tasks
    for (int i = 0; i < result->number_tasks; i++)
    {
        task_t task = new s_task_t();
        task->workunit = result->workunit->number;
        task->name = std::string(bprintf("%" PRId32, result->workunit->nsent_results++));
        task->duration = database->job_duration * ((double)req->group_power / req->power);
        task->deadline = database->delay_bound;
        task->start = sg4::Engine::get_clock();
        task->heap_index = -1;
        result->tasks[i] = task;
    }

    database->ssdmutex->lock();
    database->nresults_sent++;
    database->ssdmutex->unlock();

    return result;
}

/*
 *	Scheduling server requests function
 */
int scheduling_server_requests(int argc, char *argv[])
{
    ssmessage_t msg = NULL;        // Client message
    pdatabase_t database = NULL;   // Database
    sserver_t sserver_info = NULL; // Scheduling server info

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameter in scheduling_server_requests()\n");
        return 0;
    }

    // Init boinc server
    int32_t project_number = (int32_t)atoi(argv[1]);           // Project number
    int32_t scheduling_server_number = (int32_t)atoi(argv[2]); // Scheduling server number

    database = &_pdatabase[project_number];                  // Database
    sserver_info = &_sserver_info[scheduling_server_number]; // Scheduling server info

    sserver_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name

    // Wait until database is ready
    database->barrier->wait();

    /*
        Set asynchronous mailbox mode in order to receive requests in spite of
        the fact that the server is not calling MSG_task_receive()
    */
    // MSG_mailbox_set_async(sserver_info->server_name);

    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(sserver_info->server_name);
    while (1)
    {
        // Receive message
        msg = mailbox->get<ssmessage>();

        // Termination message
        if (msg->type == TERMINATION)
        {
            delete msg;
            break;
        }
        // Client answer with execution results
        else if (msg->type == REPLY)
        {
            database->ssrmutex->lock();
            database->nmessages_received++;
            database->nresults_received++;
            database->ssrmutex->unlock();
        }
        // Client work request
        else
        {
            database->ssrmutex->lock();
            database->nmessages_received++;
            database->nwork_requests++;
            database->ssrmutex->unlock();
        }

        // Insert request into queue
        sserver_info->mutex->lock();
        sserver_info->Nqueue++;
        sserver_info->client_requests.push(msg);

        // If queue is not empty, wake up dispatcher process
        if (sserver_info->Nqueue > 0)
            sserver_info->cond->notify_all();
        sserver_info->mutex->unlock();

        // Free
        msg = NULL;
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
    ssmessage_t msg = NULL;            // Client request
    dsmessage_t work = NULL;           // Termination message
    result_t result = NULL;            // Data server answer
    simgrid::s4u::CommPtr comm = NULL; // Asynchronous communication
    pdatabase_t database = NULL;       // Server info
    sserver_t sserver_info = NULL;     // Scheduling server info
    int32_t i, project_number;         // Index, project number
    int32_t scheduling_server_number;  // Scheduling_server_number
    double t0, t1;                     // Time measure

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

    database = &_pdatabase[project_number];                  // Server info
    sserver_info = &_sserver_info[scheduling_server_number]; // Scheduling server info

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
        msg = sserver_info->client_requests.front();
        sserver_info->client_requests.pop();
        sserver_info->Nqueue--;
        lock.unlock();

        // Check if message is an answer with the computation results
        if (msg->type == REPLY)
        {
            database->v_mutex->lock();

            // Call validator
            database->current_validations.push(reinterpret_cast<reply_t>(msg->content));
            database->ncurrent_validations++;

            database->v_empty->notify_all();
            database->v_mutex->unlock();
        }
        // Message is an address request
        else
        {
            // Consumer
            database->r_mutex->lock();

            if (database->ncurrent_results == 0)
            {
                // NO WORKUNITS
                result = blank_result();
            }
            else
            {
                // CONSUME
                result = select_result(project_number, (request_t)msg->content);
            }

            database->r_mutex->unlock();

            // Create the task

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

            // // BORRAR
            // t1 = sg4::Engine::get_clock();
            // if (t1 - t0 > 1)
            //     printf("%f: 3 -> %f s\n", sg4::Engine::get_clock(), t1 - t0);
        }

        // Iteration end time
        t1 = sg4::Engine::get_clock();

        // Accumulate total time server is busy
        if (t0 < maxtt)
            sserver_info->time_busy += (t1 - t0);

        // Free
        delete msg;
        msg = NULL;
        result = NULL;
    }

    // Wait until all scheduling servers finish
    database->ssdmutex->lock();
    database->nfinished_scheduling_servers++;
    database->ssdmutex->unlock();

    // Check if it is the last scheduling server
    if (database->nfinished_scheduling_servers == database->nscheduling_servers)
    {
        // Send termination message to data servers
        for (i = 0; i < database->ndata_servers; i++)
        {
            // Create termination message
            work = new s_dsmessage_t();

            // Group power = -1 indicates it is a termination message
            work->type = TERMINATION;

            // Send message
            sg4::Mailbox::by_name(database->data_servers[i])->put(work, KB);
        }
        // Free
        database->data_servers.clear();

        // Finish project back-end
        database->wg_end = 1;
        database->v_end = 1;
        database->a_end = 1;
        database->wg_full->notify_all();
        database->v_empty->notify_all();
        database->a_empty->notify_all();
    }

    return 0;
}

/*
 *	Data server requests function
 */
int data_server_requests(int argc, char *argv[])
{
    dserver_t dserver_info = NULL;
    dsmessage_t req = NULL;
    int32_t server_number;

    // Check number of arguments
    if (argc != 2)
    {
        printf("Invalid number of parameter in data_server_requests()\n");
        return 0;
    }

    // Init parameters
    server_number = (int32_t)atoi(argv[1]);                              // Data server number
    dserver_info = &_dserver_info[server_number];                        // Data server info pointer
    dserver_info->server_name = sg4::this_actor::get_host()->get_name(); // Data server name

    // Set asynchronous receiving in mailbox
    // MSG_mailbox_set_async(dserver_info->server_name);
    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(dserver_info->server_name);

    while (1)
    {
        // Receive message
        req = mailbox->get<dsmessage>();

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
    pdatabase_t database = NULL;
    dserver_t dserver_info = NULL;
    dsmessage_t req = NULL;
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

    dserver_info = &_dserver_info[server_number]; // Data server info pointer
    database = &_pdatabase[project_number];       // Boinc server info pointer

    std::vector<sg4::CommPtr> _dscomm; // Asynchro communications storage (data server with client)

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
        req = dserver_info->client_requests.front();
        dserver_info->client_requests.pop();
        dserver_info->Nqueue--;
        lock.unlock();

        // Reply with output file
        if (req->type == REPLY)
        {
            disk_access(project_number, database->output_file_size);
            database->dsuploads_mutex->lock();
            database->dsuploads++;
            database->dsuploads_mutex->unlock();
        }
        // Input file request
        else
        {
            // Read tasks from disk
            disk_access(project_number, database->input_file_size);

            // Answer the client
            comm = sg4::Mailbox::by_name(req->answer_mailbox)->put_async(new int(1), database->input_file_size);

            // Store the asynchronous communication created in the dictionary

            delete_completed_communications(_dscomm);
            _dscomm.push_back(comm);
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

    return 0;
}

/* ########## DATA CLIENT SERVER ########## */

/*
 *	Data client server requests function
 */
int data_client_server_requests(int argc, char *argv[])
{
    dcsmessage_t msg = NULL;                           // Data client message
    pdatabase_t database = NULL;                       // Project database
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

    database = &_pdatabase[project_number];                     // Database
    dcserver_info = &_dcserver_info[data_client_server_number]; // Data client server info

    dcserver_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name

    // Wait until database is ready
    database->barrier->wait();

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

// what purpose do you serve, dear friend...
/*
 *	Data client server dispatcher function
 */
int data_client_server_dispatcher(int argc, char *argv[])
{
    dcsmessage_t msg = NULL;                           // Data client message
    dcmessage_t ans_msg = NULL;                        // Answer to data client
    pdatabase_t database = NULL;                       // Project database
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

    database = &_pdatabase[project_number];                     // Database
    dcserver_info = &_dcserver_info[data_client_server_number]; // Data client server info

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

                // Generate target_nresults instances when workunit is confirmed for the first time
                if (workunit->ndata_clients_confirmed == database->dcreplication)
                {
                    for (int i = 0; i < database->target_nresults; i++)
                    {
                        result_t result = generate_result(database, workunit, 0);
                        {
                            std::unique_lock lock(*database->r_mutex);
                            database->current_results.push(result);
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

            database->w_mutex->lock();
            for (auto &[key, workunit] : database->current_workunits)
            {
                if (ans_msg->nworkunits < database->averagewpif)
                {
                    if (workunit->status == IN_PROGRESS &&
                        workunit->ndata_clients < database->dcreplication &&
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
                database->wg_dcs = 1;
                database->wg_full->notify_all();
            }
            database->w_mutex->unlock();

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

/* ########## DATA CLIENT ########## */

/*
 *	Data client ask for input files
 */
int data_client_ask_for_files(ask_for_files_t params)
{
    std::string server_name;
    // msg_error_t error;
    simgrid::s4u::CommPtr comm = NULL; // Asynchronous communication
    // double backoff = 300;

    // Request to data client server
    dcsmessage_t dcsrequest = NULL; // Message to data client server

    // Reply from data client server
    dcmessage_t dcreply = NULL;

    // Request to data server
    dsmessage_t dsinput_file_request = NULL;

    // Reply from data server
    int *dsinput_file_reply_task = nullptr;

    // Reply to data client server
    dcsmessage_t dcsreply = NULL;

    pdatabase_t database = NULL; // Database
    // group_t group_info = NULL;			// Group information
    dclient_t dclient_info = NULL;         // Data client information
    workunit_t workunit = NULL;            // Workunit
    double storage = 0, max_storage = 0;   // File storage in MB
    char project_number, project_priority; // Project number and priority
    int i;

    // params = MSG_process_get_data(MSG_process_self());
    project_number = params->project_number;
    project_priority = params->project_priority;
    // todo: doesn't we need it? So?
    // group_info = params->group_info;
    dclient_info = params->dclient_info;
    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(params->mailbox);

    max_storage = storage = (project_priority / dclient_info->sum_priority) * dclient_info->total_storage * KB * KB;

    database = &_pdatabase[(int)project_number]; // Database

    // Reduce input file storage if output files are uploaded to data clients
    if (database->output_file_size == 1)
    {
        max_storage /= 2.0;
        storage = max_storage;
    }

    database->dcmutex->lock();
    for (i = 0; i < database->ndata_clients; i++)
    {
        if (database->data_clients[i].empty())
        {
            database->data_clients[i] = dclient_info->server_name;
            break;
        }
    }
    database->dcmutex->unlock();

    // printf("Storage: %f\n", max_storage);

    while (1)
    {
        dclient_info->ask_for_files_mutex->lock();
        if (dclient_info->finish)
        {
            dclient_info->ask_for_files_mutex->unlock();
            break;
        }
        dclient_info->ask_for_files_mutex->unlock();

        // Delete local files when there are completed workunits
        while (storage < max_storage)
        {
            database->dcmutex->lock();
            if (database->ncurrent_deleted_workunits >= database->averagewpif)
            {
                database->ncurrent_deleted_workunits -= database->averagewpif;
                storage += database->input_file_size;
            }
            else
            {
                database->dcmutex->unlock();
                break;
            }
            database->dcmutex->unlock();
        }

        if (storage >= 0)
        {
            // backoff = 300;

            // ASK FOR WORKUNITS -> DATA CLIENT SERVER
            dcsrequest = new s_dcsmessage_t();
            dcsrequest->type = REQUEST;
            dcsrequest->content = new s_dcsrequest_t();
            dcsrequest->datatype = dcsmessage_content::SDcsrequestT;
            ((dcsrequest_t)dcsrequest->content)->answer_mailbox = mailbox->get_name();

            auto where = database->data_client_servers[uniform_int(0, database->ndata_client_servers - 1)];

            sg4::Mailbox::by_name(where)->put(dcsrequest, KB);

            dcreply = mailbox->get<dcmessage>();

            if (dcreply->nworkunits > 0)
            {
                // ASK FOR INPUT FILES -> DATA SERVERS
                for (auto &[key, workunit] : dcreply->workunits)
                {
                    if (workunit->status != IN_PROGRESS)
                        continue;

                    // Download input files (or generate them locally)
                    if (uniform_int(0, 99) < (int)database->ifgl_percentage)
                    {
                        // Download only if the workunit was not downloaded previously
                        if (uniform_int(0, 99) < (int)database->ifcd_percentage)
                        {
                            for (i = 0; i < workunit->ninput_files; i++)
                            {
                                if (workunit->input_files[i].empty())
                                    continue;

                                server_name = workunit->input_files[i];

                                // BORRAR (esta mal, no generico)
                                if (i < database->dcreplication)
                                {
                                    int server_number = atoi(server_name.c_str() + 2) - NUMBER_ORDINARY_CLIENTS;
                                    // printf("resto: %d, server_name: %s, server_number: %d\n", NUMBER_ORDINARY_CLIENTS, server_name.c_str(), server_number);
                                    // printf("%d\n", _dclient_info[server_number].working);
                                    if (_dclient_info[server_number].working.load() == 0)
                                        continue;
                                }

                                dsinput_file_request = new s_dsmessage_t();
                                dsinput_file_request->type = REQUEST;
                                dsinput_file_request->answer_mailbox = mailbox->get_name();

                                server_name = workunit->input_files[i];

                                sg4::Mailbox::by_name(server_name)->put(dsinput_file_request, KB);

                                // error = MSG_task_receive_with_timeout(&dsinput_file_reply_task, mailbox, backoff);	// Send input file reply
                                // MSG_task_receive(, mailbox);
                                // todo: seems that I don't need it? Like, below I have comm.wait, right?... I'm so tired and
                                // or not - here we get reply and later wait for comm just to destroy it?

                                dsinput_file_reply_task = mailbox->get<int>();

                                // Timeout reached -> exponential backoff 2^N
                                /*if(error == MSG_TIMEOUT){
                                    backoff*=2;
                                    //free(dsinput_file_request);
                                    continue;
                                }*/

                                // Log request
                                database->rfiles_mutex->lock();
                                database->dcrfiles[i]++;
                                database->rfiles_mutex->unlock();

                                storage -= database->input_file_size;
                                delete dsinput_file_reply_task;
                                dsinput_file_reply_task = NULL;
                                break;
                            }
                        }
                    }
                    break;
                }

                // CONFIRMATION MESSAGE TO DATA CLIENT SERVER
                dcsreply = new s_dcsmessage_t();
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
        data_client_ask_for_files_evil_label:

            delete (dcreply);
            dcsrequest = NULL;
            dcreply = NULL;
        }
        // Sleep if
        if (sg4::Engine::get_clock() >= maxwt || storage <= 0)
            sg4::this_actor::sleep_for(60);
    }

    // Finish data client servers execution
    _dclient_mutex->lock();
    database->nfinished_dclients++;
    if (database->nfinished_dclients == database->ndata_clients)
    {
        for (i = 0; i < database->ndata_client_servers; i++)
        {
            dcsrequest = new s_dcsmessage_t();
            dcsrequest->type = TERMINATION;

            sg4::Mailbox::by_name(database->data_client_servers[i])->put(dcsrequest, REQUEST_SIZE);
        }
    }
    _dclient_mutex->unlock();
    // mailbox.~Mailbox();
    return 0;
}

/*
 *	Data client requests function
 */
int data_client_requests(int argc, char *argv[])
{
    dsmessage_t msg = NULL;                      // Client message
    group_t group_info = NULL;                   // Group information
    dclient_t dclient_info = NULL;               // Data client information
    ask_for_files_t ask_for_files_params = NULL; // Ask for files params
    int32_t data_client_number, group_number;    // Data client number, group number
    int count = 0;                               // Index, termination count

    // Availability params
    double time = 0, random;

    // Check number of arguments
    if (argc != 3)
    {
        printf("Invalid number of parameters in data_client_requests\n");
        return 0;
    }

    // Init data client
    group_number = (int32_t)atoi(argv[1]);       // Group number
    data_client_number = (int32_t)atoi(argv[2]); // Data client number

    group_info = &_group_info[group_number];                             // Group info
    dclient_info = &_dclient_info[data_client_number];                   // Data client info
    dclient_info->server_name = sg4::this_actor::get_host()->get_name(); // Server name
    dclient_info->navailable = 0;

    // Wait until group info is ready
    {
        std::unique_lock lock(*group_info->mutex);
        while (group_info->on == 0)
            group_info->cond->wait(lock);
    }

    dclient_info->working.store(uniform_int(1, 2));
    dclient_info->total_storage = (int32_t)ran_distri(group_info->db_distri, group_info->da_param, group_info->db_param);

    // Create ask for files processes (1 per attached project)
    dclient_info->nprojects = atoi(group_info->proj_args[0]);
    dclient_info->sum_priority = 0;
    for (int i = 0; i < dclient_info->nprojects; i++)
        dclient_info->sum_priority += (double)atof(group_info->proj_args[i * 3 + 3]);
    for (int i = 0; i < dclient_info->nprojects; i++)
    {
        ask_for_files_params = new s_ask_for_files_t();
        ask_for_files_params->project_number = (char)atoi(group_info->proj_args[i * 3 + 2]);
        ask_for_files_params->project_priority = (char)atoi(group_info->proj_args[i * 3 + 3]);
        ask_for_files_params->group_info = group_info;
        ask_for_files_params->dclient_info = dclient_info;
        ask_for_files_params->mailbox = bprintf("%s%d", dclient_info->server_name.c_str(), ask_for_files_params->project_number);

        sg4::Actor::create("ask_for_files_thread", sg4::this_actor::get_host(), data_client_ask_for_files, ask_for_files_params);
        // if ((MSG_process_create("", , ask_for_files_params, MSG_host_self())) == NULL)
        // {
        //     printf("Error creating thread\n");
        // }
    }

    // Set asynchronous receiving in mailbox
    // MSG_mailbox_set_async(dclient_info->server_name);
    sg4::Mailbox *mailbox = sg4::Mailbox::by_name(dclient_info->server_name);

    while (1)
    {

        // Available
        if (dclient_info->working.load() == 2)
        {
            dclient_info->working.store(1);
            random = (ran_distri(group_info->av_distri, group_info->aa_param, group_info->ab_param) * 3600.0);
            if (ceil(random + sg4::Engine::get_clock() >= maxtt))
                random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            time = sg4::Engine::get_clock() + random;
        }

        // Non available
        if (dclient_info->working.load() == 1 && ceil(sg4::Engine::get_clock()) >= time)
        {
            random = (ran_distri(group_info->nv_distri, group_info->na_param, group_info->nb_param) * 3600.0);
            if (ceil(random + sg4::Engine::get_clock() >= maxtt))
                random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            if (random > 0)
                dclient_info->working.store(0);
            dclient_info->navailable += random;
            sg4::this_actor::sleep_for(random);
            dclient_info->working.store(2);
        }

        // Receive message
        msg = mailbox->get<dsmessage>();

        // Termination message
        if (msg->type == TERMINATION)
        {
            delete (msg);
            count++;
            if (count == dclient_info->nprojects)
                break;
            msg = NULL;
            continue;
        }
        else if (msg->type == NO)
        {
            delete (msg);
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
    pdatabase_t database = NULL;       // Database
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

    dclient_info = &_dclient_info[data_client_number]; // Data client info

    std::vector<sg4::CommPtr> _dscomm;

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

        database = &_pdatabase[(int)msg->proj_number];

        // Reply with output file
        if (msg->type == REPLY)
        {
            disk_access2(database->output_file_size);
        }
        // Input file request
        else if (msg->type == REQUEST)
        {
            // Read tasks from disk
            disk_access2(database->input_file_size);

            // Create the message

            // Answer the client
            comm = sg4::Mailbox::by_name(msg->answer_mailbox)->put_async(new int(2), database->input_file_size);

            // Store the asynchronous communication created in the dictionary
            delete_completed_communications(_dscomm);
            _dscomm.push_back(comm);
        }

        delete (msg);
        msg = NULL;

        // Iteration end time
        t1 = sg4::Engine::get_clock();

        // Accumulate total time server is busy
        if (t0 < maxtt && t0 >= maxwt)
            dclient_info->time_busy += (t1 - t0);
    }

    dclient_info->ask_for_files_mutex->lock();
    dclient_info->finish = 1;
    dclient_info->ask_for_files_mutex->unlock();

    return 0;
}

/* ########## END DATA CLIENT ########## */

/*
 *	Projects initialization
 */
static void client_initialize_projects(client_t client, int argc, char **argv)
{
    std::map<std::string, project_t> dict;
    int number_proj;
    int i, index;

    number_proj = atoi(argv[0]);

    if (argc - 1 != number_proj * 3)
    {
        printf("Invalid number of parameters to client: %d. It should be %d\n", argc - 1, number_proj * 3);
        exit(1);
    }

    index = 1;
    for (i = 0; i < number_proj; i++)
    {
        project_t proj;
        s_task_t t;
        proj = new s_project_t();
        proj->name = std::string(argv[index++]);
        proj->number = (char)atoi(argv[index++]);
        proj->priority = (char)atoi(argv[index++]);
        proj->on = 1;

        proj->answer_mailbox = proj->name + client->name;

        proj->number_executed_task;   // Queue with task's numbers
        proj->workunit_executed_task; // Queue with task's sizes

        proj->tasks_ready_mutex = sg4::Mutex::create();
        proj->tasks_ready_cv_is_empty = sg4::ConditionVariable::create();

        proj->total_tasks_checked = 0;
        proj->total_tasks_executed = 0;
        proj->total_tasks_received = 0;
        proj->total_tasks_missed = 0;

        proj->client = client;

        dict[proj->name] = proj;
    }
    client->projects = dict;
}

/*
 *	Client ask for work:
 *
 *	- Request work to scheduling_server
 *	- Download input files from data server
 *	- Send execution results to scheduling_server
 *	- Upload output files to data server
 */
static int client_ask_for_work(client_t client, project_t proj, double percentage)
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

    pdatabase_t database = NULL; // Database
    // msg_error_t error;				// Sending result
    // double backoff = 300;				// 1 minute initial backoff

    // Scheduling server work request
    result_t sswork_reply_task = NULL; // Work reply task from scheduling server
    ssmessage_t sswork_request = NULL; // Work request message
    result_t sswork_reply = NULL;      // Work reply message

    // Data server input file request
    dsmessage_t dsinput_file_reply_task = NULL; // Input file reply task from data server
    dsmessage_t dsinput_file_request = NULL;    // Input file request message

    // Scheduling server execution results reply
    ssmessage_t ssexecution_results = NULL; // Execution results message

    // Data server output file reply
    dsmessage_t dsoutput_file = NULL; // Output file message

    std::string server_name; // Store data server name

    database = &_pdatabase[(int)proj->number]; // Boinc server info pointer

    // Check if there are executed results
    while (proj->total_tasks_executed > proj->total_tasks_checked)
    {

        // Create execution results message
        ssexecution_results = new s_ssmessage_t();
        ssexecution_results->type = REPLY;
        ssexecution_results->content = new s_reply_t();
        ssexecution_results->datatype = ssmessage_content::SReplyT;

        // Increase number of tasks checked
        proj->total_tasks_checked++;

        // Executed task status [SUCCES, FAIL]
        if (uniform_int(0, 99) < database->success_percentage)
        {
            ((reply_t)ssexecution_results->content)->status = SUCCESS;
            // Executed task value [CORRECT, INCORRECT]
            if (uniform_int(0, 99) < database->canonical_percentage)
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
        ((reply_t)ssexecution_results->content)->credits = (int32_t)((int64_t)database->job_duration / 1000000000.0 * CREDITS_CPU_S);
        // Create execution results task

        // Send message to the server
        auto &server_nm_ml = database->scheduling_servers[uniform_int(0, database->nscheduling_servers - 1)];
        auto where = sg4::Mailbox::by_name(server_nm_ml);

        where->put(ssexecution_results, REPLY_SIZE);

        if (database->output_file_storage == 0)
        {
            // Upload output files to data servers
            for (int32_t i = 0; i < database->dsreplication; i++)
            {
                dsoutput_file = new s_dsmessage_t();
                dsoutput_file->type = REPLY;
                auto where = database->data_servers[uniform_int(0, database->ndata_servers - 1)];

                sg4::Mailbox::by_name(where)->put(dsoutput_file, database->output_file_size);
            }
        }
        else
        {
            // Upload output files to data clients

            for (int32_t i = 0; i < database->dcreplication; i++)
            {
                server_name = database->data_clients[uniform_int(0, database->ndata_clients - 1)];
                int server_number = atoi(server_name.c_str() + 2) - NUMBER_ORDINARY_CLIENTS;
                if (!_dclient_info[server_number].working.load())
                {
                    i--;
                    continue;
                }

                dsoutput_file = new s_dsmessage_t();
                dsoutput_file->type = REPLY;
                sg4::Mailbox::by_name(server_name)->put(dsoutput_file, database->output_file_size);
            }
        }
    }

    // Request work
    sswork_request = new s_ssmessage_t();
    sswork_request->type = REQUEST;
    sswork_request->content = new s_request_t();
    sswork_request->datatype = ssmessage_content::SRequestT;
    ((request_t)sswork_request->content)->answer_mailbox = proj->answer_mailbox;
    ((request_t)sswork_request->content)->group_power = _group_info[client->group_number].group_power;
    ((request_t)sswork_request->content)->power = client->power;
    ((request_t)sswork_request->content)->percentage = percentage;

    auto &serve_nm_ml = database->scheduling_servers[uniform_int(0, database->nscheduling_servers - 1)];

    auto where = sg4::Mailbox::by_name(serve_nm_ml);

    where->put(sswork_request, REQUEST_SIZE);

    // MSG_task_receive(, ); // Receive reply from scheduling server
    sswork_reply = sg4::Mailbox::by_name(proj->answer_mailbox)->get<result>(); // Get work

    // Download input files (or generate them locally)
    if (uniform_int(0, 99) < (int)database->ifgl_percentage)
    {
        // Download only if the workunit was not downloaded previously
        if (uniform_int(0, 99) < (int)database->ifcd_percentage)
        {
            // printf("try to understand %d\n", sswork_reply->ninput_files);

            for (int32_t i = 0; i < sswork_reply->ninput_files; i++)
            {
                if (sswork_reply->input_files[i].empty())
                    continue;

                server_name = sswork_reply->input_files[i];

                // BORRAR (esta mal)
                if (i < database->dcreplication)
                {
                    int server_number = atoi(server_name.c_str() + 2) - NUMBER_ORDINARY_CLIENTS;
                    // printf("resto: %d, server_number: %d\n", NUMBER_ORDINARY_CLIENTS, server_number);
                    // printf("%d\n", _dclient_info[server_number].working);
                    //  BORRAR
                    // if(i==1) printf("%d\n", _dclient_info[server_number].working);
                    if (_dclient_info[server_number].working.load() == 0)
                        continue;
                }

                dsinput_file_request = new s_dsmessage_t();
                dsinput_file_request->type = REQUEST;
                dsinput_file_request->proj_number = proj->number;
                dsinput_file_request->answer_mailbox = proj->answer_mailbox;

                sg4::Mailbox::by_name(server_name)->put(dsinput_file_request, KB);

                // error = MSG_task_receive_with_timeout(&dsinput_file_reply_task, proj->answer_mailbox, backoff);		// Send input file reply
                dsinput_file_reply_task = sg4::Mailbox::by_name(proj->answer_mailbox)->get<s_dsmessage_t>();

                // printf("%d Tiempo: %f\n", server_number, t1-t0);

                // Timeout reached -> exponential backoff 2^N
                /*if(error == MSG_TIMEOUT){
                    backoff*=2;
                    //free(dsinput_file_request);
                    //MSG_task_destroy(dsinput_file_reply_task);
                    continue;
                }*/

                // Log request
                database->rfiles_mutex->lock();
                database->rfiles[i]++;
                database->rfiles_mutex->unlock();

                break;
            }
        }
    }

    if (sswork_reply->number_tasks == 0)
        proj->on = 0;

    // Insert received tasks in tasks swag
    for (int32_t i = 0; i < (int32_t)sswork_reply->number_tasks; i++)
    {
        task_t t = sswork_reply->tasks[i];
        t->msg_task = simgrid::s4u::Exec::init();
        t->msg_task->set_name(t->name);
        t->msg_task->set_flops_amount(t->duration);
        t->project = proj;
        proj->tasks_swag.push_back(*t);
    }

    // Increase the total number of tasks received
    proj->total_tasks_received = proj->total_tasks_received + sswork_reply->number_tasks;

    // Free
    delete (sswork_reply);

    // Signal main client process
    client->on = 0;
    client->sched_cond->notify_all();

    return 0;
}

/*****************************************************************************/
/* update shortfall(amount of work needed to keep 1 cpu busy during next ConnectionInterval) of client */
static void client_update_shortfall(client_t client)
{
    // printf("Executing client_update_shortfall\n");
    task_t task;
    project_t proj;
    std::map<std::string, project_t> &projects = client->projects;
    double total_time_proj;
    double total_time = 0;
    int64_t power; // (maximum 2⁶³-1)

    client->no_actions = 1;
    power = client->power;
    for (auto &[key, proj] : projects)
    {
        total_time_proj = 0;
        for (auto &task : proj->tasks_swag)
        {
            total_time_proj += (task.msg_task->get_remaining() * client->factor) / power;

            // printf("SHORTFALL(1) %g   %s    %g   \n",  sg4::Engine::get_clock(), proj->name,   MSG_task_get_remaining_computation(task->msg_task));
            client->no_actions = 0;
        }
        for (auto &task : proj->run_list)
        {
            total_time_proj += (task.msg_task->get_remaining() * client->factor) / power;
            client->no_actions = 0;
            // printf("SHORTFALL(2) %g  %s    %g   \n",  sg4::Engine::get_clock(), proj->name,   MSG_task_get_remaining_computation(task->msg_task));
        }
        total_time += total_time_proj;
        /* amount of work needed - total already loaded */
        proj->shortfall = _group_info[client->group_number].connection_interval * proj->priority / client->sum_priority - total_time_proj;

        if (proj->shortfall < 0)
            proj->shortfall = 0;
    }
    client->total_shortfall = _group_info[client->group_number].connection_interval - total_time;
    if (client->total_shortfall < 0)
        client->total_shortfall = 0;
}

/*
    Client work fetch
*/
static int client_work_fetch(client_t client)
{
    project_t selected_proj = NULL;
    static char first = 1;
    double work_percentage = 0;
    double control, sleep;

    sg4::this_actor::sleep_for(maxwt);
    sg4::this_actor::sleep_for(uniform_ab(0, 3600));

    // client_t client = MSG_process_get_data(MSG_process_self());
    std::map<std::string, project_t> &projects = client->projects;

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
            sg4::this_actor::sleep_for(sleep);
            client->ask_for_work_mutex->lock();

            continue;
        }

        client->ask_for_work_mutex->unlock();

        client_update_shortfall(client);

        selected_proj = NULL;
        for (auto &[key, proj] : projects)
        {
            /* if there are no running tasks so we can download from all projects. Don't waste processing time */
            // if (client->running_project != NULL && client->running_project->running_task && proj->long_debt < -_group_power[client->group_number].scheduling_interval) {
            // printf("Shortfall %s: %f\n", proj->name, proj->shortfall);
            if (!proj->on)
            {
                continue;
            }
            if (!client->no_actions && proj->long_debt < -_group_info[client->group_number].scheduling_interval)
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
                client->work_fetch_cond->wait(lock);

                //  above was -1 in the end
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

/*****************************************************************************/
/* Update client short and long term debt.
This function is called every schedulingInterval and when an action finishes
The wall_cpu_time must be updated when this function is called */
static void client_clean_short_debt(const client_t client)
{
    std::map<std::string, project_t> projects = client->projects;

    /* calcule a */
    for (auto &[key, proj] : projects)
    {
        proj->short_debt = 0;
        proj->wall_cpu_time = 0;
    }
}

static void client_update_debt(client_t client)
{
    double a, w, w_short;
    double total_debt_short = 0;
    double total_debt_long = 0;
    std::map<std::string, project_t> projects = client->projects;
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
/* verify whether the task will miss its deadline if it executes alone on cpu */
static int deadline_missed(task_t task)
{
    int64_t power; // (maximum 2⁶³-1)
    double remains;
    power = task->project->client->power;
    remains = task->msg_task->get_remaining() * task->project->client->factor;
    /* we're simulating only one cpu per host */
    if (sg4::Engine::get_clock() + (remains / power) > task->start + task->deadline)
    {
        // printf("power: %ld\n", power);
        // printf("remains: %f\n", remains);
        return 1;
    }
    return 0;
}

/* simulate task scheduling and verify if it will miss its deadline */
static int task_deadline_missed_sim(client_t client, project_t proj, task_t task)
{
    return task->sim_finish > (task->start + task->deadline - _group_info[client->group_number].scheduling_interval) * 0.9;
}

static void client_update_simulate_finish_time(client_t client)
{
    int total_tasks = 0;
    double clock_sim = sg4::Engine::get_clock();
    int64_t power = client->power;
    std::map<std::string, project_t> projects = client->projects;

    for (auto &[key, proj] : projects)
    {
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
        task_t min_task = NULL;
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
            task_t task;

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
    task_t task, task_next;
    project_t proj;
    std::map<std::string, project_t> projects = client->projects;

    client_update_simulate_finish_time(client);

    for (auto &[key, proj] : projects)
    {
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

static void schedule_job(client_t client, task_t job)
{
    /* task already running, just return */
    if (job->running)
    {
        if (client->running_project != job->project)
        {
            client->running_project->thread->suspend();
            job->project->thread->resume();

            // printf("=============  Suspend   %s     resume    %s  %g\n",   client->running_project->name, 			 job->project->name, sg4::Engine::get_clock());

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
        double remains;
        task_t task_temp = job->project->running_task;
        remains = task_temp->msg_task->get_remaining() * client->factor;
        task_temp->msg_task->cancel();
        // task_temp->msg_task.~intrusive_ptr();
        task_temp->msg_task = sg4::Exec::init();
        task_temp->msg_task->set_name(task_temp->name);
        task_temp->msg_task->set_flops_amount(remains);
        // printf("Creating task(%s)(%p) again, remains %lf\n", task_temp->name, task_temp, remains);

        task_temp->scheduled = 0;
        task_temp->running = 0;
    }
    /* stop running thread and start other */
    if (client->running_project)
    {
        client->running_project->thread->suspend();
        // printf("=============  Suspend   %s     %g  \n",   client->running_project->name, sg4::Engine::get_clock());
    }
    job->project->thread->resume();

    // printf("====================       resume    %s     %g\n",    job->project->name, sg4::Engine::get_clock());

    client->running_project = job->project;
}
/*****************************************************************************/
/* this function is responsible to schedule the right task for the next SchedulingInterval.
     We're simulating only one cpu per host, so when this functions schedule a task it's enought for this turn
FIXME: if the task finish exactly at the same time of this function is called (i.e. remains = 0). We loose a schedulingInterval of processing time, cause we schedule it again */
static void client_cpu_scheduling(client_t client)
{
    project_t proj, great_debt_proj = NULL;
    std::map<std::string, project_t> projects = client->projects;
    double great_debt;

#if 0
	for(auto& [key, proj]: projects) {
		proj->anticipated_debt = proj->short_debt;
	}
#endif

    /* schedule EDF task */
    /* We need to preemt the actions that may be executing on cpu, it is done by cancelling the action and creating a new one (with the remains amount updated) that will be scheduled later */
    // printf("-------------------   1 %g\n", sg4::Engine::get_clock());
    while (!client->deadline_missed.empty())
    {
        task_t task = *client->deadline_missed.begin();
        client->deadline_missed.erase(client->deadline_missed.begin());

        // printf("-------------------   2\n");

        if (deadline_missed(task))
        {
            // printf("Task-1(%s)(%p) from project(%s) deadline, skip it\n", task->name, task, task->project->name);
            // printf("-------------------3\n");

            task->project->total_tasks_missed = task->project->total_tasks_missed + 1;

            client_update_debt(client);      // FELIX
            client_clean_short_debt(client); // FELIX
            free_task(task);

            // printf("===================4\n");

            continue;
        }

        // printf("Client (%s): Scheduling task(%s)(%p) of project(%s)\n", client->name, MSG_task_get_name(task->msg_task), task, task->project->name);
        //  update debt (anticipated). It isn't needed due to we only schedule one job per host.

        if (task->tasks_hookup.is_linked())
            xbt::intrusive_erase(task->project->tasks_swag, *task);

        task->project->run_list.push_back(*task);

        /* keep the task in deadline heap */
        // printf("((((((((((((((  HEAP PUSH      3\n");
        client->deadline_missed.insert(task);
        schedule_job(client, task);
        return;
    }

    while (1)
    {
        // printf("==============================  5\n");
        great_debt_proj = NULL;
        task_t task = NULL;
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
        if (task)
        {
            if (deadline_missed(task))
            {
                // printf(">>>>>>>>>>>> Task-2(%s)(%p) from project(%s) deadline, skip it\n", task->name, task, task->project->name);
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

/*****************************************************************************/

static int client_execute_tasks(project_t proj)
{
    task_t task;
    int32_t number;

    // printf("Running thread to execute tasks from project %s in %s  %g\n", proj->name, 			MSG_host_get_name(MSG_host_self()),  MSG_get_host_speed(MSG_host_self()));

    /* loop, execute tasks from queue until they finish or are cancelled by the main thread */
    while (1)
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

        // error_t err = MSG_task_execute(task->msg_task);
        task->msg_task->set_host(sg4::this_actor::get_host());
        task->msg_task->start();

        // if (err == MSG_OK)
        {
            number = (int32_t)atoi(task->name.c_str());
            // printf("s%d TERMINO EJECUCION DE %d en %f\n", proj->number, number, sg4::Engine::get_clock());
            proj->number_executed_task.push(number);
            proj->workunit_executed_task.push(task->workunit);
            proj->total_tasks_executed++;
            // printf("%f\n", proj->client->workunit_executed_task);
            // t1 = sg4::Engine::get_clock();

            // printf("-----(3)------Task(%s)(%s) from project(%s) finished, le queda %g --   cuanto  %g  %g %d\n", task->name, task, proj->name, MSG_task_get_remaining_computation(task->msg_task), t1-t0 ,sg4::Engine::get_clock());

            task->running = 0;
            proj->wall_cpu_time += sg4::Engine::get_clock() - proj->client->last_wall;
            proj->client->last_wall = sg4::Engine::get_clock();
            client_update_debt(proj->client);
            client_clean_short_debt(proj->client);

            proj->running_task = NULL;
            free_task(task);

            proj->client->on = 1;
            proj->client->sched_cond->notify_all();
            continue;
        }

        printf("%f: ---(2)--------Task(%s)(%p) from project(%s) error finished  duration = %g   power=  %g\n", sg4::Engine::get_clock(), task->name, task, proj->name, task->msg_task->get_remaining(), sg4::this_actor::get_host()->get_speed());
        task->running = 0;
        proj->running_task = NULL;
        free_task(task);
        continue;
    }

    proj->thread = NULL;

    // printf("Finished execute_tasks %s in %f\n", proj->client->name, sg4::Engine::get_clock());

    return 0;
}

/*****************************************************************************/

static client_t client_new(int argc, char *argv[])
{
    client_t client;
    std::string work_string;
    // xbt_dict_cursor_t cursor = NULL;
    int32_t group_number;
    double r = 0, aux = -1;
    int index = 1;

    client = new s_client_t();

    client->group_number = group_number = (int32_t)atoi(argv[index++]);

    // Initialize values
    if (argc > 3)
    {
        _group_info[group_number].group_power = (int32_t)sg4::this_actor::get_host()->get_speed();
        _group_info[group_number].n_clients = (int32_t)atoi(argv[index++]);
        _group_info[group_number].connection_interval = atof(argv[index++]);
        _group_info[group_number].scheduling_interval = atof(argv[index++]);
        _group_info[group_number].max_power = atof(argv[index++]);
        _group_info[group_number].min_power = atof(argv[index++]);
        _group_info[group_number].sp_distri = (char)atoi(argv[index++]);
        _group_info[group_number].sa_param = atof(argv[index++]);
        _group_info[group_number].sb_param = atof(argv[index++]);
        _group_info[group_number].db_distri = (char)atoi(argv[index++]);
        _group_info[group_number].da_param = atof(argv[index++]);
        _group_info[group_number].db_param = atof(argv[index++]);
        _group_info[group_number].av_distri = (char)atoi(argv[index++]);
        _group_info[group_number].aa_param = atof(argv[index++]);
        _group_info[group_number].ab_param = atof(argv[index++]);
        _group_info[group_number].nv_distri = (char)atoi(argv[index++]);
        _group_info[group_number].na_param = atof(argv[index++]);
        _group_info[group_number].nb_param = atof(argv[index++]);
        if ((argc - 20) % 3 != 0)
        {
            aux = atof(argv[index++]);
        }
        _group_info[group_number].proj_args = &argv[index];
        _group_info[group_number].on = argc - index;

        _group_info[group_number].cond->notify_all();
    }
    else
    {
        std::unique_lock lock(*_group_info[group_number].mutex);
        while (_group_info[group_number].on == 0)
            _group_info[group_number].cond->wait(lock);
        if (argc == 3)
            aux = atof(argv[index]);
    }

    if (aux == -1)
    {
        aux = ran_distri(_group_info[group_number].sp_distri, _group_info[group_number].sa_param, _group_info[group_number].sb_param);
        if (aux > _group_info[group_number].max_power)
            aux = _group_info[group_number].max_power;
        else if (aux < _group_info[group_number].min_power)
            aux = _group_info[group_number].min_power;
    }

    client->power = (int64_t)(aux * 1000000000.0);

    client->factor = (double)client->power / _group_info[group_number].group_power;

    client->name = sg4::this_actor::get_host()->get_name();

    client_initialize_projects(client, _group_info[group_number].on, _group_info[group_number].proj_args);
    // client->deadline_missed; // FELIX, antes había 8. = xbt_heap_new(8, NULL)

    // printf("Client power: %f GFLOPS\n", client->power/1000000000.0);

    client->on = 0;
    client->running_project = NULL;

    // Suspender a work_fetch_thread cuando la máquina se cae
    client->ask_for_work_mutex = sg4::Mutex::create();
    client->ask_for_work_cond = sg4::ConditionVariable::create();

    client->suspended = 0;

    client->sched_mutex = sg4::Mutex::create();
    client->sched_cond = sg4::ConditionVariable::create();
    client->work_fetch_mutex = sg4::Mutex::create();
    client->work_fetch_cond = sg4::ConditionVariable::create();

    client->finished = 0;

    client->mutex_init = sg4::Mutex::create();
    client->cond_init = sg4::ConditionVariable::create();
    client->initialized = 0;
    client->n_projects = 0;

    work_string = bprintf("work_fetch:%s", client->name.c_str());
    client->work_fetch_thread = sg4::Actor::create(work_string, sg4::this_actor::get_host(), client_work_fetch, client);

    // printf("Starting client %s, ConnectionInterval %lf SchedulingInterval %lf\n", client->name, _group_info[client->group_number].connection_interval, _group_power[client->group_number].scheduling_interval);

    /* start one thread to each project to run tasks */
    for (auto &[key, proj] : client->projects)
    {

        std::string proj_name = bprintf("%s:%s\n", key.c_str(), client->name.c_str());
        proj->thread = sg4::Actor::create(proj_name, sg4::this_actor::get_host(), &client_execute_tasks, proj);
        // {
        //     printf("Error creating thread\n");
        //     xbt_abort();
        // }
        r += proj->priority;
        client->n_projects++;
    }

    client->mutex_init->lock();
    client->sum_priority = r;
    client->initialized = 1;
    client->cond_init->notify_all();
    client->mutex_init->unlock();

    return client;
}

// Main client function
int client(int argc, char *argv[])
{
    client_t client;
    project_t proj;
    ssmessage_t msg;
    dsmessage_t msg2;
    int working = 0, i;
    int time_sim = 0;
    int64_t power;
    double time = 0, random = 0;
    double available = 0, notavailable = 0;
    double time_wait;

    client = client_new(argc, argv);
    power = client->power;

    // printf("Starting client %s\n", client->name);

    while (ceil(sg4::Engine::get_clock()) < maxtt)
    {
        // printf("%s finished: %d, nprojects: %d en %f\n", client->name, client->finished, client->n_projects, sg4::Engine::get_clock());
#if 1
        if (!working)
        {
            working = 1;
            random = (ran_distri(_group_info[client->group_number].av_distri, _group_info[client->group_number].aa_param, _group_info[client->group_number].ab_param) * 3600.0);
            if (ceil(random + sg4::Engine::get_clock()) >= maxtt)
            {
                // printf("%f\n", random);
                random = (double)std::max(maxtt - sg4::Engine::get_clock(), 0.0);
            }
            available += random;
            // printf("Weibull: %f\n", random);
            time = sg4::Engine::get_clock() + random;
        }
#endif

        /* increase wall_cpu_time to the project running task */
        if (client->running_project && client->running_project->running_task)
        {
            client->running_project->wall_cpu_time += sg4::Engine::get_clock() - client->last_wall;
            client->last_wall = sg4::Engine::get_clock();
        }
        // SAUL
        client_update_debt(client);
        client_update_deadline_missed(client);
        client_cpu_scheduling(client);

        if (client->on)
            client->work_fetch_cond->notify_all();

            /*************** SIMULAR CAIDA DEL CLIENTE ****/

#if 1
        // printf("Clock(): %g\n", sg4::Engine::get_clock());
        // printf("time: %g\n", time);
        if (working && ceil(sg4::Engine::get_clock()) >= time)
        {
            working = 0;
            random = (ran_distri(_group_info[client->group_number].nv_distri, _group_info[client->group_number].na_param, _group_info[client->group_number].nb_param) * 3600.0);

            if (ceil(random + sg4::Engine::get_clock()) > maxtt)
            {
                // printf("%f\n", random);
                random = std::max(maxtt - sg4::Engine::get_clock(), 0.0);
                working = 1;
            }

            notavailable += random;
            // printf("Lognormal: %f\n", random);

            if (client->running_project)
                client->running_project->thread->suspend();

            client->ask_for_work_mutex->lock();
            client->suspended = random;
            client->work_fetch_cond->notify_all();
            client->ask_for_work_mutex->unlock();

            // printf(" Cliente %s sleep %e\n", client->name, sg4::Engine::get_clock());

            sg4::this_actor::sleep_for(random);

            if (client->running_project)
                client->running_project->thread->resume();

            // printf(" Cliente %s RESUME %e\n", client->name, sg4::Engine::get_clock());
        }
#endif

        /*************** FIN SIMULAR CAIDA DEL CLIENTE ****/

        try
        {
            time_wait = std::min(maxtt - sg4::Engine::get_clock(), _group_info[client->group_number].scheduling_interval);
            if (time_wait < 0)
                time_wait = 0;
            std::unique_lock lock(*client->sched_mutex);
            client->sched_cond->wait_for(lock, time_wait);
        }
        catch (std::exception &e)
        {
            time_sim++;
            std::cout << "exception int the line " << __LINE__ << ' ' << e.what() << std::endl;
        }
    }

    client->work_fetch_cond->notify_all();

    {
        std::unique_lock lock(*client->ask_for_work_mutex);
        while (client->suspended != -1)
            client->ask_for_work_cond->wait(lock);
    }

    // printf("Client %s finish at %e\n", client->name, sg4::Engine::get_clock());

// Imprimir resultados de ejecucion del cliente
#if 0
	xbt_dict_foreach(client->projects, cursor, key, proj) {
                printf("Client %s:   Projet: %s    total tasks executed: %d  total tasks received: %d total missed: %d\n",
                        client->name, proj->name, proj->total_tasks_executed,
                        proj->total_tasks_received, proj->total_tasks_missed);
        }
#endif

    // Print client finish
    // printf("Client %s %f GLOPS finish en %g sec. %g horas.\t Working: %0.1f%% \t Not working %0.1f%%\n", client->name, client->power/1000000000.0, t0, t0/3600.0, available*100/(available+notavailable), (notavailable)*100/(available+notavailable));

    _group_info[client->group_number].mutex->lock();
    _group_info[client->group_number].total_available += available * 100 / (available + notavailable);
    _group_info[client->group_number].total_notavailable += (notavailable) * 100 / (available + notavailable);
    _group_info[client->group_number].total_power += power;
    _group_info[client->group_number].mutex->unlock();

    // Finish client
    _oclient_mutex->lock();
    for (auto &[key, proj] : client->projects)
    {
        proj->thread->kill();
        _pdatabase[(int)proj->number].nfinished_oclients++;
        // printf("%s, Num_clients: %d, Total_clients: %d\n", client->name, num_clients[proj->number], nclients[proj->number]);
        //  Send finishing message to project_database
        if (_pdatabase[(int)proj->number].nfinished_oclients == _pdatabase[(int)proj->number].nordinary_clients)
        {
            for (i = 0; i < _pdatabase[(int)proj->number].nscheduling_servers; i++)
            {
                msg = new s_ssmessage_t();
                msg->type = TERMINATION;
                auto ser_n_mb = _pdatabase[(int)proj->number].scheduling_servers[i];
                sg4::Mailbox::by_name(ser_n_mb)->put(msg, 1);
            }
            for (i = 0; i < _pdatabase[(int)proj->number].ndata_clients; i++)
            {
                msg2 = new s_dsmessage_t();
                msg2->type = TERMINATION;
                auto cl_n_mb = _pdatabase[(int)proj->number].data_clients[i];

                sg4::Mailbox::by_name(cl_n_mb)->put(msg2, 1);
            }
        }
    }
    _oclient_mutex->unlock();

    delete (client);

    return 0;
} /* end_of_client */

/*****************************************************************************/

/** Test function */
void test_all(int argc, char *argv[], sg4::Engine &e)
{
    const char *platform_file = argv[1];
    const char *application_file = argv[2];

    // printf("Executing test_all\n");

    int i, days, hours, min;
    double t; // Program time

    // sg4::Engine::set_config("network/model:IB");

    t = (double)time(NULL);

    {
        /*  Simulation setting */
        e.load_platform(platform_file);

        // e.on_simulation_end_cb?
        e.register_function("print_results", &print_results);
        e.register_function("init_database", init_database);
        e.register_function("work_generator", work_generator);
        e.register_function("validator", validator);
        e.register_function("assimilator", assimilator);
        e.register_function("scheduling_server_requests", scheduling_server_requests);
        e.register_function("scheduling_server_dispatcher", scheduling_server_dispatcher);
        e.register_function("data_server_requests", data_server_requests);
        e.register_function("data_server_dispatcher", data_server_dispatcher);
        e.register_function("data_client_server_requests", data_client_server_requests);
        e.register_function("data_client_server_dispatcher", data_client_server_dispatcher);
        e.register_function("data_client_requests", data_client_requests);
        e.register_function("data_client_dispatcher", data_client_dispatcher);
        // client(
        e.register_function("client", client);
        e.load_deployment(application_file);
    }

    e.run();
    // printf( " Simulation time %g sec. %g horas\n", sg4::Engine::get_clock(), sg4::Engine::get_clock()/3600);

    for (i = 0; i < NUMBER_CLIENT_GROUPS; i++)
    {
        printf(" Group %d. Average power: %f GFLOPS. Available: %0.1f%% Not available %0.1f%%\n", i, (double)_group_info[i].total_power / _group_info[i].n_clients / 1000000000.0, _group_info[i].total_available * 100.0 / (_group_info[i].total_available + _group_info[i].total_notavailable), (_group_info[i].total_notavailable) * 100.0 / (_group_info[i].total_available + _group_info[i].total_notavailable));
        _total_power += _group_info[i].total_power;
        _total_available += _group_info[i].total_available;
        _total_notavailable += _group_info[i].total_notavailable;
    }

    printf("\n Clients. Average power: %f GFLOPS. Available: %0.1f%% Not available %0.1f%%\n\n", (double)_total_power / NUMBER_CLIENTS / 1000000000.0, _total_available * 100.0 / (_total_available + _total_notavailable), (_total_notavailable) * 100.0 / (_total_available + _total_notavailable));

    t = (double)time(NULL) - t;    // Program time
    days = (int)(t / (24 * 3600)); // Calculate days
    t -= (days * 24 * 3600);
    hours = (int)(t / 3600); // Calculate hours
    t -= (hours * 3600);
    min = (int)(t / 60); // Calculate minutes
    t -= (min * 60);
    printf(" Execution time:\n %d days %d hours %d min %d s\n\n", days, hours, min, (int)round(t));

} /* end_of_test_all */

/* Main function */
int main(int argc, char *argv[])
{
    int j;
    sg4::Engine e(&argc, argv);
    // MSG_init(&argc, argv);

    if (argc != NUMBER_PROJECTS * 4 + 3)
    {
        printf("Usage: %s PLATFORM_FILE DEPLOYMENT_FILE NUMBER_CLIENTS_PROJECT1 [NUMBER_CLIENTS_PROJECT2, ..., NUMBER_CLIENTS_PROJECTN] TOTAL_NUMBER_OF_CLIENTS \n", argv[0]);
        printf("Example: %s platform.xml deloyment.xml 1000 500 1200\n", argv[0]);
        exit(1);
    }

    seed(clock());

    _total_power = 0;
    _total_available = 0;
    _total_notavailable = 0;
    _pdatabase = new s_pdatabase_t[NUMBER_PROJECTS];
    _sserver_info = new s_sserver_t[NUMBER_SCHEDULING_SERVERS];
    _dserver_info = new s_dserver_t[NUMBER_DATA_SERVERS];
    _dcserver_info = new s_dcserver_t[NUMBER_DATA_CLIENT_SERVERS];
    _dclient_info = new s_dclient_t[NUMBER_DATA_CLIENTS];
    _group_info = new s_group_t[NUMBER_CLIENT_GROUPS];

    for (int i = 0; i < NUMBER_PROJECTS; i++)
    {
        /* Project attributes */

        _pdatabase[i].nclients = (int32_t)atoi(argv[i + 3]);
        _pdatabase[i].ndata_clients = (int32_t)atoi(argv[i + NUMBER_PROJECTS + 3]);
        _pdatabase[i].nordinary_clients = _pdatabase[i].nclients - _pdatabase[i].ndata_clients;
        _pdatabase[i].nscheduling_servers = (char)atoi(argv[i + NUMBER_PROJECTS * 2 + 3]);
        _pdatabase[i].scheduling_servers.resize((int)_pdatabase[i].nscheduling_servers);
        for (j = 0; j < _pdatabase[i].nscheduling_servers; j++)
            _pdatabase[i].scheduling_servers[j] = std::string(bprintf("s%" PRId32 "%" PRId32, i + 1, j));

        _pdatabase[i].ndata_client_servers = (char)atoi(argv[i + NUMBER_PROJECTS * 3 + 3]);
        _pdatabase[i].data_client_servers.resize((int)_pdatabase[i].ndata_client_servers);
        for (j = 0; j < _pdatabase[i].ndata_client_servers; j++)
        {
            _pdatabase[i].data_client_servers[j] = std::string(bprintf("t%" PRId32 "%" PRId32, i + 1, j));
        }
        _pdatabase[i].data_clients.resize(_pdatabase[i].ndata_clients);
        // for (j = 0; j < _pdatabase[i].ndata_clients; j++)
        //     _pdatabase[i].data_clients[j] = NULL;

        _pdatabase[i].nfinished_oclients = 0;
        _pdatabase[i].nfinished_dclients = 0;

        /* Work generator */

        _pdatabase[i].ncurrent_results = 0;
        _pdatabase[i].ncurrent_workunits = 0;
        _pdatabase[i].current_results;
        _pdatabase[i].r_mutex = sg4::Mutex::create();
        _pdatabase[i].ncurrent_error_results = 0;
        _pdatabase[i].current_error_results;
        _pdatabase[i].w_mutex = sg4::Mutex::create();
        _pdatabase[i].er_mutex = sg4::Mutex::create();
        _pdatabase[i].wg_empty = sg4::ConditionVariable::create();
        _pdatabase[i].wg_full = sg4::ConditionVariable::create();
        _pdatabase[i].wg_end = 0;
        _pdatabase[i].wg_dcs = 0;

        /* Validator */

        _pdatabase[i].ncurrent_validations = 0;
        _pdatabase[i].current_validations;
        _pdatabase[i].v_mutex = sg4::Mutex::create();
        _pdatabase[i].v_empty = sg4::ConditionVariable::create();
        _pdatabase[i].v_end = 0;

        /* Assimilator */

        _pdatabase[i].ncurrent_assimilations = 0;
        _pdatabase[i].current_assimilations;
        _pdatabase[i].a_mutex = sg4::Mutex::create();
        _pdatabase[i].a_empty = sg4::ConditionVariable::create();
        _pdatabase[i].a_end = 0;

        /* Data clients */

        _pdatabase[i].rfiles_mutex = sg4::Mutex::create();
        _pdatabase[i].dcrfiles_mutex = sg4::Mutex::create();
        _pdatabase[i].dsuploads_mutex = sg4::Mutex::create();
        _pdatabase[i].dcuploads_mutex = sg4::Mutex::create();

        /* Input files */

        _pdatabase[i].ninput_files = 0;
        // _pdatabase[i].input_files;
        // _pdatabase[i].i_mutex = sg4::Mutex::create();
        // _pdatabase[i].i_empty = sg4::ConditionVariable::create();
        // _pdatabase[i].i_full = sg4::ConditionVariable::create();

        /* File deleter */

        _pdatabase[i].ncurrent_deletions = 0;
        _pdatabase[i].current_deletions;

        /* Output files */

        // _pdatabase[i].noutput_files = 0;
        // _pdatabase[i].output_files;
        // _pdatabase[i].o_mutex = sg4::Mutex::create();
        // _pdatabase[i].o_empty = sg4::ConditionVariable::create();
        // _pdatabase[i].o_full = sg4::ConditionVariable::create();

        /* Synchronization */

        _pdatabase[i].ssrmutex = sg4::Mutex::create();
        _pdatabase[i].ssdmutex = sg4::Mutex::create();
        _pdatabase[i].dcmutex = sg4::Mutex::create();
        _pdatabase[i].barrier = sg4::Barrier::create(_pdatabase[i].nscheduling_servers + _pdatabase[i].ndata_client_servers + 4);
    }

    for (j = 0; j < NUMBER_SCHEDULING_SERVERS; j++)
    {
        _sserver_info[j].mutex = sg4::Mutex::create();
        _sserver_info[j].cond = sg4::ConditionVariable::create();
        _sserver_info[j].client_requests;
        _sserver_info[j].Nqueue = 0;
        _sserver_info[j].EmptyQueue = 0;
        _sserver_info[j].time_busy = 0;
    }

    for (j = 0; j < NUMBER_DATA_SERVERS; j++)
    {
        _dserver_info[j].mutex = sg4::Mutex::create();
        _dserver_info[j].cond = sg4::ConditionVariable::create();
        _dserver_info[j].client_requests;
        _dserver_info[j].Nqueue = 0;
        _dserver_info[j].EmptyQueue = 0;
        _dserver_info[j].time_busy = 0;
    }

    for (j = 0; j < NUMBER_DATA_CLIENT_SERVERS; j++)
    {
        _dcserver_info[j].mutex = sg4::Mutex::create();
        _dcserver_info[j].cond = sg4::ConditionVariable::create();
        _dcserver_info[j].client_requests;
        _dcserver_info[j].Nqueue = 0;
        _dcserver_info[j].EmptyQueue = 0;
        _dcserver_info[j].time_busy = 0;
    }

    for (j = 0; j < NUMBER_DATA_CLIENTS; j++)
    {
        _dclient_info[j].mutex = sg4::Mutex::create();
        _dclient_info[j].ask_for_files_mutex = sg4::Mutex::create();
        _dclient_info[j].cond = sg4::ConditionVariable::create();
        _dclient_info[j].client_requests;
        _dclient_info[j].Nqueue = 0;
        _dclient_info[j].EmptyQueue = 0;
        _dclient_info[j].time_busy = 0;
        _dclient_info[j].finish = 0;
    }

    for (j = 0; j < NUMBER_CLIENT_GROUPS; j++)
    {
        _group_info[j].total_power = 0;
        _group_info[j].total_available = 0;
        _group_info[j].total_notavailable = 0;
        _group_info[j].on = 0;
        _group_info[j].mutex = sg4::Mutex::create();
        _group_info[j].cond = sg4::ConditionVariable::create();
    }

    _oclient_mutex = sg4::Mutex::create();
    _dclient_mutex = sg4::Mutex::create();

    test_all(argc, argv, e);
}
