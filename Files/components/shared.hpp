#pragma once
#include "types.hpp"
#include "tools/thermometer.hpp"
#include <boost/random/linear_congruential.hpp>

// todo: fix ./generator to set values in this file, not in boinc.cpp
extern int WARM_UP_TIME;       // Warm up time in hours
extern int MAX_SIMULATED_TIME; // Simulation time in hours
#define PRECISION 0.00001      // Accuracy (used in client_work_fetch())
#define REPLY_SIZE 10 * KB     // Reply size
#define CREDITS_CPU_S 0.002315 // Credits per second (1 GFLOP machine)
#define WORK_FETCH_PERIOD 60   // Work fetch period
#define REQUEST_SIZE 10 * KB   // Request size
static const int DOUBLE_EPS = 1e-9;

// #define NUMBER_CLIENTS 1000     // Number of clients
// #define NUMBER_DATA_CLIENTS 100 // Number of data clients
// #define NUMBER_ORDINARY_CLIENTS (NUMBER_CLIENTS - NUMBER_DATA_CLIENTS)
extern int g_total_number_clients;      // Number of clients
extern int g_total_number_data_clients; // Number of data clients
extern int g_total_number_ordinary_clients;

/* Simulation time */
extern double maxtt; // Total simulation time in seconds
extern double maxst; // Simulation time in seconds
extern double maxwt; // Warm up time in seconds

// a random generator for general purpose
extern std::unique_ptr<boost::rand48> g_rndg;
// a random generator for setting a host's power
extern std::unique_ptr<boost::rand48> g_rndg_for_host_speed;
// a random generator for setting a disk capacity in the data client's code
extern std::unique_ptr<boost::rand48> g_rndg_for_disk_cap;
// a random generator for setting a an availability model in the data client's code
extern std::unique_ptr<boost::rand48> g_rndg_for_data_client_avail;
// a random generator for setting a an availability model in the client's code
extern std::unique_ptr<boost::rand48> g_rndg_for_client_avail;

extern std::unordered_map<std::string, thermometer::Measure<double> *> g_measure_task_duration_per_project;
extern thermometer::Measure<double> *g_measure_non_availability_duration;

class SharedDatabase
{
public:
    /* Server info */
    static ProjectDatabase _pdatabase;             // Projects databases
    static sserver_t _sserver_info;                // Scheduling servers information
    static dserver_t _dserver_info;                // Data servers information
    static dcserver_t _dcserver_info;              // Data client servers information
    static std::vector<data_client> _dclient_info; // Data clients information
    static std::vector<client_group> _group_info;  // Client groups information
};

/*
 *	 Server compute simulation. Wait till the end of a executing task
 */
void compute_server(int flops);

/*
 * to free memory, we delete all completed asynchronous communications. It shouldn't affect the clocks in the simmulator.
 */
void delete_completed_communications(sg4::ActivitySet &pending_comms);

/*
 *	Generate result
 */
AssignedResult *generate_result(ProjectDatabaseValue &project, WorkunitT *workunit, int X);

bool the_same_client_group(const std::string &a, const std::string &b);
