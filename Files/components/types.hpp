#pragma once
#include <list>
#include <vector>
#include <boost/intrusive/list.hpp>
#include "thread_safe_queue.hpp"

namespace intrusive = boost::intrusive;

#define KB 1024 // 1 KB in bytes

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
// typedef struct ssmessage s_ssmessage_t, *ssmessage_t; // Message to scheduling server
typedef struct request s_request_t, *request_t; // Client request to scheduling server
typedef struct reply s_reply_t, *reply_t;       // Client reply to scheduling server
// typedef struct result s_result_t, *result_t;                      // Result
typedef struct dsmessage s_dsmessage_t, *dsmessage_t;    // Message to data server
typedef struct dcsrequest s_dcsrequest_t, *dcsrequest_t; // Data client request to data client server
typedef struct dcsreply s_dcsreply_t, *dcsreply_t;       // Message to data server
typedef struct dcsmessage s_dcsmessage_t, *dcsmessage_t; // Message to data client server
typedef struct dcmessage s_dcmessage_t, *dcmessage_t;    // Message to data server
typedef struct dcworkunit s_dcworkunit_t, *dcworkunit_t; // Data client workunit
// typedef struct WorkunitT s_WorkunitT, *WorkunitT;                 // Workunit
// typedef struct task s_TaskT *, *TaskT *; // Task
// typedef struct project s_ProjectInstanceOnClient *, *ProjectInstanceOnClient *; // Project
typedef struct client s_client_t, *client_t;                      // Client
typedef struct scheduling_server s_sserver_t, *sserver_t;         // Scheduling server
typedef struct data_server s_dserver_t, *dserver_t;               // Data server
typedef struct data_client_server s_dcserver_t, *dcserver_t;      // Data client server
typedef struct data_client s_dclient_t, *dclient_t;               // Data client
typedef struct client_group s_group_t, *group_t;                  // Client group
typedef struct ask_for_files s_ask_for_files_t, *ask_for_files_t; // Ask for files params
struct WorkunitT;
struct ProjectInstanceOnClient;
struct TaskT;

typedef enum
{
    SReplyT,
    SRequestT
} ssmessage_content;

/* Message to scheduling server */
struct SchedulingServerMessage
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
    std::string host_name;
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
struct AssignedResult
{
    uint32_t number;                      // Result number among all in workunit
    WorkunitT *workunit;                  // Associated workunit
    int32_t ninput_files;                 // Number of input files
    std::vector<std::string> input_files; // Input files names (URLs)
    int32_t number_tasks;                 // Number of tasks (usually one)
    TaskT *corresponding_tasks;           // Tasks
};

struct ResultBag
{
    std::vector<AssignedResult *> results; // to send several results from a scheduling server
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
    std::string dclient_name;                     // Data client name
    std::map<std::string, WorkunitT *> workunits; // Workunits
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
    std::string answer_mailbox;                   // Answer mailbox
    int32_t nworkunits;                           // Number of workunits
    std::map<std::string, WorkunitT *> workunits; // Workunits
};

/* Workunit */
struct WorkunitT
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
struct TaskT
{
    std::string workunit;   // Workunit of the task
    uint32_t result_number; // associated result's number among all in the workunit
    std::string name;       // Task name
    char scheduled;         // Task scheduled (it is in tasks_ready list) [0,1]
    char running;           // Task is actually running on cpu [0,1]
    intrusive::list_member_hook<> tasks_hookup;
    intrusive::list_member_hook<> run_list_hookup;
    intrusive::list_member_hook<> sim_tasks_hookup;
    sg4::ExecPtr msg_task;
    ProjectInstanceOnClient *project; // Project reference
    int64_t heap_index;               // (maximum 2⁶³-1)
    int64_t deadline;                 // Task deadline (maximum 2⁶³-1)
    double duration;                  // Task duration in flops
    double sent_time;
    double sim_finish; // Simulated finish time of task
    double sim_remains;
    double time_spent_on_execution = 0; // the simulation time during which task was executed.
    double last_start_time;             // the last time task was taken for execution

    bool is_freed = false;
};

using TaskSwagT = intrusive::list<TaskT, intrusive::member_hook<TaskT, intrusive::list_member_hook<>,
                                                                &TaskT::tasks_hookup>>;
using SimTaskSwagT = intrusive::list<TaskT, intrusive::member_hook<TaskT, intrusive::list_member_hook<>,
                                                                   &TaskT::sim_tasks_hookup>>;
using RunListSwagT = intrusive::list<TaskT, intrusive::member_hook<TaskT, intrusive::list_member_hook<>,
                                                                   &TaskT::run_list_hookup>>;

/* Client project */
struct ProjectInstanceOnClient
{

    /* General data of project */

    std::string name;           // Project name
    std::string answer_mailbox; // Client answer mailbox
    char priority;              // Priority (maximum 255)
    char number;                // Project number (maximum 255)
    char on;                    // Project is working

    char success_percentage;   // Percentage of success results
    char canonical_percentage; // Percentage of success results that make up a consensus

    /* Data to control tasks and their execution */

    TaskT *running_task;  // which is the task that is running on thread */
    sg4::ActorPtr thread; // thread running the tasks of this project */
    client_t client;      // Client pointer
    sg4::MutexPtr tasks_swag_mutex;

    TaskSwagT tasks_swag;   // nearly runnable jobs of project, insert at tail to keep ordered
    SimTaskSwagT sim_tasks; // nearly runnable jobs of project, insert at tail to keep ordered
    sg4::MutexPtr run_list_mutex;

    RunListSwagT run_list; // list of jobs running, normally only one tasks, but if a deadline may occur it can put another here
    sg4::MutexPtr tasks_ready_mutex;
    sg4::ConditionVariablePtr tasks_ready_cv_is_empty;
    std::queue<TaskT *> tasks_ready;       // synchro queue, thread to execute task */
    TSQueue<int32_t> number_executed_task; // Queue with executed task numbers. ATTENTION: Store not task number but associated result's one

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
    bool operator()(const TaskT *a, const TaskT *b) const
    {
        return a->sent_time + a->deadline > b->sent_time + b->deadline;
    }
};

/* Client */
struct client
{
    std::string name;
    std::map<std::string, ProjectInstanceOnClient *> projects; // all projects of client
    // todo: here I need operator < to be correct
    std::set<TaskT *, TaskCmp> deadline_missed;
    ProjectInstanceOnClient *running_project;
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
struct ProjectDatabaseValue
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

    char ifgl_percentage;     // Percentage of input files generated locally
    char ifcd_percentage;     // Number of workunits that share the same input files
    char averagewpif;         // Average workunits per input files
    int64_t input_file_size;  // Size of the input files needed for a result associated with a workunit of this project
    int64_t output_file_size; // Size of the output files needed for a result associated with a workunit of this project
    int64_t job_duration;     // Job length in FLOPS

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
    int64_t ncancelled_results; // Number of cancelled results on client

    /* Workunit statistics */

    int64_t total_credit;               // Total credit granted
    int64_t nworkunits;                 // Number of workunits created
    int64_t nvalid_workunits;           // Number of workunits validated
    int64_t nerror_workunits;           // Number of erroneous workunits
    int64_t ncurrent_deleted_workunits; // Number of current deleted workunits

    /* Work generator */

    int64_t ncurrent_results;                             // Number of results currently in the system
    int64_t ncurrent_error_results;                       // Number of error results currently in the system
    int64_t ncurrent_workunits;                           // Number of current workunits
    std::map<std::string, WorkunitT *> current_workunits; // Current workunits
    std::list<AssignedResult *> current_results;          // Current results
    std::queue<WorkunitT *> current_error_results;        // Current error results
    sg4::MutexPtr w_mutex;                                // Workunits mutex
    sg4::MutexPtr r_mutex;                                // Results mutex
    sg4::MutexPtr er_mutex;                               // Error results mutex
    sg4::ConditionVariablePtr wg_empty;                   // Work generator CV empty
    sg4::ConditionVariablePtr wg_full;                    // Work generator CV full
    int wg_dcs;                                           // Work generator (data client server)
    int wg_end;                                           // Work generator end

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
    int64_t ncurrent_deletions;             // Number of workunits that should be deleted
    TSQueue<WorkunitT *> current_deletions; // Workunits that should be deleted

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
using ProjectDatabase = std::vector<ProjectDatabaseValue>;

/* Scheduling server */
struct scheduling_server
{
    char project_number;                                   // Project number
    std::string server_name;                               // Server name
    sg4::MutexPtr mutex;                                   // Mutex
    sg4::ConditionVariablePtr cond;                        // VC
    std::queue<SchedulingServerMessage *> client_requests; // Requests queue
    int32_t EmptyQueue;                                    // Queue end [0,1]
    int64_t Nqueue;                                        // Queue size
    double time_busy;                                      // Time the server is busy
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
    std::vector<double> sa_param; // Speed A parameter
    std::vector<double> sb_param; // Speed B parameter
    std::vector<double> da_param; // Disk speed A parameter
    std::vector<double> db_param; // Disk speed B parameter
    std::vector<double> aa_param; // Availability A parameter
    std::vector<double> ab_param; // Availability B parameter
    std::vector<double> na_param; // Non availability A parameter
    std::vector<double> nb_param; // Non availability B parameter
    double max_power;             // Maximum host power
    double min_power;             // Minimum host power

    int nfinished_oclients;
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